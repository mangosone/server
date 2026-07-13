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

#ifndef MANGOS_GRIDMAP_H
#define MANGOS_GRIDMAP_H

// The ADT-heightfield loader (GridMap) and the per-map query wrapper (TerrainInfo)
// that used to live here are gone: static terrain, liquid, area and the WMO/M2
// collision are all fused into one .tile served by FusedTerrain (see FusedTerrain.h).
// What remains here is TerrainManager -- the per-map registry that hands out one
// shared FusedTerrain per map id -- plus the AreaTable.dbc flag<->id helpers.

#include "Platform/Define.h"
#include "Policies/Singleton.h"
#include "Utilities/UnorderedMapSet.h"
#include "FusedTerrain.h"
#include <mutex>
// class for sharing and managing FusedTerrain objects, one per map id
class TerrainManager : public MaNGOS::Singleton<TerrainManager>
{
        typedef UNORDERED_MAP<uint32, FusedTerrain*> TerrainDataMap;
        friend class MaNGOS::Singleton<TerrainManager>;

    public:
        FusedTerrain* LoadTerrain(const uint32 mapId);
        void UnloadTerrain(const uint32 mapId);

        void Update(const uint32 diff);
        void UnloadAll();

        uint16 GetAreaFlag(uint32 mapid, float x, float y, float z) const
        {
            FusedTerrain* pData = const_cast<TerrainManager*>(this)->LoadTerrain(mapid);
            return pData->GetAreaFlag(x, y, z);
        }
        uint32 GetAreaId(uint32 mapid, float x, float y, float z) const
        {
            return TerrainManager::GetAreaIdByAreaFlag(GetAreaFlag(mapid, x, y, z), mapid);
        }
        uint32 GetZoneId(uint32 mapid, float x, float y, float z) const
        {
            return TerrainManager::GetZoneIdByAreaFlag(GetAreaFlag(mapid, x, y, z), mapid);
        }
        void GetZoneAndAreaId(uint32& zoneid, uint32& areaid, uint32 mapid, float x, float y, float z)
        {
            TerrainManager::GetZoneAndAreaIdByAreaFlag(zoneid, areaid, GetAreaFlag(mapid, x, y, z), mapid);
        }

        static uint32 GetAreaIdByAreaFlag(uint16 areaflag, uint32 map_id);
        static uint32 GetZoneIdByAreaFlag(uint16 areaflag, uint32 map_id);
        static void GetZoneAndAreaIdByAreaFlag(uint32& zoneid, uint32& areaid, uint16 areaflag, uint32 map_id);

    private:
        TerrainManager();
        ~TerrainManager();

        TerrainManager(const TerrainManager&);
        TerrainManager& operator=(const TerrainManager&);

        typedef std::mutex LOCK_TYPE;
        LOCK_TYPE m_mutex;
        TerrainDataMap i_TerrainMap;
};

#define sTerrainMgr TerrainManager::Instance()

#endif
