#pragma once

// One ADT tile's worth of data, in the form the height engine needs — nothing to
// do with how it was loaded (synthetic, or parsed from an MPQ). A tile carries:
//   * the terrain height field (the V9 9x9-per-chunk corner grid and the V8 8x8
//     center grid, exactly OregonCore's MCVT layout) plus the per-chunk hole mask;
//   * the static objects placed on it (WMO/M2), each an instance Transform + a
//     shared CollisionModel + its world-space bounds for cheap column culling.
//
// Coordinate convention matches OregonCore's GridMap so the terrain math is the
// proven one: a tile is 533.33333 yards, the height grid is 128 cells across it,
// and the world<->grid mapping is g = 128 * (32 - coord/533.33333).

#include "terrain/Geometry.hpp"
#include "terrain/ICollisionModel.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>
#include <cmath>

namespace world::terrain
{

    constexpr float TILE_SIZE = 533.33333f;    // SIZE_OF_GRIDS
    constexpr int GRID_PER_TILE = 128;         // MAP_RESOLUTION (V8 side; V9 = +1)
    constexpr int V9_SIDE = GRID_PER_TILE + 1; // 129
    constexpr int CHUNKS = 16;                 // MCNK per tile side
    constexpr uint32_t MAP_CENTER = 32;        // center tile id

    // Global height-grid coordinate for a world axis value (x or y).
    inline float gridCoord(float c) { return GRID_PER_TILE * (MAP_CENTER - c / TILE_SIZE); }
    // Which tile index a world axis value falls in.
    inline int tileIndex(float c) { return static_cast<int>(gridCoord(c)) >> 7; }

    // What a liquid query returns: the surface Z and what kind of liquid it is.
    enum class LiquidKind : uint8_t
    {
        None = 0,
        Water = 1,
        Ocean = 2,
        Magma = 3,
        Slime = 4,
        Lava = Magma  // "lava" is just the magma kind — alias, NOT a distinct value.
                      // (Previously this was 1, which silently collided with Water and
                      // made WMO lava classify as water.)
    };
    struct LiquidInfo
    {
        float level = 0.f;
        LiquidKind kind = LiquidKind::None;
        // LiquidType.dbc id (0 = unknown). The server needs it only to look up the
        // liquid's aura spell (SSC water, slime); the category above drives everything
        // else. The canonical ids 1..4 coincide with the LiquidKind values by design.
        uint16_t entry = 0;
    };

    // A placement of a static model on this tile.
    struct StaticInstance
    {
        Transform xf;
        std::shared_ptr<const ICollisionModel> model;
        Aabb worldBounds;   // model bounds -> world (for culling)
        int32_t adtId = 0;  // WMO placement nameSet (MODF), the WMOAreaTable "adtId"
    };

    class TerrainTile
    {
    public:
        int tx = 0, ty = 0;
        bool hasTerrain = false;
        bool isGlobalWmo = false;

        std::vector<float> v9;                         // V9_SIDE*V9_SIDE corner heights (absolute Z)
        std::vector<float> v8;                         // GRID_PER_TILE*GRID_PER_TILE center heights
        std::array<uint16_t, CHUNKS * CHUNKS> holes{}; // per-MCNK 4x4 hole mask
        // Per-MCNK AreaTable.dbc id (0 = unset), same chunk indexing as holes. Drives the
        // zone-map exploration discovery — see areaId(x,y).
        std::array<uint16_t, CHUNKS * CHUNKS> areaIds{};

        // Liquid surface (water/ocean/magma), parsed from per-MCNK MCLQ. liquidHeight
        // is a V9-style 129x129 corner grid of the surface Z; liquidShow marks which of
        // the 128x128 cells actually carry liquid. Same orientation as v9/v8.
        bool hasLiquid = false;
        std::vector<float> liquidHeight; // V9_SIDE*V9_SIDE
        std::vector<uint8_t> liquidShow; // GRID_PER_TILE*GRID_PER_TILE
        std::vector<uint8_t> liquidType; // GRID_PER_TILE*GRID_PER_TILE (LiquidKind)

        std::vector<StaticInstance> instances;

        // Terrain height at world (x,y), or nullopt if there is no terrain there (no
        // height field, or the spot is a hole — e.g. a cave mouth). Mirrors
        // GridMap::getHeightFromFloat: 4 triangles meeting at the V8 center.
        std::optional<float> terrainHeight(float x, float y) const
        {
            if (!hasTerrain || v8.empty() || v9.empty())
                return std::nullopt;

            float gx = gridCoord(x), gy = gridCoord(y);
            int ix = static_cast<int>(gx) & (GRID_PER_TILE - 1);
            int iy = static_cast<int>(gy) & (GRID_PER_TILE - 1);
            if (isHole(ix, iy))
                return std::nullopt;

            float fx = gx - std::floor(gx);
            float fy = gy - std::floor(gy);

            auto V9 = [&](int a, int b)
            { return v9[a * V9_SIDE + b]; };
            auto V8 = [&](int a, int b)
            { return v8[a * GRID_PER_TILE + b]; };

            float a, b, c;
            if (fx + fy < 1.f)
            {
                if (fx > fy)
                { // triangle (h1,h2,h5)
                    float h1 = V9(ix, iy), h2 = V9(ix + 1, iy), h5 = 2 * V8(ix, iy);
                    a = h2 - h1;
                    b = h5 - h1 - h2;
                    c = h1;
                }
                else
                { // triangle (h1,h3,h5)
                    float h1 = V9(ix, iy), h3 = V9(ix, iy + 1), h5 = 2 * V8(ix, iy);
                    a = h5 - h1 - h3;
                    b = h3 - h1;
                    c = h1;
                }
            }
            else
            {
                if (fx > fy)
                { // triangle (h2,h4,h5)
                    float h2 = V9(ix + 1, iy), h4 = V9(ix + 1, iy + 1), h5 = 2 * V8(ix, iy);
                    a = h2 + h4 - h5;
                    b = h4 - h2;
                    c = h5 - h4;
                }
                else
                { // triangle (h3,h4,h5)
                    float h3 = V9(ix, iy + 1), h4 = V9(ix + 1, iy + 1), h5 = 2 * V8(ix, iy);
                    a = h4 - h3;
                    b = h3 + h4 - h5;
                    c = h5 - h4;
                }
            }
            return a * fx + b * fy + c;
        }

        // Liquid surface + kind at world (x,y) from the terrain (ADT MCLQ), or nullopt
        // if there is no liquid there. Bilinear over the four corners of the (usually
        // flat) surface. WMO interior liquid is handled separately (see FusedTerrain).
        std::optional<LiquidInfo> liquidAt(float x, float y) const
        {
            if (!hasLiquid || liquidHeight.empty() || liquidShow.empty())
                return std::nullopt;

            float gx = gridCoord(x), gy = gridCoord(y);
            int ix = static_cast<int>(gx) & (GRID_PER_TILE - 1);
            int iy = static_cast<int>(gy) & (GRID_PER_TILE - 1);
            if (!liquidShow[ix * GRID_PER_TILE + iy])
                return std::nullopt;

            float fx = gx - std::floor(gx);
            float fy = gy - std::floor(gy);
            auto LH = [&](int a, int b)
            { return liquidHeight[a * V9_SIDE + b]; };
            float top = LH(ix, iy) * (1 - fx) + LH(ix + 1, iy) * fx;
            float bot = LH(ix, iy + 1) * (1 - fx) + LH(ix + 1, iy + 1) * fx;

            LiquidInfo info;
            info.level = top * (1 - fy) + bot * fy;
            info.kind = liquidType.empty()
                            ? LiquidKind::Water
                            : static_cast<LiquidKind>(liquidType[ix * GRID_PER_TILE + iy]);
            // ADT liquid is always one of the four canonical LiquidType.dbc rows, whose
            // ids are 1..4 in the same order as LiquidKind -- the same identity the
            // reference map-extractor writes for an MCLQ chunk.
            info.entry = static_cast<uint16_t>(info.kind);
            return info;
        }

        // Is the height cell (ix,iy) — tile-local indices in [0,128) — punched out? A hole
        // is a gap in the terrain (a cave mouth), where the heightfield must not be walked
        // on or rasterized. Exposed for the offline navmesh bake.
        bool isHoleAt(int ix, int iy) const { return isHole(ix, iy); }

        // AreaTable.dbc id at world (x,y) — the MCNK chunk the position falls in, or 0 when
        // unknown (no area data baked, e.g. a pre-v5 cache or a global-WMO tile). Same chunk
        // indexing as isHole. World thread only.
        uint16_t areaId(float x, float y) const
        {
            float gx = gridCoord(x), gy = gridCoord(y);
            int ix = static_cast<int>(gx) & (GRID_PER_TILE - 1);
            int iy = static_cast<int>(gy) & (GRID_PER_TILE - 1);
            return areaIds[(ix / 8) * CHUNKS + (iy / 8)];
        }

    private:
        // A MCNK hole mask is a 4x4 grid; each bit covers a 2x2 block of the chunk's
        // 8x8 height cells. (ix,iy) are tile-local grid indices in [0,128).
        bool isHole(int ix, int iy) const
        {
            int chunk = (ix / 8) * CHUNKS + (iy / 8);
            uint16_t mask = holes[chunk];
            if (!mask)
                return false;
            int hi = (ix % 8) / 2, hj = (iy % 8) / 2; // 0..3
            return (mask >> (hi * 4 + hj)) & 1u;
        }
    };

    // Where tiles come from. Implementations: SyntheticTileSource (tests/demo) and
    // MpqTileSource (real client data via IMpqArchive). Returns nullptr if the tile
    // does not exist for that map (e.g. an ocean tile with no ADT).
    class ITileSource
    {
    public:
        virtual ~ITileSource() = default;
        virtual std::shared_ptr<TerrainTile> load(uint32_t mapId, int tx, int ty) = 0;
    };

    // "No data" source: every tile is empty. Lets the world run with the terrain query
    // wired in but no client data available (a build without WITH_MPQ, or a daemon
    // started without a data dir) — floorAt then returns nullopt and callers fall back
    // to keeping the caller-supplied Z.
    class NullTileSource : public ITileSource
    {
    public:
        std::shared_ptr<TerrainTile> load(uint32_t, int, int) override { return nullptr; }
    };

} // namespace world::terrain
