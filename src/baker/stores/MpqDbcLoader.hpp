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

// Bridges the client MPQ to MaNGOS's own DBC reader. The baker carries no DBC parser of
// its own: it reads the raw WDBC image out of the archive and hands it to DBCFileLoader,
// the very class mangosd loads its DBCs with, using the very format strings from
// Server/DBCfmt.h. One parser, one column layout -- the baker cannot drift from the
// server's view of a .dbc.
//
// Note on format strings. The server marks columns it does not care about as 'x' (skip),
// and Map.dbc's Directory column is one of them -- yet the directory is precisely what the
// baker needs to build "World\Maps\<Directory>\..." paths. That is fine: 'x' and 's' are
// both 4 bytes wide, and neither Map.dbc nor GameObjectDisplayInfo.dbc contains a byte
// field ('b'/'X'), so every field offset DBCFileLoader computes is identical either way.
// We therefore reuse the server's unmodified format strings and read the string column
// straight off the record, rather than forking a second layout that could rot.

#include "terrain/IMpqArchive.hpp"

#include "DataStores/DBCFileLoader.h"

#include <cstdint>
#include <string>
#include <vector>

namespace world
{
    /// Reads `dbcPath` (e.g. "DBFilesClient\\Map.dbc") from `archive` and parses it into
    /// `out` with the given DBCfmt format string. False if absent or malformed.
    inline bool loadDbcFromMpq(world::terrain::IMpqArchive& archive,
                               const std::string& dbcPath,
                               const char* fmt,
                               DBCFileLoader& out)
    {
        std::vector<uint8_t> bytes;
        if (!archive.read(dbcPath, bytes))
        {
            return false;
        }
        return out.LoadFromMemory(bytes.data(), bytes.size(), fmt);
    }
} // namespace world
