#pragma once

// Bridges the client MPQ to the server's own DBC reader. The baker carries no DBC
// parser of its own: it reads the raw WDBC image out of the archive and hands it to
// DBCFileLoader, driven by the format strings in Server/DBCfmt.h. One parser and one
// column layout, so the baker cannot drift from the server's view of a .dbc.
//
// Format strings come from Server/DBCfmt.h. On-disk, 'x' and 's' are both 4-byte
// slots, so getString still works if a column is ignored server-side.

#include "IMpqArchive.hpp"

#include "DataStores/DBCFileLoader.h"

#include <cstdint>
#include <string>
#include <vector>

namespace world
{
    inline bool LoadDbcFromMpq(world::terrain::IMpqArchive& archive,
                               const std::string& dbcPath, const char* fmt,
                               DBCFileLoader& out)
    {
        std::vector<uint8_t> bytes;
        if (!archive.Read(dbcPath, bytes))
        {
            return false;
        }
        return out.LoadFromMemory(bytes.data(), bytes.size(), fmt);
    }
}
