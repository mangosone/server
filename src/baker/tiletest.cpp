/*
 * Copyright (C) 2005-2026 MaNGOS <https://www.getmangos.eu>
 *
 * MangosOne - a World of Warcraft 2.4.3 (The Burning Crusade) server.
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

// mangos-tiletest — a standalone probe for the baked fused .tile files. It drives
// the SAME runtime code the server uses (FusedTerrain + the terrain compute core),
// so a query here is exactly what mangosd would compute for that point. Use it to
// spot-check a bake: floor height (Z), liquid, area id, the WMO area triple
// (rootId/adtId/groupId) + MOGP flags, and indoor/outdoor.
//
//   mangos-tiletest <tileDir> <mapId> <x> <y> [z]
//
// tileDir : the baker's <dest>/tiles dir (holds t_<map>_<tx>_<ty>.tile / w_<map>.tile)
// x,y     : world coordinates (MaNGOS/Map frame)
// z       : optional reference height. Omitted -> the tool finds the ground first and
//           probes area/liquid/indoor just above it (what you usually want).

#include "FusedTerrain.h"
#include "terrain/Terrain.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

namespace
{
    const char* liquidKindName(world::terrain::LiquidKind k)
    {
        switch (k)
        {
            case world::terrain::LiquidKind::Water: return "Water";
            case world::terrain::LiquidKind::Ocean: return "Ocean";
            case world::terrain::LiquidKind::Magma: return "Magma/Lava";
            case world::terrain::LiquidKind::Slime: return "Slime";
            default:                                return "None";
        }
    }

    void printMogpFlags(uint32_t f)
    {
        std::cout << "mogpFlag: 0x" << std::hex << f << std::dec << "  [";
        if (f & 0x00000001u) { std::cout << " HasBSP"; }
        if (f & 0x00000004u) { std::cout << " HasVertexColor"; }
        if (f & 0x00000008u) { std::cout << " Outdoor(0x8)"; }
        if (f & 0x00000040u) { std::cout << " Unreachable"; }
        if (f & 0x00000200u) { std::cout << " HasLights"; }
        if (f & 0x00000800u) { std::cout << " HasDoodads"; }
        if (f & 0x00001000u) { std::cout << " HasLiquid"; }
        if (f & 0x00002000u) { std::cout << " Indoor(0x2000)"; }
        if (f & 0x00008000u) { std::cout << " Outdoor(0x8000)"; }
        std::cout << " ]\n";
    }
} // namespace

int main(int argc, char** argv)
{
    if (argc < 5)
    {
        std::cerr <<
            "usage: mangos-tiletest <tileDir> <mapId> <x> <y> [z]\n"
            "\n"
            "Probes a baked fused .tile via FusedTerrain (the runtime query path) and\n"
            "prints floor height, liquid, area id, WMO area triple + flags, indoor/outdoor.\n"
            "  tileDir : the baker's <dest>/tiles directory\n"
            "  x, y    : world coordinates (MaNGOS Map frame)\n"
            "  z       : optional reference height (default: found ground + 1)\n";
        return 2;
    }

    const std::string tileDir = argv[1];
    const uint32_t mapId = static_cast<uint32_t>(std::strtoul(argv[2], nullptr, 10));
    const float x = std::strtof(argv[3], nullptr);
    const float y = std::strtof(argv[4], nullptr);
    const bool haveZ = argc > 5;

    FusedTerrain::SetTileDir(tileDir);
    FusedTerrain terrain(mapId);

    // Pick the reference Z: given, or the ground we can find from way up high.
    float z;
    if (haveZ)
    {
        z = std::strtof(argv[5], nullptr);
    }
    else
    {
        float ground = 0.0f;
        z = terrain.GetHeight(x, y, 100000.0f, ground, 100000.0f, 200000.0f)
                ? ground + 1.0f
                : 0.0f;
    }

    std::cout << "tileDir : " << tileDir << "\n";
    std::cout << "query   : map " << mapId << " @ (" << x << ", " << y << ", " << z << ")"
              << (haveZ ? "" : "   [z auto from ground]") << "\n";
    std::cout << "----------------------------------------\n";

    float floorZ = 0.0f;
    if (terrain.GetHeight(x, y, z, floorZ, /*searchUp*/ 50.0f, /*maxDrop*/ 500.0f))
    {
        std::cout << "floor Z : " << floorZ << "\n";
    }
    else
    {
        std::cout << "floor Z : (none - off-map / no data / hole)\n";
    }

    world::terrain::LiquidInfo lq;
    if (terrain.GetLiquid(x, y, z, lq))
    {
        std::cout << "liquid  : " << lq.level << "  (" << liquidKindName(lq.kind) << ")\n";
    }
    else
    {
        std::cout << "liquid  : (none)\n";
    }

    std::cout << "areaId  : " << terrain.GetAreaId(x, y) << "\n";

    uint32_t mogp = 0;
    int32_t adtId = 0, rootId = 0, groupId = 0;
    float groundZ = 0.0f;
    if (terrain.GetAreaInfo(x, y, z, mogp, adtId, rootId, groupId, groundZ))
    {
        std::cout << "WMO     : root=" << rootId << " adt=" << adtId
                  << " group=" << groupId << "   groundZ=" << groundZ << "\n";
        printMogpFlags(mogp);
    }
    else
    {
        std::cout << "WMO     : (no WMO over this point)\n";
    }

    std::cout << "outdoors: " << (terrain.IsOutdoors(x, y, z) ? "YES" : "NO (indoors)") << "\n";
    return 0;
}
