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

#ifndef MANGOS_H_GAMEOBJECTMODEL
#define MANGOS_H_GAMEOBJECTMODEL

// A game object's collision body: a placement (world Transform) over an immutable,
// shared collision model baked by displayId (gomodels/go_<displayId>.tile).
//
// This is deliberately the SAME shape as world::terrain::StaticInstance -- transform +
// shared model + world bounds -- because a game object is nothing more than a static
// instance whose transform can change. That symmetry is what lets the dynamic and the
// baked-terrain queries share one narrowphase: the binned-SAH BVH the baker built over the
// model's collidable faces -- a WMO's (doors, bridges, ships) or an M2 hull's -- raycast in
// model-local space.
//
// Replaces the old VMAP::WorldModel-backed model: no G3D, no .vmo model store, no BIH.

#include "terrain/Geometry.hpp"
#include "terrain/ICollisionModel.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class GameObject;

class GameObjectModel
{
    public:
        // Directory of the baked per-display models (<DataDir>/gomodels). Set once at
        // startup, before any map loads.
        static void SetModelDir(const std::string& dir);

        // Builds a collision body for a game object, or NULL when its display has no
        // baked model (most game objects are decorative and never collide).
        static GameObjectModel* Create(GameObject const* owner);

        // The shared, immutable model baked for a display, straight from the store --
        // no placement, no world transform, no DynamicCollision membership.
        //
        // A MO transport wants exactly this. It is not a body IN the world: it is a
        // little world of its own, and its deck is queried in the space the mesh was
        // baked in. Posing that mesh into world coordinates would mean knowing the
        // vessel's true pose, which the server does not (the client runs the Catmull
        // path; we only estimate it) -- so we never do. NULL when the display has no
        // baked geometry.
        static std::shared_ptr<const world::terrain::ICollisionModel>
            AcquireModel(uint32_t displayId);

        GameObject const* GetOwner() const { return m_owner; }
        const world::terrain::Aabb& GetBounds() const { return m_worldBounds; }

        bool IsCollidable() const { return m_collidable; }
        void SetCollidable(bool enabled) { m_collidable = enabled; }

        // Transports and elevators re-pose every tick; everything else is pose-frozen
        // between (rare) rotation/teleport events. DynamicCollision partitions on this.
        bool IsMover() const { return m_mover; }

        // Recompute the world transform + bounds from the owner's current pose.
        void UpdatePose();

        // Nearest hit of the world-space ray (origin, dir) within tMax, as a distance
        // along dir. nullopt when the ray misses (or the body is non-collidable).
        std::optional<float> Raycast(const world::terrain::Vec3& origin,
                                     const world::terrain::Vec3& dir,
                                     float tMax) const;

        // --- DynamicCollision bookkeeping (bucket membership + per-query dedup) ---
        std::vector<uint32_t>& Cells() { return m_cells; }
        const std::vector<uint32_t>& Cells() const { return m_cells; }
        uint32_t GetEpoch() const { return m_epoch; }
        void SetEpoch(uint32_t e) const { m_epoch = e; }

    private:
        GameObjectModel() = default;

        GameObject const* m_owner = nullptr;
        std::shared_ptr<const world::terrain::ICollisionModel> m_model;
        world::terrain::Transform m_xf;
        world::terrain::Aabb m_worldBounds;

        bool m_collidable = false;
        bool m_mover = false;

        // Tile buckets this body is currently filed under (frozen bodies only).
        std::vector<uint32_t> m_cells;
        // Last query that visited this body, so a body spanning several tiles is
        // narrowphase-tested at most once per query.
        mutable uint32_t m_epoch = 0;
};

#endif // MANGOS_H_GAMEOBJECTMODEL
