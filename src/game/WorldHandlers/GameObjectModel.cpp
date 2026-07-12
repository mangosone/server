/**
 * MaNGOS is a full featured server for World of Warcraft, supporting
 * the following clients: 1.12.x, 2.4.3, 3.3.5a, 4.3.4a and 5.4.8
 *
 * Copyright (C) 2005-2025 MaNGOS <https://www.getmangos.eu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * World of Warcraft, and all World of Warcraft or Warcraft art, images,
 * and lore are copyrighted by Blizzard Entertainment, Inc.
 */

#include "GameObjectModel.h"
#include "GameObject.h"

#include "terrain/TileSerializer.hpp"

#include <cmath>
#include <mutex>
#include <unordered_map>

using world::terrain::Aabb;
using world::terrain::ICollisionModel;
using world::terrain::Mat3;
using world::terrain::Transform;
using world::terrain::Vec3;

namespace
{
    // The baked per-display model store. One immutable model per displayId, shared by
    // every live game object of that display. A miss is cached as null so a display
    // without collision geometry costs one disk probe, not one per spawn.
    //
    // Map update threads call Create() concurrently, so this is mutex-guarded (the old
    // VMapManager2 model store had the same contract).
    std::string s_modelDir;
    std::mutex s_modelMutex;
    std::unordered_map<uint32_t, std::shared_ptr<const ICollisionModel>> s_modelCache;

    std::shared_ptr<const ICollisionModel> LoadModel(uint32_t displayId)
    {
        std::lock_guard<std::mutex> lk(s_modelMutex);

        auto it = s_modelCache.find(displayId);
        if (it != s_modelCache.end())
        {
            return it->second;
        }

        std::shared_ptr<const ICollisionModel> model;
        if (!s_modelDir.empty())
        {
            // A baked GO model is a one-instance, identity-posed tile: the geometry
            // stays in the model's own frame, exactly as the placement math expects.
            const std::string path = s_modelDir + "/go_" + std::to_string(displayId) + ".tile";
            if (auto tile = world::terrain::readTile(path))
            {
                if (!tile->instances.empty())
                {
                    model = tile->instances[0].model;
                }
            }
        }

        s_modelCache[displayId] = model;    // caches null misses too
        return model;
    }

    // Rotation matrix of a unit quaternion, row-stored to match Mat3::mul.
    Mat3 QuatToMat3(float x, float y, float z, float w)
    {
        Mat3 r;
        r.m = {1.f - 2.f * (y * y + z * z), 2.f * (x * y - w * z),       2.f * (x * z + w * y),
               2.f * (x * y + w * z),       1.f - 2.f * (x * x + z * z), 2.f * (y * z - w * x),
               2.f * (x * z - w * y),       2.f * (y * z + w * x),       1.f - 2.f * (x * x + y * y)};
        return r;
    }
} // namespace

void GameObjectModel::SetModelDir(const std::string& dir)
{
    std::lock_guard<std::mutex> lk(s_modelMutex);
    s_modelDir = dir;
    s_modelCache.clear();
}

std::shared_ptr<const ICollisionModel> GameObjectModel::AcquireModel(uint32_t displayId)
{
    return LoadModel(displayId);
}

GameObjectModel* GameObjectModel::Create(GameObject const* owner)
{
    if (!owner)
    {
        return NULL;
    }

    std::shared_ptr<const ICollisionModel> model = LoadModel(owner->GetDisplayId());
    if (!model || model->empty())
    {
        return NULL;
    }

    GameObjectModel* gom = new GameObjectModel();
    gom->m_owner = owner;
    gom->m_model = std::move(model);

    // Only transports re-pose continuously; the rest are frozen between events.
    const uint32 type = owner->GetGoType();
    gom->m_mover = (type == GAMEOBJECT_TYPE_TRANSPORT || type == GAMEOBJECT_TYPE_MO_TRANSPORT);

    gom->m_collidable = owner->IsCollisionEnabled();
    gom->UpdatePose();
    return gom;
}

void GameObjectModel::UpdatePose()
{
    const float qx = m_owner->GetFloatValue(GAMEOBJECT_ROTATION + 0);
    const float qy = m_owner->GetFloatValue(GAMEOBJECT_ROTATION + 1);
    const float qz = m_owner->GetFloatValue(GAMEOBJECT_ROTATION + 2);
    const float qw = m_owner->GetFloatValue(GAMEOBJECT_ROTATION + 3);

    m_xf = Transform(Vec3{m_owner->GetPositionX(), m_owner->GetPositionY(), m_owner->GetPositionZ()},
                     QuatToMat3(qx, qy, qz, qw),
                     m_owner->GetObjectScale());

    // World bounds = the model's local box, all eight corners pushed through the
    // placement. Cheap, and it is the only per-pose work a frozen body ever does.
    const Aabb& lb = m_model->bounds();
    Aabb wb;
    for (int i = 0; i < 8; ++i)
    {
        const Vec3 corner{(i & 1) ? lb.hi.x : lb.lo.x,
                          (i & 2) ? lb.hi.y : lb.lo.y,
                          (i & 4) ? lb.hi.z : lb.lo.z};
        wb.expand(m_xf.localToWorld(corner));
    }
    m_worldBounds = wb;
}

std::optional<float> GameObjectModel::Raycast(const Vec3& origin, const Vec3& dir, float tMax) const
{
    if (!m_collidable || !m_model || m_model->empty())
    {
        return std::nullopt;
    }

    auto inv = [](float d) { return std::fabs(d) > 1e-9f ? 1.0f / d : 1e30f; };
    const Vec3 invDir{inv(dir.x), inv(dir.y), inv(dir.z)};
    if (!m_worldBounds.intersectsRay(origin, invDir, tMax))
    {
        return std::nullopt;
    }

    // localToWorld(oL + t*dL) == origin + t*dir, so t is a world-space distance along
    // dir no matter the placement's scale -- feed tMax straight through.
    const Vec3 oL = m_xf.worldToLocal(origin);
    const Vec3 dL = m_xf.worldToLocalDirection(dir);
    return m_model->raycastNearest(oL, dL, tMax);
}
