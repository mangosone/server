#pragma once

// Offline navmesh bake: turns the fused terrain+collision .tile files into the
// Detour navmesh the server's MMAP layer loads (mmaps/<map>.mmap + .mmtile).
//
// Input is the baked tile, not the client MPQs -- the geometry the navmesh walks on
// is exactly the geometry FusedTerrain collides against, so pathfinding can never
// disagree with collision. Per tile we rasterize:
//
//   * the ADT heightfield (V9 corners + V8 centres, 4 triangles per cell, holes skipped);
//   * every static WMO/M2 instance, its triangles pushed through the placement transform;
//   * the ADT liquid surface, carrying NAV_WATER / NAV_MAGMA / NAV_SLIME area flags.
//
// Coordinate spaces. WoW is Z-up; Recast/Detour is Y-up. The server converts a world
// point with (x, y, z) -> (y, z, x) (see PathFinder), a cyclic permutation, so it is
// orientation preserving and triangle winding (hence face normals) carries over intact.
// We emit vertices in that same recast space.
//
// Tile indices. A MaNGOS grid cell (gx, gy) covers world X in [(31-gx)*533.33,
// (32-gx)*533.33] and likewise Y for gy. Since recast X == world Y and recast Z ==
// world X, the navmesh tile coordinates are SWAPPED relative to the grid: navTileX = gy
// and navTileY = gx. That is why the runtime loads `mmaps/%04u%02i%02i.mmtile` with
// (mapId, y, x) -- see MMapManager::loadMap.

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace world::nav
{

    /// Agent + rasterization settings. Defaults reproduce the values the server's
    /// PathFinder was tuned against (0.2666yd cells, ~1.6yd agent).
    struct NavConfig
    {
        /// World size of one voxel, in yards. Must divide 533.33333 evenly.
        float cellSize = 0.266666f;
        /// Steepest slope an agent may walk, in degrees.
        float maxWalkableAngle = 60.0f;
        /// Voxel counts (all in cell units).
        int walkableHeight = 6;  ///< agent height
        int walkableClimb = 4;   ///< max step up; keep below walkableHeight
        int walkableRadius = 2;  ///< agent radius
        /// Recast sub-tile edge, in cells. Must divide 533.33333 / cellSize.
        int subTileSize = 80;
        /// Worker threads; 0 asks the hardware. Tiles are baked independently -- each
        /// reads one .tile and writes one .mmtile -- so the bake scales with cores.
        int threads = 0;
    };

    /// Bakes navmesh for every map found in `tileDir`, writing into `outDir`.
    class NavMeshBuilder
    {
    public:
        NavMeshBuilder(std::string tileDir, std::string outDir, NavConfig cfg = {});

        /// Bakes all maps (or only `mapFilter` when >= 0).
        /// Returns the number of .mmtile files written; -1 on a fatal error.
        int bakeAll(long mapFilter = -1);

    private:
        /// Bakes one map. Returns tiles written.
        int bakeMap(uint32_t mapId, const std::vector<std::pair<int, int>> &grids);

        std::string m_tileDir;
        std::string m_outDir;
        NavConfig m_cfg;
    };

} // namespace world::nav
