#pragma once

// Map.dbc -> { id : Directory }. Directory is field 1 (fmt "nsix...").

#include "MpqDbcLoader.hpp"

#include "Server/DBCfmt.h"

#include <cstdint>
#include <map>
#include <string>

namespace world
{
    class MapDbcStore
    {
    public:
        bool LoadFromDbc(world::terrain::IMpqArchive& archive)
        {
            DBCFileLoader dbc;
            if (!LoadDbcFromMpq(archive, "DBFilesClient\\Map.dbc", MapEntryfmt, dbc))
            {
                return false;
            }

            m_directories.clear();
            for (uint32_t r = 0; r < dbc.GetNumRows(); ++r)
            {
                DBCFileLoader::Record rec = dbc.getRecord(r);
                const char* dir = rec.getString(1);
                if (dir && *dir)
                {
                    m_directories[rec.getUInt(0)] = dir;
                }
            }
            return true;
        }

        const std::string* Find(uint32_t mapId) const
        {
            auto it = m_directories.find(mapId);
            return it != m_directories.end() ? &it->second : nullptr;
        }

        const std::map<uint32_t, std::string>& All() const { return m_directories; }

    private:
        std::map<uint32_t, std::string> m_directories;
    };
}
