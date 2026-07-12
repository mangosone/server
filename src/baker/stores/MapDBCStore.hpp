/**
 * MaNGOS is a full featured server for World of Warcraft, supporting
 * the following clients: 1.12.x, 2.4.3, 3.3.5a, 4.3.4a and 5.4.8
 *
 * Copyright (C) 2005-2026 MaNGOS <https://www.getmangos.eu>
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
 */

#pragma once

// Map.dbc -> { mapId : internal directory name }. The directory is what the ADT/WDT
// paths are built from (World\Maps\<Directory>\...). Map.dbc column 1 is that string
// in 2.4.3. Consumed by MpqTileSource (which map am I loading) and by the baker's
// map enumeration.
//
// Parsed with MaNGOS's DBCFileLoader and MaNGOS's MapEntryfmt -- see MpqDbcLoader.hpp
// for why the server's 'x' on the Directory column does not stop us reading it.

#include "stores/MpqDbcLoader.hpp"

#include "Server/DBCfmt.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace world
{
    struct MapInfo
    {
        std::string directory; ///< internal name, e.g. "Azeroth", "Kalimdor"
    };

    class MapStore
    {
    public:
        std::unordered_map<uint32_t, MapInfo> mapsRegistry;

        std::string getDirectory(uint32_t mapId) const
        {
            auto it = mapsRegistry.find(mapId);
            return it != mapsRegistry.end() ? it->second.directory : std::string();
        }
    };

    // Reads DBFilesClient\Map.dbc from the archive. Returns nullptr if it can't be
    // read/parsed (the caller aborts the tile bake in that case).
    inline std::unique_ptr<MapStore> loadMapStore(world::terrain::IMpqArchive& archive)
    {
        DBCFileLoader dbc;
        if (!loadDbcFromMpq(archive, "DBFilesClient\\Map.dbc", MapEntryfmt, dbc))
        {
            return nullptr;
        }

        auto store = std::make_unique<MapStore>();
        for (uint32_t r = 0; r < dbc.GetNumRows(); ++r)
        {
            DBCFileLoader::Record rec = dbc.getRecord(r);
            const uint32_t id = rec.getUInt(0);            // col 0: ID
            const char* dir = rec.getString(1);            // col 1: Directory
            store->mapsRegistry[id] = MapInfo{dir ? dir : ""};
        }
        return store;
    }
} // namespace world
