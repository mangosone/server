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

// The one terrain/collision query class: floor height, liquid, area, line of sight,
// all answered from the fused .tile files. The tile cache and its locking live below
// and are safe to hit from any map-update thread.

#include "FusedTerrain.h"
#include "terrain/TileSerializer.hpp"
#include "terrain/WmoModel.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>

using world::terrain::Vec3;
using world::terrain::Aabb;
using world::terrain::StaticInstance;
using world::terrain::LiquidInfo;
using world::terrain::LiquidKind;

std::string FusedTerrain::s_tileDir;

namespace
{
    // Tile-cache sweep cadence, and how long an unpinned tile may sit unqueried before
    // it is reclaimed. The idle window is comfortably longer than the sweep interval so
    // a tile on the edge of an active grid -- pinned by nobody, but hit constantly by
    // LoS/height queries reaching across the grid boundary -- is never evicted from
    // under a live query.
    constexpr uint32_t kSweepIntervalMs = 60u * 1000u;        // once a minute
    constexpr uint32_t kTileIdleMs = 5u * 60u * 1000u;        // five idle minutes

    // First static hit of the segment a->b as a fraction in [0,1], or a value > 1
    // when nothing blocks. The segment is used EXACTLY as the caller gave it, then
    // transformed into each model's own space and tested against its BVH.
    //
    // It used to be lifted a yard first ("agent mid-height"), which was wrong three
    // ways: no caller passes a foot position (WorldObject::IsWithinLOS lifts both ends
    // by 2 yards, and every GetHitPosition caller lifts both ends itself), so it was a
    // SECOND lift that made static LoS a yard more permissive than the eyes it modelled;
    // DynamicCollision applied no such lift, so game objects and the walls they sit in
    // were sampled a yard apart on one ray; and the fraction it returned was resolved
    // back onto the UNLIFTED segment, so the reported hit point did not lie on the
    // surface that was hit. Owning the lift is the caller's job -- as it was under vmap.
    float SegmentHitFrac(const std::vector<const StaticInstance*>& insts,
                         const Vec3& a, const Vec3& b)
    {
        const Vec3 seg = b - a;
        if (world::terrain::dot(seg, seg) < 1e-6f)
        {
            return 2.0f;
        }

        auto inv = [](float d) { return std::fabs(d) > 1e-9f ? 1.0f / d : 1e30f; };
        const Vec3 invDir{inv(seg.x), inv(seg.y), inv(seg.z)};

        float best = 2.0f;
        for (const StaticInstance* inst : insts)
        {
            if (!inst->model || inst->model->empty())
            {
                continue;
            }
            if (!inst->worldBounds.intersectsRay(a, invDir, 1.0f))
            {
                continue;
            }

            const Vec3 oL = inst->xf.worldToLocal(a);
            const Vec3 dL = inst->xf.worldToLocal(b) - oL;

            if (auto t = inst->model->raycastNearest(oL, dL, 1.0f))
            {
                if (*t >= 0.f && *t < best)
                {
                    best = *t;
                }
            }
        }
        return best;
    }
} // namespace

FusedTerrain::FusedTerrain(uint32_t mapId) : m_mapId(mapId)
{
}

bool FusedTerrain::HasTile(uint32_t mapId, int gx, int gy)
{
    if (s_tileDir.empty())
    {
        return false;
    }
    const std::string path = s_tileDir + "/t_" + std::to_string(mapId) + "_" +
                             std::to_string(gx) + "_" + std::to_string(gy) + ".tile";
    std::ifstream f(path, std::ios::binary);
    if (f.good())
    {
        return true;
    }
    // Instance maps built from one global WMO carry no ADT grid tiles.
    const std::string gpath = s_tileDir + "/w_" + std::to_string(mapId) + ".tile";
    return std::ifstream(gpath, std::ios::binary).good();
}

FusedTerrain::TilePtr FusedTerrain::LoadCell(int tx, int ty) const
{
    if (s_tileDir.empty())
    {
        return nullptr;
    }
    std::string path = s_tileDir + "/t_" + std::to_string(m_mapId) + "_" +
                       std::to_string(tx) + "_" + std::to_string(ty) + ".tile";
    return world::terrain::readTile(path);
}

FusedTerrain::TilePtr FusedTerrain::TileAt(float x, float y) const
{
    const int tx = world::terrain::tileIndex(x);
    const int ty = world::terrain::tileIndex(y);
    if (tx < 0 || tx >= GRID_COUNT || ty < 0 || ty >= GRID_COUNT)
    {
        return nullptr;
    }

    const uint32_t now = m_clockMs.load(std::memory_order_relaxed);

    // Hit path: shared lock, so concurrent queries (including every instance of the
    // same dungeon, which all share this object) run in parallel instead of queueing.
    {
        std::shared_lock<std::shared_mutex> lk(m_mutex);
        if (m_loaded[tx][ty])
        {
            m_tileLastUse[tx][ty].store(now, std::memory_order_relaxed);
            return m_tiles[tx][ty];
        }
    }

    // Read from disk outside the lock (I/O should not stall other columns), then
    // publish. A racing thread may load the same cell too; storing either result
    // is fine since both describe the same tile.
    TilePtr tile = LoadCell(tx, ty);

    std::unique_lock<std::shared_mutex> lk(m_mutex);
    if (!m_loaded[tx][ty])
    {
        m_tiles[tx][ty] = tile;
        m_loaded[tx][ty] = 1;
    }
    m_tileLastUse[tx][ty].store(now, std::memory_order_relaxed);
    return m_tiles[tx][ty];
}

FusedTerrain::TilePtr FusedTerrain::GlobalWmo() const
{
    {
        std::shared_lock<std::shared_mutex> lk(m_mutex);
        if (m_globalWmoProbed)
        {
            return m_globalWmo;
        }
    }

    TilePtr tile;
    if (!s_tileDir.empty())
    {
        tile = world::terrain::readTile(s_tileDir + "/w_" + std::to_string(m_mapId) + ".tile");
    }

    std::unique_lock<std::shared_mutex> lk(m_mutex);
    if (!m_globalWmoProbed)
    {
        m_globalWmo = tile;
        m_globalWmoProbed = 1;
    }
    return m_globalWmo;
}

void FusedTerrain::EvictTile_(int tx, int ty) const
{
    // Keep m_loaded at 0 so the next query re-probes. We do NOT keep the "probed"
    // memo here because the memo's whole value is recording that the file is ABSENT;
    // this tile plainly exists, and re-reading it is the price of having released it.
    m_tiles[tx][ty].reset();
    m_loaded[tx][ty] = 0;
    m_tileLastUse[tx][ty].store(0, std::memory_order_relaxed);
}

void FusedTerrain::Update(uint32_t diff)
{
    const uint32_t now = m_clockMs.load(std::memory_order_relaxed) + diff;
    m_clockMs.store(now, std::memory_order_relaxed);

    m_sweepAccumMs += diff;
    if (m_sweepAccumMs < kSweepIntervalMs)
    {
        return;
    }
    m_sweepAccumMs = 0;

    // Lock order is grid-ref then tile cache. Nothing else takes both (LoadGrid /
    // UnloadGrid take only the former, TileAt only the latter), so this cannot invert.
    std::lock_guard<std::mutex> refLk(m_gridRefMutex);
    std::unique_lock<std::shared_mutex> lk(m_mutex);

    for (int tx = 0; tx < GRID_COUNT; ++tx)
    {
        for (int ty = 0; ty < GRID_COUNT; ++ty)
        {
            // Nothing to reclaim (absent, or an absent-tile memo we want to keep), or
            // an active grid still stands on it.
            if (!m_tiles[tx][ty] || m_gridRef[tx][ty] > 0)
            {
                continue;
            }
            // Unsigned subtraction, so this stays correct across the ms counter's wrap.
            if (now - m_tileLastUse[tx][ty].load(std::memory_order_relaxed) < kTileIdleMs)
            {
                continue;
            }
            EvictTile_(tx, ty);
        }
    }
}

bool FusedTerrain::GetHeight(float x, float y, float z, float& outZ,
                             float searchUp, float maxDrop) const
{
    TilePtr tile = TileAt(x, y);
    TilePtr gtile = GlobalWmo();
    if (!tile && !gtile)
    {
        return false;
    }

    const float ceiling = z + searchUp;
    float best = -std::numeric_limits<float>::max();
    bool found = false;

    // Terrain height only from the local ADT tile (global-WMO maps carry no ADT).
    if (tile)
    {
        if (auto h = tile->terrainHeight(x, y))
        {
            if (*h <= ceiling)
            {
                best = *h;
                found = true;
            }
        }
    }

    const Vec3 originW{x, y, ceiling};
    const Vec3 downDirW{0.0f, 0.0f, -1.0f};

    auto processInstances = [&](const std::vector<StaticInstance>& instances)
    {
        for (const auto& inst : instances)
        {
            if (!inst.model || inst.model->empty())
            {
                continue;
            }
            const Aabb& wb = inst.worldBounds;
            if (!wb.coversColumn(x, y))
            {
                continue;
            }
            if (wb.hi.z < ceiling - maxDrop || wb.lo.z > ceiling + 0.1f)
            {
                continue;
            }

            // Every instance -- including a map's global WMO -- stores its model in
            // model-local space plus a placement xf, so the world probe must be pulled
            // into that space. A global WMO's xf is NOT identity: it carries the
            // WoW->map half-turn about Z (worldBounds already reflect it, which is why
            // coversColumn passes), so raycasting the raw model directly in world space
            // silently misses the floor for every global-WMO map but the one whose xf
            // happens to be identity (Dire Maul). Transform like any other instance.
            const Vec3 oL = inst.xf.worldToLocal(originW);
            const Vec3 dL = inst.xf.worldToLocalDirection(downDirW);

            // localToWorld(oL + t*dL) == originW + t*downDirW, so t is already a
            // WORLD distance regardless of the instance scale -- no rescaling here.
            if (auto t = inst.model->raycastNearest(oL, dL, maxDrop))
            {
                const float hitZ = ceiling - *t;
                if (hitZ > best)
                {
                    best = hitZ;
                    found = true;
                }
            }
        }
    };

    if (tile)
    {
        processInstances(tile->instances);
    }
    if (gtile && gtile != tile)
    {
        processInstances(gtile->instances);
    }

    if (found)
    {
        outZ = best;
    }
    return found;
}

// Highest fused floor (terrain + static WMO/M2) at or just below z.
//
// This is the entry point the SERVER uses for "how high is the ground here", and it is
// deliberately not a plain GetHeight: a query point often sits a little UNDER the
// surface (a spawn buried a yard or two in a hillside), and a bare downward probe from
// z+2 simply misses the ground above it and reports nothing. Hence the second pass.
//
// It lives here, in the engine half, rather than in GridMap.cpp: it needs no DBC and no
// ACE, and keeping it here is what lets the offline probe tools ask the terrain exactly
// what mangosd would ask it. (While it sat game-side, the tools could only reach the raw
// GetHeight, and so reported "no floor" for every point that was merely under the
// ground -- a fault of the probe, not of the terrain.)
//
// checkVMap/maxSearchDist are kept for source compatibility with the old TerrainInfo
// signature but no longer branch: the fused tile always carries both terrain and
// collision, so there is nothing to switch on.
float FusedTerrain::GetHeightStatic(float x, float y, float z, bool /*checkVMap*/,
                                    float /*maxSearchDist*/) const
{
    float out;
    // Preferred: the nearest floor at or just below z (handles platforms, bridges,
    // multi-floor WMOs). A generous drop bound so a high fall still finds the floor.
    if (GetHeight(x, y, z, out, 2.0f, 10000.0f))
    {
        return out;
    }
    // Nothing at/below -> the point sits under the ground; snap up to the surface
    // above it, matching the old GetHeightStatic's "return the .map height" fallback.
    if (GetHeight(x, y, z, out, 10000.0f, 10000.0f))
    {
        return out;
    }
    return INVALID_HEIGHT_VALUE;
}

bool FusedTerrain::GetLiquid(float x, float y, float z, LiquidInfo& out) const
{
    TilePtr tile = TileAt(x, y);
    TilePtr gtile = GlobalWmo();
    if (!tile && !gtile)
    {
        return false;
    }

    bool have = false;
    LiquidInfo best;
    const Vec3 queryW{x, y, z};

    auto processLiquid = [&](const std::vector<StaticInstance>& instances)
    {
        for (const auto& inst : instances)
        {
            if (!inst.model || inst.model->empty())
            {
                continue;
            }
            const Aabb& wb = inst.worldBounds;
            if (!wb.coversColumn(x, y))
            {
                continue;
            }
            if (wb.hi.z < z - 50.f || wb.lo.z > z + 50.f)
            {
                continue;
            }

            // As in GetHeight: pull the world query into the instance's model space
            // (the global WMO's xf is not identity), then lift the local liquid Z back.
            const Vec3 pModel = inst.xf.worldToLocal(queryW);
            auto l = inst.model->liquidLocal(pModel);
            if (!l)
            {
                continue;
            }

            // Lift the surface back to world through the placement itself. The surface
            // sits directly over the query column at model-space (pModel.x, pModel.y),
            // so transforming that exact point is both exact and self-evidently right.
            // Reconstructing the lift by hand (z + dzLocal * scale * localToWorldDir(up).z)
            // applied the placement scale TWICE -- localToWorldDirection already scales --
            // and silently assumed the model's local Z was parallel to world Z.
            const Vec3 surfaceLocal{pModel.x, pModel.y, l->z};
            const float liquidWorldZ = inst.xf.localToWorld(surfaceLocal).z;

            // The baker already resolved this group's liquid: MOGP.groupLiquid (plus the
            // root MOHD flags and, where needed, LiquidType.dbc) collapsed into a
            // LiquidKind and its dbc row. Nothing left to classify here.
            const LiquidKind kind = static_cast<LiquidKind>(l->kind);
            if (kind == LiquidKind::None)
            {
                continue;   // the group names no liquid (MOGP.groupLiquid == 15)
            }

            if (!have || liquidWorldZ > best.level)
            {
                best = LiquidInfo{liquidWorldZ, kind, l->entry};
                have = true;
            }
        }
    };

    if (tile)
    {
        processLiquid(tile->instances);
    }
    if (gtile && gtile != tile)
    {
        processLiquid(gtile->instances);
    }

    // ADT liquid from the local tile.
    if (tile)
    {
        if (auto a = tile->liquidAt(x, y))
        {
            if (!have || a->level > best.level)
            {
                best = *a;
                have = true;
            }
        }
    }

    if (have)
    {
        out = best;
    }
    return have;
}

void FusedTerrain::CollectSegmentInstances_(Vec3 a, Vec3 b,
                                            std::vector<const StaticInstance*>& out,
                                            std::vector<TilePtr>& keepAlive) const
{
    const float minx = std::min(a.x, b.x), maxx = std::max(a.x, b.x);
    const float miny = std::min(a.y, b.y), maxy = std::max(a.y, b.y);

    const float dx = b.x - a.x, dy = b.y - a.y;
    const float segLenXY = std::sqrt(dx * dx + dy * dy);
    const int samples = std::max(2, static_cast<int>(segLenXY / (world::terrain::TILE_SIZE * 0.5f)) + 2);

    int lastTx = 0, lastTy = 0;
    bool have = false;
    auto gather = [&](const TilePtr& tile)
    {
        if (!tile)
        {
            return;
        }
        keepAlive.push_back(tile);
        for (const StaticInstance& inst : tile->instances)
        {
            const Aabb& wb = inst.worldBounds;
            if (wb.hi.x < minx || wb.lo.x > maxx || wb.hi.y < miny || wb.lo.y > maxy)
            {
                continue;
            }
            out.push_back(&inst);
        }
    };

    for (int i = 0; i < samples; ++i)
    {
        const float f = static_cast<float>(i) / static_cast<float>(samples - 1);
        const int tx = world::terrain::tileIndex(a.x + dx * f);
        const int ty = world::terrain::tileIndex(a.y + dy * f);
        if (have && tx == lastTx && ty == lastTy)
        {
            continue;
        }
        have = true;
        lastTx = tx;
        lastTy = ty;
        if (tx >= 0 && tx < GRID_COUNT && ty >= 0 && ty < GRID_COUNT)
        {
            gather(TileAt(a.x + dx * f, a.y + dy * f));
        }
    }

    gather(GlobalWmo());
}

float FusedTerrain::NearestHitFraction(float x1, float y1, float z1,
                                       float x2, float y2, float z2) const
{
    const Vec3 a{x1, y1, z1}, b{x2, y2, z2};
    std::vector<const StaticInstance*> insts;
    std::vector<TilePtr> keep;
    CollectSegmentInstances_(a, b, insts, keep);
    return SegmentHitFrac(insts, a, b);
}

bool FusedTerrain::IsInLineOfSight(float x1, float y1, float z1,
                                   float x2, float y2, float z2) const
{
    return NearestHitFraction(x1, y1, z1, x2, y2, z2) > 1.0f;   // clear
}

uint16_t FusedTerrain::GetAreaId(float x, float y) const
{
    TilePtr tile = TileAt(x, y);
    if (!tile || !tile->hasTerrain)
    {
        return 0;
    }
    return tile->areaId(x, y);
}

bool FusedTerrain::GetAreaInfo(float x, float y, float z, uint32_t& mogpFlags, int32_t& adtId,
                               int32_t& rootId, int32_t& groupId, float& groundZ) const
{
    TilePtr tile = TileAt(x, y);
    TilePtr gtile = GlobalWmo();
    if (!tile && !gtile)
    {
        return false;
    }

    // Generous downward search, matching how VMAP probes the WMO you stand in.
    constexpr float searchUp = 2.0f;
    constexpr float maxDrop = 300.0f;
    const float ceiling = z + searchUp;
    const Vec3 originW{x, y, ceiling};
    const Vec3 downW{0.0f, 0.0f, -1.0f};

    bool found = false;
    float bestZ = -std::numeric_limits<float>::max();
    uint32_t bMogp = 0;
    int32_t bAdt = 0, bRoot = 0, bGroup = 0;

    auto scan = [&](const std::vector<StaticInstance>& instances)
    {
        for (const auto& inst : instances)
        {
            const auto* wmo = dynamic_cast<const world::terrain::WmoModel*>(inst.model.get());
            if (!wmo || wmo->empty())
            {
                continue;
            }
            const Aabb& wb = inst.worldBounds;
            if (!wb.coversColumn(x, y))
            {
                continue;
            }
            if (wb.hi.z < ceiling - maxDrop || wb.lo.z > ceiling + 0.1f)
            {
                continue;
            }

            // Transform the world probe into the instance's model space -- the global
            // WMO's xf is not identity (see GetHeight).
            const Vec3 oL = inst.xf.worldToLocal(originW);
            const Vec3 dL = inst.xf.worldToLocalDirection(downW);

            // As in GetHeight: t comes back as a world distance already.
            if (auto a = wmo->areaInfo(oL, dL, maxDrop))
            {
                const float hitZ = ceiling - a->t;
                if (hitZ <= ceiling && hitZ > bestZ)
                {
                    bestZ = hitZ;
                    found = true;
                    bMogp = a->mogpFlags;
                    bGroup = static_cast<int32_t>(a->groupId);
                    bRoot = static_cast<int32_t>(wmo->rootId());
                    bAdt = inst.adtId;
                }
            }
        }
    };

    if (tile)
    {
        scan(tile->instances);
    }
    if (gtile && gtile != tile)
    {
        scan(gtile->instances);
    }

    // Roof guard: if the ADT terrain surface lies between the query point and the
    // WMO floor we found (terrain above the WMO group, but at/below the querier),
    // the querier is standing on the terrain "roof", not inside the WMO. Reject it
    // -- same test the old TerrainInfo::GetAreaInfo did against GridMap height.
    if (found && tile)
    {
        if (auto th = tile->terrainHeight(x, y))
        {
            if (z + 2.0f > *th && *th > bestZ)
            {
                found = false;
            }
        }
    }

    if (found)
    {
        mogpFlags = bMogp;
        adtId = bAdt;
        rootId = bRoot;
        groupId = bGroup;
        groundZ = bestZ;
    }
    return found;
}

bool FusedTerrain::IsOutdoors(float x, float y, float z) const
{
    uint32_t mogpFlags = 0;
    int32_t adtId = 0, rootId = 0, groupId = 0;
    float groundZ = 0.0f;
    // No WMO overhead -> outdoors by default.
    if (!GetAreaInfo(x, y, z, mogpFlags, adtId, rootId, groupId, groundZ))
    {
        return true;
    }
    // Outdoor bit 0x8000; on Outland (map 530) flying areas 0x8 also counts.
    const uint32_t mask = (m_mapId == 530) ? 0x8008u : 0x8000u;
    return (mogpFlags & mask) != 0;
}
