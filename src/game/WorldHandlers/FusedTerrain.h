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

#ifndef MANGOS_H_FUSEDTERRAIN
#define MANGOS_H_FUSEDTERRAIN

// One map's worth of *fused* terrain+collision tiles.
//
// Historically MaNGOS answered "how high is the ground / is there a wall" from
// two independent tile systems: GridMap (the ADT heightfield + liquid + area,
// under maps/) and the VMAP manager (WMO/M2 static collision, under vmaps/),
// reconciled by hand and gated by a "use vmaps" config switch. This class
// replaces both with a single tile that already fuses them: the ADT
// heightfield/liquid/area AND the static WMO/M2 collision come from one
// .tile file, so there is nothing to reconcile and the switch is moot.
//
// The heavy lifting -- reading the client MPQs and BUILDING the acceleration
// structures (a binned-SAH BVH over each model's collidable faces; Blizzard's authored
// MOBN/MOBR BSP was measured and retired, see Accelerators.hpp) -- is
// done offline by the baker; here we only mmap-cheap read the baked tile and
// query it. The query math (highest floor via a downward ray into each model's own
// space, liquid surface, segment blocking) is the world::terrain algorithm

#include "terrain/Terrain.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Terrain query result types / sentinels; they are the vocabulary the server
// speaks to the terrain, so they live with the terrain class.
// Plain types only -- no ACE, no DBC.
// ---------------------------------------------------------------------------

enum GridMapLiquidStatus
{
    LIQUID_MAP_NO_WATER = 0x00000000,
    LIQUID_MAP_ABOVE_WATER = 0x00000001,
    LIQUID_MAP_WATER_WALK = 0x00000002,
    LIQUID_MAP_IN_WATER = 0x00000004,
    LIQUID_MAP_UNDER_WATER = 0x00000008
};

// defined in DBC and left shifted for flag usage
#define MAP_LIQUID_TYPE_NO_WATER 0x00
#define MAP_LIQUID_TYPE_MAGMA 0x01
#define MAP_LIQUID_TYPE_OCEAN 0x02
#define MAP_LIQUID_TYPE_SLIME 0x04
#define MAP_LIQUID_TYPE_WATER 0x08

#define MAP_ALL_LIQUIDS (MAP_LIQUID_TYPE_WATER | MAP_LIQUID_TYPE_MAGMA | MAP_LIQUID_TYPE_OCEAN | MAP_LIQUID_TYPE_SLIME)

#define MAP_LIQUID_TYPE_DARK_WATER 0x10
#define MAP_LIQUID_TYPE_WMO_WATER 0x20

struct GridMapLiquidData
{
    uint32_t type_flags;
    uint32_t entry;
    float level;
    float depth_level;
};

#define MAX_HEIGHT 100000.0f            // can be use for find ground height at surface
#define INVALID_HEIGHT -100000.0f       // for check, must be equal to VMAP_INVALID_HEIGHT
#define INVALID_HEIGHT_VALUE -200000.0f // for return, check value for unknown height is VMAP_INVALID_HEIGHT
#define MAX_FALL_DISTANCE 250000.0f     // "unlimited fall" larger than MAX_HEIGHT - INVALID_HEIGHT
#define DEFAULT_HEIGHT_SEARCH 10.0f     // default search distance to find height at nearby locations
#define DEFAULT_WATER_SEARCH 50.0f      // default search distance for water level detection

// One map's fused terrain+collision, and the sole object the server queries for
// "how high is the ground / is there water / a wall / what area is this". It owns a
// per-map cache of baked .tile files and answers every query off Blizzard's authored
// acceleration structures. The old GridMap(.map)+VMAP(.vmtile) split, the "use vmaps"
// switch, and the TerrainInfo wrapper are all gone: this is the one class.
//
// Two method families share the class:
//   * engine queries (GetHeight/GetLiquid/GetAreaId/GetAreaInfo/IsInLineOfSight/...)
//     are self-contained -- defined in FusedTerrain.cpp, ACE/DBC-free, and compiled
//     into the offline probe tools as well;
//   * server queries (GetHeightStatic/getLiquidStatus/GetAreaFlag/GetZoneId/...) map
//     those onto MaNGOS's DBC world -- defined in GridMap.cpp, game-only. The tools
//     never reference them, so they never need MaNGOS/ACE symbols.
class FusedTerrain
{
public:
    // 64x64 ADT grid per map (matches MAX_NUMBER_OF_GRIDS).
    static const int GRID_COUNT = 64;

    explicit FusedTerrain(uint32_t mapId);

    FusedTerrain(const FusedTerrain &) = delete;
    FusedTerrain &operator=(const FusedTerrain &) = delete;

    uint32_t GetMapId() const { return m_mapId; }

    // Reference counting: the per-map registry (TerrainManager) keeps one instance
    // shared across every Map of that id and frees it when the last Map drops its
    // reference. std::atomic replaces the old ACE_Atomic_Op Referencable.
    void AddRef() const { ++m_refCount; }
    bool Release() const { return (--m_refCount < 1); }
    bool IsReferenced() const { return m_refCount > 0; }

    // True if a baked tile file exists for (map,gx,gy). Used by the startup data
    // check; grid indices are the MaNGOS grid coords (== tile tx,ty).
    static bool HasTile(uint32_t mapId, int gx, int gy);

    // Directory the baked .tile files live in (e.g. "<DataDir>/tiles"). Set once
    // at startup before any map loads. Files are named t_<map>_<tx>_<ty>.tile,
    // with a per-map global-WMO tile w_<map>.tile (instance maps built from one
    // giant WMO, which carry no ADT grid).
    static void SetTileDir(const std::string &dir) { s_tileDir = dir; }
    static const std::string &GetTileDir() { return s_tileDir; }

    // Highest floor at (x,y) at or just below z, considering terrain (unless the
    // spot is a hole) AND every static model under the column. searchUp starts
    // the probe a little above z; maxDrop bounds how far down a static floor may
    // be. Returns true and writes outZ on success; false if nothing was found
    // (off-map, ocean tile with no data, a hole with no static below).
    bool GetHeight(float x, float y, float z, float &outZ,
                   float searchUp = 2.0f, float maxDrop = 200.0f) const;

    // Liquid surface + kind at (x,y) near reference height z (ADT liquid and WMO
    // interior MLIQ), or false if there is no liquid there. z is the reference the
    // WMO transform needs -- pass the querying entity's Z.
    bool GetLiquid(float x, float y, float z,
                   world::terrain::LiquidInfo &out) const;

    // True if the segment a->b is unobstructed by static geometry (WMO walls, M2
    // doodads). The walkable heightfield never blocks LoS. Mirrors the old
    // IVMapManager::isInLineOfSight contract.
    //
    // The segment is taken VERBATIM. Any agent-height lift belongs to the caller and
    // is already applied there (WorldObject::IsWithinLOS raises both ends by 2 yards);
    // do not add one here, and do not add one to DynamicCollision either -- the two
    // must see the same ray or a door and its doorway get tested at different heights.
    bool IsInLineOfSight(float x1, float y1, float z1,
                         float x2, float y2, float z2) const;

    // Nearest static hit along a->b, as a fraction of the segment; > 1 when nothing
    // blocks. The fraction, not a hit POINT, is the primitive on purpose: the static
    // and dynamic worlds are queried over the same segment and the nearer fraction
    // wins, so only the winner is turned into a point (see Map::GetHitPosition).
    float NearestHitFraction(float x1, float y1, float z1,
                             float x2, float y2, float z2) const;

    // AreaTable.dbc id of the MCNK chunk under (x,y), or 0 when unknown.
    uint16_t GetAreaId(float x, float y) const;

    // WMO area identity of the group you're standing in under (x,y) near z: fills the
    // WMOAreaTable triple (rootId, adtId, groupId) + the group's MOGP flags, and snaps
    // groundZ to that group's floor. Returns false when no WMO covers the point.
    bool GetAreaInfo(float x, float y, float z, uint32_t &mogpFlags, int32_t &adtId,
                     int32_t &rootId, int32_t &groupId, float &groundZ) const;

    // True if (x,y,z) is outdoors: no WMO overhead, or the WMO group's MOGP flags mark
    // it outdoor (bit 0x8000; on Outland/map 530 also 0x8).
    bool IsOutdoors(float x, float y, float z) const;

    // -----------------------------------------------------------------------
    // Server-facing API (the old TerrainInfo surface). Definitions live in
    // GridMap.cpp; they translate the engine answers above into MaNGOS's DBC
    // world (AreaTable/WMOAreaTable/LiquidType). checkVMap/maxSearchDist are kept
    // for source compatibility but no longer branch anything -- the fused tile is
    // always authoritative, so the old "use vmaps" switch is a no-op.
    // -----------------------------------------------------------------------

    float GetHeightStatic(float x, float y, float z, bool checkVMap = true,
                          float maxSearchDist = DEFAULT_HEIGHT_SEARCH) const;
    float GetWaterLevel(float x, float y, float z, float *pGround = nullptr) const;
    float GetWaterOrGroundLevel(float x, float y, float z, float *pGround = nullptr,
                                bool swim = false) const;
    bool IsInWater(float x, float y, float z, GridMapLiquidData *data = nullptr) const;
    bool IsUnderWater(float x, float y, float z) const;

    GridMapLiquidStatus getLiquidStatus(float x, float y, float z, uint8_t ReqLiquidType,
                                        GridMapLiquidData *data = nullptr) const;

    uint16_t GetAreaFlag(float x, float y, float z, bool *isOutdoors = nullptr) const;
    uint8_t GetTerrainType(float x, float y) const;

    uint32_t GetAreaId(float x, float y, float z) const;
    uint32_t GetZoneId(float x, float y, float z) const;
    void GetZoneAndAreaId(uint32_t &zoneid, uint32_t &areaid, float x, float y, float z) const;

    // WMO area triple without the groundZ out-param (the old TerrainInfo signature).
    bool GetAreaInfo(float x, float y, float z, uint32_t &mogpflags, int32_t &adtId,
                     int32_t &rootId, int32_t &groupId) const;

    // -----------------------------------------------------------------------
    // Per-grid lifecycle. FusedTerrain caches its own .tile data lazily, so a
    // grid "load" pins the navmesh (MMAP) tile for that cell and marks the cell's
    // terrain tile as in-use; the count tracks how many active grids reference a
    // cell so the last one unloads the navmesh and unpins the tile.
    // Called by Map as grids activate/deactivate. Definitions in GridMap.cpp.
    // -----------------------------------------------------------------------
    bool LoadGrid(int gx, int gy);
    void UnloadGrid(int gx, int gy);

    // Periodic tick, driven by TerrainManager::Update (World update thread).
    // Advances the tile-cache clock and, once a minute, sweeps the cache: a tile
    // that no active grid pins and that no query has touched for a while is
    // dropped. Without this the cache is monotonic -- a player walking a continent
    // leaves every WMO he passed resident for the life of the map -- because grid
    // unload only ever released the navmesh, never the .tile.
    void Update(uint32_t diff);

private:
    using Tile = world::terrain::TerrainTile;
    using TilePtr = std::shared_ptr<const Tile>;

    // Resident tile covering world (x,y), loading + caching it on first touch.
    // nullptr when the map has no such tile (ocean / off-map). Thread-safe.
    TilePtr TileAt(float x, float y) const;
    // The map's global-WMO tile, if any (loaded once, cached).
    TilePtr GlobalWmo() const;
    TilePtr LoadCell(int tx, int ty) const;

    // Gather the static instances whose footprint overlaps the segment a->b's XY
    // bbox (walking the tiles the segment crosses), keeping their tiles alive in
    // keepAlive for the duration of the query (out holds raw instance pointers).
    void CollectSegmentInstances_(world::terrain::Vec3 a, world::terrain::Vec3 b,
                                  std::vector<const world::terrain::StaticInstance *> &out,
                                  std::vector<TilePtr> &keepAlive) const;

    // Drop the cached tile at (tx,ty). Caller holds m_mutex exclusively. In-flight
    // queries are unaffected: TileAt hands out a shared_ptr, so a tile stays alive
    // as long as somebody is reading it.
    void EvictTile_(int tx, int ty) const;

    // getLiquidStatus with the column's floor Z already known. The public entry point
    // derives it; GetWaterLevel/GetWaterOrGroundLevel already hold it, and used to pay
    // for a second full-depth floor raycast to have it re-derived behind their backs.
    // Definition in GridMap.cpp, beside the public wrapper.
    GridMapLiquidStatus LiquidStatusAtGround_(float x, float y, float z, uint8_t ReqLiquidType,
                                              float groundZ, GridMapLiquidData *data) const;

    const uint32_t m_mapId;

    // The tile cache. Every query goes through it, from every map-update thread, so
    // the hit path takes m_mutex in SHARED mode: readers no longer serialise against
    // each other. This matters because TerrainManager hands the SAME FusedTerrain to
    // every Map with this id -- all instances of a dungeon share one cache, and used
    // to contend on one exclusive lock for every height/liquid/LoS query.
    //
    // m_loaded: 0 = not yet probed, 1 = probed (m_tiles[i] holds the result, possibly
    // null). A null-but-probed entry is a memo that the map has no such tile (ocean /
    // off-map); it is one byte and spares a failed file open per query, so the sweep
    // below keeps it and only ever reclaims real tile data.
    mutable std::array<std::array<TilePtr, GRID_COUNT>, GRID_COUNT> m_tiles;
    mutable std::array<std::array<uint8_t, GRID_COUNT>, GRID_COUNT> m_loaded{};
    mutable TilePtr m_globalWmo;
    mutable uint8_t m_globalWmoProbed = 0;
    mutable std::shared_mutex m_mutex;

    // Tile-cache ageing. m_clockMs is a monotonic ms counter advanced by Update();
    // each cache hit stamps the tile with it, and the sweep evicts unpinned tiles
    // nothing has touched since. Atomic because the map-update threads read the clock
    // and stamp tiles while holding only the SHARED lock, concurrently with each other
    // and with the world thread advancing the clock.
    mutable std::array<std::array<std::atomic<uint32_t>, GRID_COUNT>, GRID_COUNT> m_tileLastUse{};
    std::atomic<uint32_t> m_clockMs{0};
    uint32_t m_sweepAccumMs = 0;    // Update() only (single world thread)

    // Registry reference count (Map instances sharing this map's terrain).
    mutable std::atomic<long> m_refCount{0};

    // Per-cell active-grid reference count. Guards MMAP tile load/unload, and pins the
    // cell's terrain tile against the sweep. Indexed exactly like m_tiles: Map passes
    // the flipped grid coords (63 - gridX), which are the tile's (tx,ty).
    std::array<std::array<int16_t, GRID_COUNT>, GRID_COUNT> m_gridRef{};
    std::mutex m_gridRefMutex;

    static std::string s_tileDir;
};

#endif // MANGOS_H_FUSEDTERRAIN
