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

#ifndef MANGOS_H_DYNAMICCOLLISION
#define MANGOS_H_DYNAMICCOLLISION

// Game-object collision for one Map. Replaces DynamicMapTree (a BIH that was rebuilt
// periodically via balance()/update()).
//
// The old design treated every game object as dynamic and paid tree-rebuild cost for
// objects that never move. That is backwards for WoW: of all collidable game objects,
// effectively all of them are POSE-FROZEN after spawn (doors, chests, bridges,
// mailboxes -- a door "opening" flips a collidable *flag*, it does not move), and only
// a literal handful per map genuinely re-pose every tick (transports, elevators).
//
// So we partition on mobility, not on space:
//
//   * FROZEN bodies are filed into the SAME 64x64 tile grid FusedTerrain uses, keyed by
//     the same (tx,ty). A body is filed under every tile its world AABB overlaps, so
//     large bodies (bridges, ships) are found from any tile they touch -- unlike a
//     position-keyed object grid, which would file a 100yd ship under one cell.
//     In steady state these buckets never change, so a query is a flat, branch-friendly
//     AABB sweep over a contiguous vector -- no tree, no traversal stack, no rebuild.
//     At the object counts involved (tens per tile) a flat sweep beats any tree.
//
//   * MOVER bodies (transports) live in one small flat vector, re-posed once per tick.
//     Keeping them out of the buckets means a moving ship never churns the frozen
//     vectors, and n is small enough that a linear scan is optimal.
//
// Frozen bodies are filed on the same 64x64 tile grid the baked terrain instances use,
// keyed by the same (tx,ty), which is what makes the two worlds cheap to query back to
// back.
//
// Both worlds answer a segment query the same way: NearestHitFraction, a fraction of
// src->dest, > 1 when clear. Map asks BOTH over the SAME segment and keeps the smaller
// fraction, so there is one nearest hit and it is pulled back by modifyDist exactly once
// (Map::GetHitPosition). It must not be done the other way round -- bounding this class's
// sweep by the static hit, as Map used to, hides every body standing in the last
// modifyDist of the ray, because the static hit handed over had already been pulled back.
//
// The two candidate gathers are still separate loops over the shared grid. Merging them
// into a literal single walk saves only the tile-index arithmetic -- the raycasts, which
// dominate, are the same set either way -- and would couple Map to FusedTerrain's tile
// internals, so it has deliberately not been done.
//
// Narrowphase is shared with the terrain: the baked WMO / M2 BVH, reached through
// ICollisionModel::raycastNearest, in model-local space.
//
// Threading: one instance per Map, touched only by that map's update thread. No locks.

#include "terrain/Geometry.hpp"

#include <cstdint>
#include <unordered_map>
#include <vector>

class GameObjectModel;

class DynamicCollision
{
    public:
        DynamicCollision() = default;

        void Insert(GameObjectModel& model);
        void Remove(GameObjectModel& model);
        bool Contains(const GameObjectModel& model) const;

        // Re-file a body after its pose changed (rotation, teleport, state change).
        void Refresh(GameObjectModel& model);

        // Per-tick: re-pose the movers (transports) and re-derive their bounds.
        void Update(uint32_t diff);

        int Size() const { return static_cast<int>(m_all.size()); }

        // True when nothing collidable blocks the segment a->b.
        bool IsInLineOfSight(float x1, float y1, float z1,
                             float x2, float y2, float z2) const;

        // Nearest collidable hit along a->b, as a fraction of the segment; > 1 when
        // nothing blocks. Same primitive, and the same units, as
        // FusedTerrain::NearestHitFraction, so Map can compare the two directly and
        // resolve only the winner into a point.
        float NearestHitFraction(float x1, float y1, float z1,
                                 float x2, float y2, float z2) const;

        // Highest collidable surface under (x,y) at or below z, within maxSearchDist.
        // Returns INVALID_HEIGHT_VALUE when nothing is under the column.
        float GetHeight(float x, float y, float z, float maxSearchDist) const;

    private:
        // Visit every body whose bucket overlaps the XY box, plus every mover, calling
        // f(model) at most once per body (epoch-stamped).
        template <typename F>
        void ForEachCandidate_(float minx, float miny, float maxx, float maxy, F&& f) const;

        static uint32_t CellKey_(int tx, int ty) { return uint32_t(tx) * 64u + uint32_t(ty); }

        std::unordered_map<uint32_t, std::vector<GameObjectModel*>> m_buckets;  // frozen
        std::vector<GameObjectModel*> m_movers;                                 // transports
        std::vector<GameObjectModel*> m_all;                                    // membership

        mutable uint32_t m_epoch = 0;
};

#endif // MANGOS_H_DYNAMICCOLLISION
