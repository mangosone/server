/**
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
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
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 *
 * World of Warcraft, and all World of Warcraft or Warcraft art, images,
 * and lore are copyrighted by Blizzard Entertainment, Inc.
 */

#ifndef MANGOS_H_IDLIST
#define MANGOS_H_IDLIST

// Parsing a comma-separated id list out of a config value. This lived as a pair of
// statics on the vmap library's factory, which had nothing to do with either config
// or ids and was reached only because it happened to be linked.

#include "Platform/Define.h"

#include <cstdlib>
#include <string>
#include <vector>

namespace MaNGOS
{
    inline std::vector<uint32> ParseIdList(const std::string& text)
    {
        std::vector<uint32> ids;
        size_t pos = 0;
        while (pos <= text.size())
        {
            const size_t comma = text.find(',', pos);
            std::string token = text.substr(
                pos, comma == std::string::npos ? std::string::npos : comma - pos);

            const size_t first = token.find_first_not_of(" \t\r\n\"'");
            if (first != std::string::npos)
            {
                const size_t last = token.find_last_not_of(" \t\r\n\"'");
                token = token.substr(first, last - first + 1);
                // ZERO IS A MAP. Map 0 is Eastern Kingdoms, so `id > 0` silently dropped
                // it from the one setting whose entire purpose is naming maps to
                // force-load -- an administrator who wrote LoadAllGridsOnMaps = "0" got
                // no grids and no complaint.
                //
                // strtol returns 0 both for the token "0" and for a token that is not a
                // number at all, which is why the guard cannot simply widen to `id >= 0`.
                // The end pointer separates them: the conversion must have consumed
                // something and must have consumed ALL of it, so a malformed token is
                // still rejected rather than silently read as map 0.
                char* end = nullptr;
                const long id = std::strtol(token.c_str(), &end, 10);
                if (end != token.c_str() && *end == '\0' && id >= 0)
                {
                    ids.push_back(uint32(id));
                }
            }

            if (comma == std::string::npos)
            {
                break;
            }
            pos = comma + 1;
        }
        return ids;
    }
}

using MaNGOS::ParseIdList;

#endif
