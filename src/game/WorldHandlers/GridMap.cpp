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

/**
 * @file GridMap.cpp
 * @brief Server-facing terrain queries + the per-map FusedTerrain registry.
 *
 * The old ADT (.map) heightfield loader and the TerrainInfo query wrapper are
 * retired: static terrain, liquid, area and WMO/M2 collision are fused into one
 * .tile served by FusedTerrain (engine side in FusedTerrain.cpp). This file is the
 * game-only half of FusedTerrain -- the methods that translate its raw answers into
 * MaNGOS's DBC world (AreaTable / WMOAreaTable) -- plus TerrainManager, the registry
 * that shares one FusedTerrain per map id, and the per-grid navmesh (MMAP) lifecycle.
 */

#include "Log.h"
#include "DBCEnums.h"
#include "DBCStores.h"
#include "GridMap.h"
#include "MoveMap.h"
#include "World.h"
#include "Policies/Singleton.h"

using world::terrain::LiquidInfo;
using world::terrain::LiquidKind;

/**
 * @brief Checks whether a WMO group is flagged as outdoors.
 */
static inline bool IsOutdoorWMO(uint32 mogpFlags, uint32 mapId)
{
    // in flyable areas mounting up is also allowed if 0x0008 flag is set
    if (mapId == 530)
    {
        return mogpFlags & 0x8008;
    }

    return mogpFlags & 0x8000;
}

//////////////////////////////////////////////////////////////////////////
// FusedTerrain -- server-facing queries (engine answers -> MaNGOS DBC world)
//////////////////////////////////////////////////////////////////////////

/**
 * @brief Liquid interaction status at a world position.
 *
 * Fold of the old GridMap + VMAP liquid paths into the one fused query. The tile
 * carries both the liquid *kind* (water/ocean/magma/slime), from which type_flags
 * are reconstructed, and the LiquidType.dbc row that names it -- the latter only
 * matters for the liquid's aura spell (see Player::HandleDrowning).
 *
 * Still not carried: the dark-water bit, and the Hyjal/Coilfang area overrides.
 */
GridMapLiquidStatus FusedTerrain::getLiquidStatus(float x, float y, float z, uint8 ReqLiquidType,
                                                  GridMapLiquidData* data) const
{
    return LiquidStatusAtGround_(x, y, z, ReqLiquidType,
                                 GetHeightStatic(x, y, z, true, DEFAULT_WATER_SEARCH), data);
}

/**
 * @brief getLiquidStatus for a caller that already knows the column's floor Z.
 *
 * The floor is what decides whether the liquid we found is real (a surface below the
 * ground is not water you can swim in) and it is what fills depth_level -- but deriving
 * it means a full-depth column raycast, and the water-level callers below have already
 * paid for exactly that. Passing it in halves their cost: GetWaterLevel and
 * GetWaterOrGroundLevel used to call GetHeightStatic themselves and then have
 * getLiquidStatus call it a second time, behind their backs, to rediscover the very
 * floor they had just handed it as the query Z.
 */
GridMapLiquidStatus FusedTerrain::LiquidStatusAtGround_(float x, float y, float z,
                                                        uint8 ReqLiquidType, float ground_level,
                                                        GridMapLiquidData* data) const
{
    LiquidInfo liq;
    if (!GetLiquid(x, y, z, liq))
    {
        return LIQUID_MAP_NO_WATER;
    }

    uint32 typeFlag = 0;
    switch (liq.kind)
    {
        case LiquidKind::Water: typeFlag = MAP_LIQUID_TYPE_WATER; break;
        case LiquidKind::Ocean: typeFlag = MAP_LIQUID_TYPE_OCEAN; break;
        case LiquidKind::Magma: typeFlag = MAP_LIQUID_TYPE_MAGMA; break;
        case LiquidKind::Slime: typeFlag = MAP_LIQUID_TYPE_SLIME; break;
        default: return LIQUID_MAP_NO_WATER;
    }

    // Check requested liquid type mask
    if (ReqLiquidType && !(ReqLiquidType & typeFlag))
    {
        return LIQUID_MAP_NO_WATER;
    }

    float liquid_level = liq.level;

    // Check water level and ground level
    if (liquid_level <= ground_level || z < ground_level - 2)
    {
        return LIQUID_MAP_NO_WATER;
    }

    if (data)
    {
        data->level = liquid_level;
        data->depth_level = ground_level;
        data->entry = liq.entry;    // LiquidType.dbc row, resolved by the baker
        data->type_flags = typeFlag;
    }

    // For speed check as int values
    int delta = int((liquid_level - z) * 10);

    if (delta > 20)                                          // Under water
    {
        return LIQUID_MAP_UNDER_WATER;
    }
    if (delta > 0)                                           // In water
    {
        return LIQUID_MAP_IN_WATER;
    }
    if (delta > -1)                                          // Walk on water
    {
        return LIQUID_MAP_WATER_WALK;
    }
    return LIQUID_MAP_ABOVE_WATER;
}

/**
 * @brief True when liquid is present in the column at the position.
 */
bool FusedTerrain::IsInWater(float x, float y, float z, GridMapLiquidData* data) const
{
    GridMapLiquidData liquid_status;
    GridMapLiquidData* liquid_ptr = data ? data : &liquid_status;
    return getLiquidStatus(x, y, z, MAP_ALL_LIQUIDS, liquid_ptr) != LIQUID_MAP_NO_WATER;
}

/**
 * @brief True when the position is fully submerged in water/ocean.
 */
bool FusedTerrain::IsUnderWater(float x, float y, float z) const
{
    return (getLiquidStatus(x, y, z, MAP_LIQUID_TYPE_WATER | MAP_LIQUID_TYPE_OCEAN)
            & LIQUID_MAP_UNDER_WATER) != 0;
}

/**
 * @brief Liquid surface level at a position, or an invalid marker when dry.
 */
float FusedTerrain::GetWaterLevel(float x, float y, float z, float* pGround /*= nullptr*/) const
{
    float ground_z = GetHeightStatic(x, y, z, true, DEFAULT_WATER_SEARCH);
    if (pGround)
    {
        *pGround = ground_z;
    }

    // Hand the floor we just found straight to the liquid check: probing from ground_z
    // would only rediscover ground_z, at the cost of another full column raycast.
    GridMapLiquidData liquid_status;
    if (!LiquidStatusAtGround_(x, y, ground_z, MAP_ALL_LIQUIDS, ground_z, &liquid_status))
    {
        return INVALID_HEIGHT_VALUE;
    }
    return liquid_status.level;
}

/**
 * Function find higher of water or ground for the current floor.
 *
 * @param swim  when true, in-water returns a level 2yd below the surface so the
 *              client does not treat a swimming unit as standing on the water.
 */
float FusedTerrain::GetWaterOrGroundLevel(float x, float y, float z, float* pGround /*= nullptr*/,
                                          bool swim /*= false*/) const
{
    float ground_z = GetHeightStatic(x, y, z, true, DEFAULT_WATER_SEARCH);
    if (pGround)
    {
        *pGround = ground_z;
    }

    GridMapLiquidData liquid_status;
    GridMapLiquidStatus res =
        LiquidStatusAtGround_(x, y, ground_z, MAP_ALL_LIQUIDS, ground_z, &liquid_status);
    return res ? (swim ? liquid_status.level - 2.0f : liquid_status.level) : ground_z;
}

/**
 * @brief WMO area triple at a position (the old TerrainInfo 4-out signature).
 */
bool FusedTerrain::GetAreaInfo(float x, float y, float z, uint32& flags, int32& adtId,
                               int32& rootId, int32& groupId) const
{
    float groundZ;
    return GetAreaInfo(x, y, z, flags, adtId, rootId, groupId, groundZ);
}

/**
 * @brief Resolved area explore flag (AreaTable AreaBit) for a position.
 *
 * WMO group -> WMOAreaTable -> AreaTable when a WMO covers the point; otherwise the
 * ADT MCNK's AreaTable id (converted to its explore AreaBit), falling back to the
 * map default. Also reports indoor/outdoor from the WMO group flags.
 */
uint16 FusedTerrain::GetAreaFlag(float x, float y, float z, bool* isOutdoors) const
{
    uint32 mogpFlags;
    int32 adtId, rootId, groupId;
    WMOAreaTableEntry const* wmoEntry = 0;
    AreaTableEntry const* atEntry = 0;
    bool haveAreaInfo = false;

    if (GetAreaInfo(x, y, z, mogpFlags, adtId, rootId, groupId))
    {
        haveAreaInfo = true;
        wmoEntry = GetWMOAreaTableEntryByTripple(rootId, adtId, groupId);
        if (wmoEntry)
        {
            atEntry = GetAreaEntryByAreaID(wmoEntry->areaId);
        }
    }

    uint16 areaflag;
    if (atEntry)
    {
        areaflag = atEntry->AreaBit;
    }
    else
    {
        // ADT MCNK area id (a real AreaTable.dbc id) -> its explore AreaBit.
        int32 flag = GetAreaFlagByAreaID(GetAreaId(x, y));
        areaflag = (flag >= 0) ? uint16(flag) : GetAreaFlagByMapId(GetMapId());
    }

    if (isOutdoors)
    {
        *isOutdoors = haveAreaInfo ? IsOutdoorWMO(mogpFlags, GetMapId()) : true;
    }
    return areaflag;
}

/**
 * @brief Liquid category flags at a position, independent of height (rarely used).
 */
uint8 FusedTerrain::GetTerrainType(float x, float y) const
{
    // ADT surface liquid is height-independent, so a nominal z suffices to classify
    // the column's liquid category. (This mirror of the old GridMap::getTerrainType
    // has no live callers; kept for API completeness.)
    LiquidInfo liq;
    if (!GetLiquid(x, y, 0.0f, liq))
    {
        return 0;
    }
    switch (liq.kind)
    {
        case LiquidKind::Water: return MAP_LIQUID_TYPE_WATER;
        case LiquidKind::Ocean: return MAP_LIQUID_TYPE_OCEAN;
        case LiquidKind::Magma: return MAP_LIQUID_TYPE_MAGMA;
        case LiquidKind::Slime: return MAP_LIQUID_TYPE_SLIME;
        default: return 0;
    }
}

/**
 * @brief Area id at a position (AreaTable.dbc id).
 */
uint32 FusedTerrain::GetAreaId(float x, float y, float z) const
{
    return TerrainManager::GetAreaIdByAreaFlag(GetAreaFlag(x, y, z), m_mapId);
}

/**
 * @brief Zone id at a position (parent area, or the area itself).
 */
uint32 FusedTerrain::GetZoneId(float x, float y, float z) const
{
    return TerrainManager::GetZoneIdByAreaFlag(GetAreaFlag(x, y, z), m_mapId);
}

/**
 * @brief Both zone id and area id for a position.
 */
void FusedTerrain::GetZoneAndAreaId(uint32& zoneid, uint32& areaid, float x, float y, float z) const
{
    TerrainManager::GetZoneAndAreaIdByAreaFlag(zoneid, areaid, GetAreaFlag(x, y, z), m_mapId);
}

//////////////////////////////////////////////////////////////////////////
// FusedTerrain -- per-grid navmesh (MMAP) lifecycle
//////////////////////////////////////////////////////////////////////////

/**
 * @brief Takes a reference on a grid cell: loads its navmesh tile on the first one,
 *        and pins its terrain tile against the cache sweep for as long as it is held.
 */
bool FusedTerrain::LoadGrid(int gx, int gy)
{
    std::lock_guard<std::mutex> lk(m_gridRefMutex);
    if (m_gridRef[gx][gy]++ == 0)
    {
        MMAP::MMapFactory::createOrGetMMapManager()->loadMap(m_mapId, gx, gy);
    }
    return true;
}

/**
 * @brief Drops a grid cell reference; at the last one unloads the navmesh tile and
 *        unpins the terrain tile, which the sweep may then reclaim once it goes idle.
 */
void FusedTerrain::UnloadGrid(int gx, int gy)
{
    std::lock_guard<std::mutex> lk(m_gridRefMutex);
    if (m_gridRef[gx][gy] > 0 && --m_gridRef[gx][gy] == 0)
    {
        MMAP::MMapFactory::createOrGetMMapManager()->unloadMap(m_mapId, gx, gy);
    }
}

//////////////////////////////////////////////////////////////////////////
// TerrainManager -- one shared FusedTerrain per map id
//////////////////////////////////////////////////////////////////////////

#define CLASS_LOCK MaNGOS::ClassLevelLockable<TerrainManager, ACE_Thread_Mutex>
INSTANTIATE_SINGLETON_2(TerrainManager, CLASS_LOCK);
INSTANTIATE_CLASS_MUTEX(TerrainManager, ACE_Thread_Mutex);

TerrainManager::TerrainManager() : m_mutex()
{
}

TerrainManager::~TerrainManager()
{
    for (TerrainDataMap::iterator it = i_TerrainMap.begin(); it != i_TerrainMap.end(); ++it)
    {
        delete it->second;
    }
}

/**
 * @brief Loads or returns the shared terrain for a map.
 */
FusedTerrain* TerrainManager::LoadTerrain(const uint32 mapId)
{
    ACE_GUARD_RETURN(LOCK_TYPE, _guard, m_mutex, NULL)

    TerrainDataMap::const_iterator iter = i_TerrainMap.find(mapId);
    if (iter == i_TerrainMap.end())
    {
        FusedTerrain* ti = new FusedTerrain(mapId);
        i_TerrainMap[mapId] = ti;
        return ti;
    }

    return (*iter).second;
}

/**
 * @brief Frees a map's terrain (and its navmesh) once no Map references it.
 */
void TerrainManager::UnloadTerrain(const uint32 mapId)
{
    if (sWorld.getConfig(CONFIG_BOOL_GRID_UNLOAD) == 0)
    {
        return;
    }

    ACE_GUARD(LOCK_TYPE, _guard, m_mutex)

    TerrainDataMap::iterator iter = i_TerrainMap.find(mapId);
    if (iter != i_TerrainMap.end())
    {
        FusedTerrain* ptr = (*iter).second;
        if (ptr->IsReferenced() == false)
        {
            i_TerrainMap.erase(iter);
            MMAP::MMapFactory::createOrGetMMapManager()->unloadMap(mapId);
            delete ptr;
        }
    }
}

/**
 * @brief Periodic tick: ages each map's tile cache and lets it reclaim what it can.
 *
 * A FusedTerrain caches .tile data lazily and, until now, forever -- grid unload only
 * released the navmesh. Ticking it lets the sweep drop tiles no active grid pins and
 * nothing has queried lately (see FusedTerrain::Update).
 */
void TerrainManager::Update(const uint32 diff)
{
    ACE_GUARD(LOCK_TYPE, _guard, m_mutex)

    for (TerrainDataMap::iterator it = i_TerrainMap.begin(); it != i_TerrainMap.end(); ++it)
    {
        it->second->Update(diff);
    }
}

/**
 * @brief Unloads all cached terrain and navmeshes.
 */
void TerrainManager::UnloadAll()
{
    for (TerrainDataMap::iterator it = i_TerrainMap.begin(); it != i_TerrainMap.end(); ++it)
    {
        MMAP::MMapFactory::createOrGetMMapManager()->unloadMap(it->first);
        delete it->second;
    }

    i_TerrainMap.clear();
}

/**
 * @brief Resolves an area id from an explore flag and map id.
 */
uint32 TerrainManager::GetAreaIdByAreaFlag(uint16 areaflag, uint32 map_id)
{
    AreaTableEntry const* entry = GetAreaEntryByAreaFlagAndMap(areaflag, map_id);

    if (entry)
    {
        return entry->ID;
    }
    return 0;
}

/**
 * @brief Resolves a zone id from an explore flag and map id.
 */
uint32 TerrainManager::GetZoneIdByAreaFlag(uint16 areaflag, uint32 map_id)
{
    AreaTableEntry const* entry = GetAreaEntryByAreaFlagAndMap(areaflag, map_id);

    if (entry)
    {
        return (entry->ParentAreaID != 0) ? entry->ParentAreaID : entry->ID;
    }
    return 0;
}

/**
 * @brief Resolves both zone id and area id from an explore flag and map id.
 */
void TerrainManager::GetZoneAndAreaIdByAreaFlag(uint32& zoneid, uint32& areaid, uint16 areaflag, uint32 map_id)
{
    AreaTableEntry const* entry = GetAreaEntryByAreaFlagAndMap(areaflag, map_id);

    areaid = entry ? entry->ID : 0;
    zoneid = entry ? ((entry->ParentAreaID != 0) ? entry->ParentAreaID : entry->ID) : 0;
}
