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

// GameObjectDisplayInfo.dbc -> { displayId : model path }. The baker walks these to
// bake collidable game-object models (doors, lifts, transport decks) by displayId.
// Column 1 is the model path (a .mdx/.m2 or .wmo, backslash-separated) in 2.4.3.
//
// Parsed with MaNGOS's DBCFileLoader and MaNGOS's GameObjectDisplayInfofmt, so the
// column layout is the server's -- see MpqDbcLoader.hpp.

#include "stores/MpqDbcLoader.hpp"

#include "Server/DBCfmt.h"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace world
{
    struct GameObjectDisplayInfoEntry
    {
        std::string modelPath; ///< raw DBC path (backslashes), e.g. "World\wmo\transports\..."
    };

    class GameObjectDisplayInfoStore
    {
    public:
        // Reads DBFilesClient\GameObjectDisplayInfo.dbc. Returns false if unreadable.
        bool loadFromDbc(world::terrain::IMpqArchive& archive)
        {
            DBCFileLoader dbc;
            if (!loadDbcFromMpq(archive, "DBFilesClient\\GameObjectDisplayInfo.dbc",
                                GameObjectDisplayInfofmt, dbc))
            {
                return false;
            }

            m_entries.clear();
            for (uint32_t r = 0; r < dbc.GetNumRows(); ++r)
            {
                DBCFileLoader::Record rec = dbc.getRecord(r);
                const uint32_t id = rec.getUInt(0);         // col 0: ID
                const char* model = rec.getString(1);       // col 1: ModelName
                m_entries[id] = GameObjectDisplayInfoEntry{model ? model : ""};
            }
            return true;
        }

        const std::unordered_map<uint32_t, GameObjectDisplayInfoEntry>& entries() const
        {
            return m_entries;
        }

    private:
        std::unordered_map<uint32_t, GameObjectDisplayInfoEntry> m_entries;
    };
} // namespace world
