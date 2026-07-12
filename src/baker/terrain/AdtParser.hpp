#pragma once

// Parses one TBC (.adt) tile's raw bytes into the pieces the height engine wants:
//   * the V9 (129x129) and V8 (128x128) absolute-Z height grids and the per-MCNK
//     hole mask, laid out exactly as OregonCore's GridMap reads them (so the
//     ported getHeightFromFloat in TerrainTile indexes them correctly);
//   * the static placements (WMO via MODF, M2/doodad via MDDF) plus the model
//     filename tables they reference — consumed later by MpqTileSource to attach
//     CollisionModels.
//
// Byte layout:
// MCNK base Z is the chunk's `ypos` field; MCVT holds 145 floats interleaved as
// 9 V9 then 8 V8 per row; global fill uses i=MCNK.iy (->first/stride dim from
// world X) and j=MCNK.ix (->second dim from world Y).

#include "terrain/Geometry.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace world::terrain
{

    struct AdtPlacement
    {
        uint32_t nameIndex = 0; // index into the matching name table
        Vec3 pos{};             // raw WoW placement coordinates
        Vec3 rotDeg{};          // rotation (degrees): X, Y, Z
        float scale = 1.0f;     // WMO has none (1.0); M2 carries scale/1024
        uint16_t nameSet = 0;   // MODF nameSet (WMOAreaTable "adtId"); 0 for M2 (MDDF)
        // MODF doodadSet: which of the WMO's MODS doodad sets this placement dresses
        // itself with. A building is shipped with several alternative furnishings and
        // the placement picks one; only that set's doodads exist in the world, so only
        // that set's collision may be baked. 0 for M2 (MDDF) placements.
        uint16_t doodadSet = 0;
    };

    struct AdtData
    {
        bool hasTerrain = false;
        std::vector<float> v9; // 129*129, absolute Z
        std::vector<float> v8; // 128*128, absolute Z
        std::array<uint16_t, 16 * 16> holes{};
        // Per-MCNK AreaTable.dbc id (the MCNK header `areaid`), indexed [iy*16+ix] like holes.
        // 0 = unset (a chunk the file didn't tag). Drives zone-map exploration discovery.
        std::array<uint16_t, 16 * 16> areaIds{};

        bool hasLiquid = false;
        std::vector<float> liquidHeight; // 129*129 surface Z (MCLQ)
        std::vector<uint8_t> liquidShow; // 128*128 cell-has-liquid mask
        std::vector<uint8_t> liquidType; // 128*128 LiquidKind per cell

        std::vector<std::string> wmoNames;       // resolved via MWMO + MWID
        std::vector<std::string> m2Names;        // resolved via MMDX + MMID
        std::vector<AdtPlacement> wmoPlacements; // MODF
        std::vector<AdtPlacement> m2Placements;  // MDDF
    };

    // Parse the bytes of one .adt. Returns false only on a structurally broken file;
    // a valid-but-empty tile yields hasTerrain=false with no placements.
    bool parseAdt(const uint8_t *data, size_t size, AdtData &out);

    inline bool parseAdt(const std::vector<uint8_t> &bytes, AdtData &out)
    {
        return parseAdt(bytes.data(), bytes.size(), out);
    }

} // namespace world::terrain
