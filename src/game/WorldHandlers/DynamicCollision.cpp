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

#include "DynamicCollision.h"
#include "GameObjectModel.h"
#include "FusedTerrain.h"           // INVALID_HEIGHT_VALUE
#include "terrain/Terrain.hpp"      // tileIndex

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

using world::terrain::Aabb;
using world::terrain::Vec3;

namespace
{
    const int GRID_COUNT = 64;

    // tileIndex() decreases as the world coordinate grows, so the low/high world edges
    // map to the high/low tile indices. Normalise to an inclusive [lo,hi] tile range.
    void TileRange(float loCoord, float hiCoord, int& outLo, int& outHi)
    {
        int a = world::terrain::tileIndex(loCoord);
        int b = world::terrain::tileIndex(hiCoord);
        if (a > b)
        {
            std::swap(a, b);
        }
        outLo = std::max(0, a);
        outHi = std::min(GRID_COUNT - 1, b);
    }
} // namespace

template <typename F>
void DynamicCollision::ForEachCandidate_(float minx, float miny, float maxx, float maxy, F&& f) const
{
    const uint32_t epoch = ++m_epoch;

    int txLo, txHi, tyLo, tyHi;
    TileRange(minx, maxx, txLo, txHi);
    TileRange(miny, maxy, tyLo, tyHi);

    for (int tx = txLo; tx <= txHi; ++tx)
    {
        for (int ty = tyLo; ty <= tyHi; ++ty)
        {
            auto it = m_buckets.find(CellKey_(tx, ty));
            if (it == m_buckets.end())
            {
                continue;
            }
            // Flat sweep: the bucket is a contiguous vector of bodies that, in steady
            // state, never changes. Each body's own AABB reject happens in Raycast.
            for (GameObjectModel* m : it->second)
            {
                if (m->GetEpoch() == epoch)
                {
                    continue;   // already visited via another tile it straddles
                }
                m->SetEpoch(epoch);
                f(*m);
            }
        }
    }

    // The handful of transports/elevators: no bucket, just scan.
    for (GameObjectModel* m : m_movers)
    {
        if (m->GetEpoch() == epoch)
        {
            continue;
        }
        m->SetEpoch(epoch);
        f(*m);
    }
}

void DynamicCollision::Insert(GameObjectModel& model)
{
    if (std::find(m_all.begin(), m_all.end(), &model) != m_all.end())
    {
        return;
    }
    m_all.push_back(&model);

    if (model.IsMover())
    {
        m_movers.push_back(&model);
        return;
    }

    const Aabb& b = model.GetBounds();
    int txLo, txHi, tyLo, tyHi;
    TileRange(b.lo.x, b.hi.x, txLo, txHi);
    TileRange(b.lo.y, b.hi.y, tyLo, tyHi);

    model.Cells().clear();
    for (int tx = txLo; tx <= txHi; ++tx)
    {
        for (int ty = tyLo; ty <= tyHi; ++ty)
        {
            const uint32_t key = CellKey_(tx, ty);
            m_buckets[key].push_back(&model);
            model.Cells().push_back(key);
        }
    }
}

void DynamicCollision::Remove(GameObjectModel& model)
{
    auto a = std::find(m_all.begin(), m_all.end(), &model);
    if (a == m_all.end())
    {
        return;
    }
    m_all.erase(a);

    if (model.IsMover())
    {
        m_movers.erase(std::remove(m_movers.begin(), m_movers.end(), &model), m_movers.end());
        return;
    }

    for (uint32_t key : model.Cells())
    {
        auto it = m_buckets.find(key);
        if (it == m_buckets.end())
        {
            continue;
        }
        auto& v = it->second;
        v.erase(std::remove(v.begin(), v.end(), &model), v.end());
        if (v.empty())
        {
            m_buckets.erase(it);
        }
    }
    model.Cells().clear();
}

bool DynamicCollision::Contains(const GameObjectModel& model) const
{
    return std::find(m_all.begin(), m_all.end(), &model) != m_all.end();
}

void DynamicCollision::Refresh(GameObjectModel& model)
{
    const bool tracked = Contains(model);
    if (!tracked)
    {
        model.UpdatePose();
        return;
    }

    // A mover keeps no bucket membership, so re-posing is all there is to do.
    if (model.IsMover())
    {
        model.UpdatePose();
        return;
    }

    // A frozen body changed pose (door rotated, object teleported): re-file it. Pose
    // changes are rare, so paying a bucket re-file here keeps the query path free of
    // any per-tick maintenance.
    Remove(model);
    model.UpdatePose();
    Insert(model);
}

void DynamicCollision::Update(uint32_t /*diff*/)
{
    // Only the movers need per-tick work, and only to re-derive their pose+bounds.
    // There is no tree to rebalance.
    for (GameObjectModel* m : m_movers)
    {
        m->UpdatePose();
    }
}

float DynamicCollision::NearestHitFraction(float x1, float y1, float z1,
                                           float x2, float y2, float z2) const
{
    const Vec3 a{x1, y1, z1}, b{x2, y2, z2};
    const Vec3 seg = b - a;
    const float len = std::sqrt(world::terrain::dot(seg, seg));
    if (len < 1e-4f)
    {
        return 2.0f;
    }
    const Vec3 dir{seg.x / len, seg.y / len, seg.z / len};

    float best = len;
    bool hit = false;

    const float minx = std::min(a.x, b.x), maxx = std::max(a.x, b.x);
    const float miny = std::min(a.y, b.y), maxy = std::max(a.y, b.y);

    ForEachCandidate_(minx, miny, maxx, maxy, [&](const GameObjectModel& m)
    {
        // tMax tightens as we go, so later bodies reject earlier.
        if (auto t = m.Raycast(a, dir, best))
        {
            if (*t >= 0.f && *t < best)
            {
                best = *t;
                hit = true;
            }
        }
    });

    return hit ? (best / len) : 2.0f;
}

bool DynamicCollision::IsInLineOfSight(float x1, float y1, float z1,
                                       float x2, float y2, float z2) const
{
    return NearestHitFraction(x1, y1, z1, x2, y2, z2) > 1.0f;
}

float DynamicCollision::GetHeight(float x, float y, float z, float maxSearchDist) const
{
    const Vec3 origin{x, y, z};
    const Vec3 down{0.0f, 0.0f, -1.0f};

    float best = maxSearchDist;
    bool hit = false;

    ForEachCandidate_(x, y, x, y, [&](const GameObjectModel& m)
    {
        if (auto t = m.Raycast(origin, down, best))
        {
            if (*t >= 0.f && *t < best)
            {
                best = *t;
                hit = true;
            }
        }
    });

    return hit ? (z - best) : INVALID_HEIGHT_VALUE;
}
