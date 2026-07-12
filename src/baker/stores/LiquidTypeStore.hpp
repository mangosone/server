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

// LiquidType.dbc -> { id : (category, spellId) }.
//
// Needed only for WMO interior liquid, and then only for the minority of WMOs whose root
// MOHD carries flag 0x4 ("group liquid is a real LiquidType.dbc id"). Everywhere else the
// liquid id is one of the canonical 1..4 and its category follows from (id - 1) & 3.
// ADT terrain never needs this file at all: in the MCLQ era (<= 2.4.3) the MCNK chunk
// flags *are* the type. The dbc first becomes load-bearing for terrain with MH2O, WotLK+.
//
// 2.4.3 layout is `LiquidTypefmt` = "nsii": id, name, Type, SpellID. Mind the Type
// encoding -- it is NOT the 0/1/2/3 water/ocean/magma/slime tuple that WotLK's much wider
// LiquidType.dbc uses. In 2.4.3 it is:
//
//     0 = magma, 2 = slime, 3 = water        (1 is unused; ocean is not distinguishable)
//
// which is exactly why the reference vmap-extractor classifies ids below 21 with
// (id - 1) & 3 instead of consulting this column: only the id can tell ocean from water.

#include "stores/MpqDbcLoader.hpp"

#include "Server/DBCfmt.h"

#include <cstdint>
#include <unordered_map>

namespace world
{
    /// 2.4.3 LiquidType.dbc `Type` column values.
    enum class LiquidDbcType : uint32_t
    {
        Magma = 0,
        Slime = 2,
        Water = 3
    };

    struct LiquidTypeInfo
    {
        uint32_t type = 0;      ///< the `Type` column; see LiquidDbcType
        uint32_t spellId = 0;   ///< aura applied while in this liquid (SSC water, ...)
    };

    class LiquidTypeStore
    {
    public:
        // Reads DBFilesClient\LiquidType.dbc. Returns false if unreadable.
        bool loadFromDbc(world::terrain::IMpqArchive& archive)
        {
            DBCFileLoader dbc;
            if (!loadDbcFromMpq(archive, "DBFilesClient\\LiquidType.dbc", LiquidTypefmt, dbc))
            {
                return false;
            }

            m_entries.clear();
            for (uint32_t r = 0; r < dbc.GetNumRows(); ++r)
            {
                DBCFileLoader::Record rec = dbc.getRecord(r);
                const uint32_t id = rec.getUInt(0);     // col 0: ID
                LiquidTypeInfo info;
                info.type = rec.getUInt(2);             // col 2: Type
                info.spellId = rec.getUInt(3);          // col 3: SpellID
                m_entries[id] = info;
            }
            return true;
        }

        /// nullptr when the id is not in the dbc.
        const LiquidTypeInfo* find(uint32_t id) const
        {
            auto it = m_entries.find(id);
            return it != m_entries.end() ? &it->second : nullptr;
        }

        size_t size() const { return m_entries.size(); }

    private:
        std::unordered_map<uint32_t, LiquidTypeInfo> m_entries;
    };
} // namespace world
