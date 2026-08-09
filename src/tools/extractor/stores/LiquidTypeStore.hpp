#pragma once

// LiquidType.dbc (2.4.3, fmt "nsii"): ID, Name, Type, SpellID -- fieldCount 4.
// Type: 0 magma, 2 slime, 3 water; ocean is not in Type (row id 2 / 15).
// WotLK uses wider rows and different Type numbers -- indices 3/5 assert here.

#include "MpqDbcLoader.hpp"
#include "terrain/Terrain.hpp"

#include "Server/DBCfmt.h"

#include <cstdint>
#include <unordered_map>

namespace world
{
    struct LiquidTypeInfo
    {
        uint32_t type = 0;
        uint32_t spellId = 0;
    };

    class LiquidTypeStore
    {
    public:
        bool LoadFromDbc(world::terrain::IMpqArchive& archive)
        {
            DBCFileLoader dbc;
            if (!LoadDbcFromMpq(archive, "DBFilesClient\\LiquidType.dbc", LiquidTypefmt, dbc))
            {
                return false;
            }

            m_entries.clear();
            for (uint32_t r = 0; r < dbc.GetNumRows(); ++r)
            {
                DBCFileLoader::Record rec = dbc.getRecord(r);
                LiquidTypeInfo info;
                info.type = rec.getUInt(2);
                info.spellId = rec.getUInt(3);
                m_entries[rec.getUInt(0)] = info;
            }
            return true;
        }

        const LiquidTypeInfo* Find(uint32_t id) const
        {
            auto it = m_entries.find(id);
            return it != m_entries.end() ? &it->second : nullptr;
        }

        size_t Size() const { return m_entries.size(); }

    private:
        std::unordered_map<uint32_t, LiquidTypeInfo> m_entries;
    };

    // Same Type→family map as GridMap::LiquidFlagsOfRow (water/ocean/magma/slime bits).
    inline world::terrain::LiquidKind ClassifyLiquid(uint32_t entry,
                                                     const LiquidTypeStore* store)
    {
        using world::terrain::LiquidKind;
        if (!entry)
        {
            return LiquidKind::None;
        }

        if (store)
        {
            if (const LiquidTypeInfo* info = store->Find(entry))
            {
                switch (info->type)
                {
                    case 0: return LiquidKind::Magma;
                    case 2: return LiquidKind::Slime;
                    default:
                        return (entry == 2 || entry == 15)
                               ? LiquidKind::Ocean
                               : LiquidKind::Water;
                }
            }
        }

        if (entry <= 12)
        {
            switch ((entry - 1) & 3)
            {
                case 0: return LiquidKind::Water;
                case 1: return LiquidKind::Ocean;
                case 2: return LiquidKind::Magma;
                default: return LiquidKind::Slime;
            }
        }
        switch (entry)
        {
            case 13: case 17: case 41: case 61: case 81: return LiquidKind::Water;
            case 14: case 100: return LiquidKind::Ocean;
            case 15: case 19: case 121: case 141: return LiquidKind::Magma;
            case 20: case 21: case 181: return LiquidKind::Slime;
            default: return LiquidKind::Water;
        }
    }
}
