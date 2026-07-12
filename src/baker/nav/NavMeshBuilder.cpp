#include "nav/NavMeshBuilder.hpp"

#include "terrain/CollisionModel.hpp"
#include "terrain/Terrain.hpp"
#include "terrain/TileSerializer.hpp"
#include "terrain/WmoModel.hpp"

#include "DetourAlloc.h"
#include "DetourNavMesh.h"
#include "DetourNavMeshBuilder.h"
#include "Recast.h"

#include <algorithm>
#include <atomic>
#include <cfloat>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace world::nav
{
    namespace
    {
        using world::terrain::CollisionModel;
        using world::terrain::TerrainTile;
        using world::terrain::Vec3;
        using world::terrain::WmoModel;

        /// Workers only ever write whole lines here, and only on failure, so one lock
        /// around the whole message keeps diagnostics readable. Progress marks bypass it:
        /// a single '*' cannot tear, which is the point of using one character.
        std::mutex g_logMutex;

        void logError(const std::string& msg)
        {
            std::lock_guard<std::mutex> lock(g_logMutex);
            std::cerr << msg << "\n";
        }

        constexpr float GRID_SIZE = world::terrain::TILE_SIZE;   // 533.33333
        constexpr int V9_SIDE = world::terrain::V9_SIDE;          // 129
        constexpr int V8_SIDE = world::terrain::GRID_PER_TILE;    // 128
        constexpr float GRID_PART = GRID_SIZE / float(V8_SIDE);

        // Mirrors MoveMapSharedDefines.h. Duplicated (not included) because that header
        // drags in Platform/Define.h -> ACE, which the offline baker must not link.
        constexpr uint32_t MMAP_MAGIC = 0x4d4d4150;   // 'MMAP'
        constexpr uint32_t MMAP_VERSION = 5;

        struct MmapTileHeader
        {
            uint32_t mmapMagic = MMAP_MAGIC;
            uint32_t dtVersion = DT_NAVMESH_VERSION;
            uint32_t mmapVersion = MMAP_VERSION;
            uint32_t size = 0;
            bool usesLiquids : 1;

            MmapTileHeader() : usesLiquids(true) {}
        };

        enum NavTerrain : unsigned char
        {
            NAV_EMPTY = 0x00,
            NAV_GROUND = 0x01,
            NAV_MAGMA = 0x02,
            NAV_SLIME = 0x04,
            NAV_WATER = 0x08
        };

        // --- RAII for Recast's C-style allocations -----------------------------------
        struct HfDel { void operator()(rcHeightfield* p) const { rcFreeHeightField(p); } };
        struct ChfDel { void operator()(rcCompactHeightfield* p) const { rcFreeCompactHeightfield(p); } };
        struct CsDel { void operator()(rcContourSet* p) const { rcFreeContourSet(p); } };
        struct PmDel { void operator()(rcPolyMesh* p) const { rcFreePolyMesh(p); } };
        struct DmDel { void operator()(rcPolyMeshDetail* p) const { rcFreePolyMeshDetail(p); } };

        using HfPtr = std::unique_ptr<rcHeightfield, HfDel>;
        using ChfPtr = std::unique_ptr<rcCompactHeightfield, ChfDel>;
        using CsPtr = std::unique_ptr<rcContourSet, CsDel>;
        using PmPtr = std::unique_ptr<rcPolyMesh, PmDel>;
        using DmPtr = std::unique_ptr<rcPolyMeshDetail, DmDel>;

        /// A triangle soup already in RECAST space, with a per-triangle area flag.
        struct Soup
        {
            std::vector<float> verts;          ///< xyz triples
            std::vector<int> tris;             ///< index triples
            std::vector<unsigned char> areas;  ///< one per triangle

            /// Appends a WoW-space point, converting to recast: (x,y,z) -> (y,z,x).
            int addVertex(const Vec3& w)
            {
                const int index = int(verts.size() / 3);
                verts.push_back(w.y);
                verts.push_back(w.z);
                verts.push_back(w.x);
                return index;
            }

            void addTriangle(int a, int b, int c, unsigned char area)
            {
                tris.push_back(a);
                tris.push_back(b);
                tris.push_back(c);
                areas.push_back(area);
            }

            int vertexCount() const { return int(verts.size() / 3); }
            int triangleCount() const { return int(tris.size() / 3); }
            bool empty() const { return tris.empty(); }
        };

        /// World X of a V9 column on grid gx (X falls as the index grows).
        inline float worldX(int gx, float ix) { return (32.0f - float(gx)) * GRID_SIZE - ix * GRID_PART; }
        inline float worldY(int gy, float iy) { return (32.0f - float(gy)) * GRID_SIZE - iy * GRID_PART; }

        /// Recast tile bounds for a navmesh tile (navTileX, navTileY).
        void tileBoundsXZ(int navTileX, int navTileY, float* bmin, float* bmax)
        {
            bmax[0] = (32.0f - float(navTileX)) * GRID_SIZE;
            bmax[2] = (32.0f - float(navTileY)) * GRID_SIZE;
            bmin[0] = bmax[0] - GRID_SIZE;
            bmin[2] = bmax[2] - GRID_SIZE;
        }

        // --- Geometry gathering -------------------------------------------------------

        /// The ADT heightfield: 4 triangles per cell around its V8 centre, holes skipped.
        ///
        /// Winding matters: Recast slope-filters on the triangle normal's Y. In recast
        /// space x == world Y and z == world X, and BOTH fall as the cell index grows, so
        /// the ring a->b->c->d (corners (ix,iy),(ix,iy+1),(ix+1,iy+1),(ix+1,iy)) runs
        /// clockwise seen from +Y. Emitting (a, m, b) rather than (a, b, m) is what makes
        /// n.y positive, i.e. the ground face up.
        void addTerrain(const TerrainTile& tile, int gx, int gy, Soup& out)
        {
            if (!tile.hasTerrain || tile.v9.empty() || tile.v8.empty())
            {
                return;
            }

            const auto v9 = [&](int ix, int iy) { return tile.v9[ix * V9_SIDE + iy]; };
            const auto v8 = [&](int ix, int iy) { return tile.v8[ix * V8_SIDE + iy]; };

            // V9 corners first so a cell can reference its four neighbours by index.
            const int base9 = out.vertexCount();
            for (int ix = 0; ix < V9_SIDE; ++ix)
            {
                for (int iy = 0; iy < V9_SIDE; ++iy)
                {
                    out.addVertex(Vec3{worldX(gx, float(ix)), worldY(gy, float(iy)), v9(ix, iy)});
                }
            }

            const auto corner = [&](int ix, int iy) { return base9 + ix * V9_SIDE + iy; };

            for (int ix = 0; ix < V8_SIDE; ++ix)
            {
                for (int iy = 0; iy < V8_SIDE; ++iy)
                {
                    if (tile.isHoleAt(ix, iy))
                    {
                        continue;
                    }

                    const int m = out.addVertex(Vec3{worldX(gx, float(ix) + 0.5f),
                                                     worldY(gy, float(iy) + 0.5f),
                                                     v8(ix, iy)});
                    const int a = corner(ix, iy);
                    const int b = corner(ix, iy + 1);
                    const int c = corner(ix + 1, iy + 1);
                    const int d = corner(ix + 1, iy);

                    out.addTriangle(a, m, b, NAV_GROUND);
                    out.addTriangle(b, m, c, NAV_GROUND);
                    out.addTriangle(c, m, d, NAV_GROUND);
                    out.addTriangle(d, m, a, NAV_GROUND);
                }
            }
        }

        unsigned char liquidArea(world::terrain::LiquidKind kind)
        {
            switch (kind)
            {
                case world::terrain::LiquidKind::Magma: return NAV_MAGMA;
                case world::terrain::LiquidKind::Slime: return NAV_SLIME;
                case world::terrain::LiquidKind::Water:
                case world::terrain::LiquidKind::Ocean: return NAV_WATER;
                default: return NAV_EMPTY;
            }
        }

        /// ADT liquid surface: two triangles per shown cell, flagged by liquid kind.
        void addLiquid(const TerrainTile& tile, int gx, int gy, Soup& out)
        {
            if (!tile.hasLiquid || tile.liquidHeight.empty() || tile.liquidShow.empty())
            {
                return;
            }

            const auto h = [&](int ix, int iy) { return tile.liquidHeight[ix * V9_SIDE + iy]; };

            for (int ix = 0; ix < V8_SIDE; ++ix)
            {
                for (int iy = 0; iy < V8_SIDE; ++iy)
                {
                    if (!tile.liquidShow[ix * V8_SIDE + iy])
                    {
                        continue;
                    }

                    const auto kind = tile.liquidType.empty()
                                          ? world::terrain::LiquidKind::Water
                                          : world::terrain::LiquidKind(tile.liquidType[ix * V8_SIDE + iy]);
                    const unsigned char area = liquidArea(kind);
                    if (area == NAV_EMPTY)
                    {
                        continue;
                    }

                    const int a = out.addVertex(Vec3{worldX(gx, float(ix)), worldY(gy, float(iy)), h(ix, iy)});
                    const int b = out.addVertex(Vec3{worldX(gx, float(ix)), worldY(gy, float(iy) + 1.f), h(ix, iy + 1)});
                    const int c = out.addVertex(Vec3{worldX(gx, float(ix) + 1.f), worldY(gy, float(iy) + 1.f), h(ix + 1, iy + 1)});
                    const int d = out.addVertex(Vec3{worldX(gx, float(ix) + 1.f), worldY(gy, float(iy)), h(ix + 1, iy)});

                    // Reversed ring, same reason as the terrain: up-facing normals.
                    out.addTriangle(a, c, b, area);
                    out.addTriangle(a, d, c, area);
                }
            }
        }

        /// Every static WMO/M2 instance, pushed through its placement transform. The
        /// recast conversion is a cyclic axis permutation, so the authored winding (and
        /// therefore the face normals Recast slope-filters on) survives untouched.
        void addModels(const TerrainTile& tile, Soup& out)
        {
            for (const auto& inst : tile.instances)
            {
                if (!inst.model || inst.model->empty())
                {
                    continue;
                }

                const auto emit = [&](const Vec3& l) { return out.addVertex(inst.xf.localToWorld(l)); };

                if (const auto* wmo = dynamic_cast<const WmoModel*>(inst.model.get()))
                {
                    // The soup is already exactly the collidable faces -- the baker drops
                    // the render-only ones -- so there is no filter left to apply here.
                    const auto& soup = wmo->soup();
                    for (const auto& tri : soup.tris)
                    {
                        out.addTriangle(emit(soup.verts[tri[0]]),
                                        emit(soup.verts[tri[1]]),
                                        emit(soup.verts[tri[2]]), NAV_GROUND);
                    }
                }
                else if (const auto* cm = dynamic_cast<const CollisionModel*>(inst.model.get()))
                {
                    const auto& verts = cm->verts();
                    for (const auto& tri : cm->tris())
                    {
                        out.addTriangle(emit(verts[tri[0]]),
                                        emit(verts[tri[1]]),
                                        emit(verts[tri[2]]), NAV_GROUND);
                    }
                }
            }
        }

        // --- Recast pipeline ----------------------------------------------------------

        /// One recast sub-tile's poly + detail mesh, kept alive for the merge.
        struct SubTile
        {
            PmPtr pmesh;
            DmPtr dmesh;
        };

        /// Builds the sub-tile at (sx, sy). Returns false when it yields no polygons,
        /// which is normal for empty corners of a tile.
        bool buildSubTile(rcContext& ctx, const rcConfig& cfg, int borderSize, int subTileSize,
                          int sx, int sy, const Soup& solid, const Soup& liquid, SubTile& out)
        {
            rcConfig tcfg = cfg;
            tcfg.width = subTileSize + borderSize * 2;
            tcfg.height = subTileSize + borderSize * 2;
            tcfg.bmin[0] = cfg.bmin[0] + float(sx * subTileSize - borderSize) * cfg.cs;
            tcfg.bmin[2] = cfg.bmin[2] + float(sy * subTileSize - borderSize) * cfg.cs;
            tcfg.bmax[0] = cfg.bmin[0] + float((sx + 1) * subTileSize + borderSize) * cfg.cs;
            tcfg.bmax[2] = cfg.bmin[2] + float((sy + 1) * subTileSize + borderSize) * cfg.cs;

            HfPtr solidHf(rcAllocHeightfield());
            if (!solidHf || !rcCreateHeightfield(&ctx, *solidHf, tcfg.width, tcfg.height,
                                                 tcfg.bmin, tcfg.bmax, tcfg.cs, tcfg.ch))
            {
                return false;
            }

            // Solid geometry: start every triangle walkable, then let Recast clear the
            // ones whose normal is steeper than the agent can climb.
            {
                std::vector<unsigned char> areas = solid.areas;
                rcClearUnwalkableTriangles(&ctx, tcfg.walkableSlopeAngle, solid.verts.data(),
                                           solid.vertexCount(), solid.tris.data(),
                                           solid.triangleCount(), areas.data());
                rcRasterizeTriangles(&ctx, solid.verts.data(), solid.vertexCount(),
                                     solid.tris.data(), areas.data(), solid.triangleCount(),
                                     *solidHf, cfg.walkableClimb);
            }

            rcFilterLowHangingWalkableObstacles(&ctx, cfg.walkableClimb, *solidHf);
            rcFilterLedgeSpans(&ctx, tcfg.walkableHeight, tcfg.walkableClimb, *solidHf);
            rcFilterWalkableLowHeightSpans(&ctx, tcfg.walkableHeight, *solidHf);

            // Liquid is rasterized after the ledge/height filters so its surface is kept
            // verbatim -- a water plane is walkable (swimmable) regardless of what is
            // under it.
            if (!liquid.empty())
            {
                rcRasterizeTriangles(&ctx, liquid.verts.data(), liquid.vertexCount(),
                                     liquid.tris.data(), liquid.areas.data(),
                                     liquid.triangleCount(), *solidHf, cfg.walkableClimb);
            }

            ChfPtr chf(rcAllocCompactHeightfield());
            if (!chf || !rcBuildCompactHeightfield(&ctx, tcfg.walkableHeight, tcfg.walkableClimb,
                                                   *solidHf, *chf))
            {
                return false;
            }
            solidHf.reset();

            if (!rcErodeWalkableArea(&ctx, cfg.walkableRadius, *chf) ||
                !rcBuildDistanceField(&ctx, *chf) ||
                !rcBuildRegions(&ctx, *chf, tcfg.borderSize, tcfg.minRegionArea, tcfg.mergeRegionArea))
            {
                return false;
            }

            CsPtr cset(rcAllocContourSet());
            if (!cset || !rcBuildContours(&ctx, *chf, tcfg.maxSimplificationError, tcfg.maxEdgeLen, *cset))
            {
                return false;
            }

            PmPtr pmesh(rcAllocPolyMesh());
            if (!pmesh || !rcBuildPolyMesh(&ctx, *cset, tcfg.maxVertsPerPoly, *pmesh))
            {
                return false;
            }

            DmPtr dmesh(rcAllocPolyMeshDetail());
            if (!dmesh || !rcBuildPolyMeshDetail(&ctx, *pmesh, *chf, tcfg.detailSampleDist,
                                                 tcfg.detailSampleMaxError, *dmesh))
            {
                return false;
            }

            if (pmesh->npolys == 0)
            {
                return false;
            }

            out.pmesh = std::move(pmesh);
            out.dmesh = std::move(dmesh);
            return true;
        }

        bool writeFile(const std::string& path, const void* head, size_t headSize,
                       const void* body, size_t bodySize)
        {
            std::ofstream f(path, std::ios::binary | std::ios::trunc);
            if (!f)
            {
                logError("[nav] cannot open " + path + " for writing");
                return false;
            }
            f.write(static_cast<const char*>(head), std::streamsize(headSize));
            if (bodySize)
            {
                f.write(static_cast<const char*>(body), std::streamsize(bodySize));
            }
            return bool(f);
        }

        // --- Per-tile bake ------------------------------------------------------------

        /// Emit one '*' per this many tiles finished.
        constexpr int PROGRESS_EVERY = 10;

        /// Everything a tile worker needs, fixed for the whole map. Copied, not referenced,
        /// so a worker never reaches back into NavMeshBuilder.
        struct MapBake
        {
            uint32_t mapId = 0;
            std::string tileDir;
            std::string outDir;
            NavConfig cfg;
            float orig[3] = {0.0f, 0.0f, 0.0f};  ///< navmesh origin (recast space)
            int subTileSize = 0;
            int subTilesPerTile = 0;
            int borderSize = 0;
        };

        /// Bakes one grid cell into one .mmtile.
        ///
        /// Tiles share nothing: this reads its own .tile, owns its own Recast context and
        /// allocations, and writes its own file. That is what lets the map bake run one
        /// tile per core with no locking on the hot path.
        ///
        /// Returns false when the tile yields no navmesh -- an ocean tile with nothing to
        /// stand on is the common case, not an error.
        bool bakeTile(const MapBake& mb, int gx, int gy)
        {
            const std::string tilePath = mb.tileDir + "/t_" + std::to_string(mb.mapId) + "_" +
                                         std::to_string(gx) + "_" + std::to_string(gy) + ".tile";
            std::shared_ptr<TerrainTile> tile = world::terrain::readTile(tilePath);
            if (!tile)
            {
                return false;
            }

            const auto where = [&]()
            {
                return "[nav] map " + std::to_string(mb.mapId) + " tile (" +
                       std::to_string(gx) + "," + std::to_string(gy) + "): ";
            };

            Soup solid;
            Soup liquid;
            addTerrain(*tile, gx, gy, solid);
            addModels(*tile, solid);
            addLiquid(*tile, gx, gy, liquid);
            if (solid.empty())
            {
                return false;   // ocean tile: nothing to stand on
            }

            const int navTileX = gy;
            const int navTileY = gx;

            float bmin[3];
            float bmax[3];
            rcCalcBounds(solid.verts.data(), solid.vertexCount(), bmin, bmax);
            if (!liquid.empty())
            {
                float lmin[3];
                float lmax[3];
                rcCalcBounds(liquid.verts.data(), liquid.vertexCount(), lmin, lmax);
                bmin[1] = std::min(bmin[1], lmin[1]);
                bmax[1] = std::max(bmax[1], lmax[1]);
            }
            tileBoundsXZ(navTileX, navTileY, bmin, bmax);   // overrides X/Z, keeps elevation

            rcConfig cfg{};
            rcVcopy(cfg.bmin, bmin);
            rcVcopy(cfg.bmax, bmax);
            cfg.cs = mb.cfg.cellSize;
            cfg.ch = mb.cfg.cellSize;
            cfg.walkableSlopeAngle = mb.cfg.maxWalkableAngle;
            cfg.walkableHeight = mb.cfg.walkableHeight;
            cfg.walkableClimb = mb.cfg.walkableClimb;
            cfg.walkableRadius = mb.cfg.walkableRadius;
            cfg.tileSize = mb.subTileSize;
            cfg.borderSize = mb.borderSize;
            cfg.maxVertsPerPoly = DT_VERTS_PER_POLYGON;
            cfg.maxEdgeLen = mb.subTileSize + 1;
            cfg.minRegionArea = rcSqr(60);
            cfg.mergeRegionArea = rcSqr(50);
            cfg.maxSimplificationError = 2.0f;
            cfg.detailSampleDist = cfg.cs * 64.0f;
            cfg.detailSampleMaxError = cfg.ch * 2.0f;
            rcCalcGridSize(cfg.bmin, cfg.bmax, cfg.cs, &cfg.width, &cfg.height);

            // A 2000x2000 voxel field per tile is too large to build in one piece, so we
            // build subTilesPerTile^2 padded sub-tiles and merge their poly meshes.
            rcContext ctx(false);
            std::vector<SubTile> subTiles;
            subTiles.reserve(size_t(mb.subTilesPerTile) * size_t(mb.subTilesPerTile));
            for (int sy = 0; sy < mb.subTilesPerTile; ++sy)
            {
                for (int sx = 0; sx < mb.subTilesPerTile; ++sx)
                {
                    SubTile st;
                    if (buildSubTile(ctx, cfg, mb.borderSize, mb.subTileSize, sx, sy,
                                     solid, liquid, st))
                    {
                        subTiles.push_back(std::move(st));
                    }
                }
            }
            if (subTiles.empty())
            {
                return false;
            }

            std::vector<rcPolyMesh*> pmMerge;
            std::vector<rcPolyMeshDetail*> dmMerge;
            pmMerge.reserve(subTiles.size());
            dmMerge.reserve(subTiles.size());
            for (auto& st : subTiles)
            {
                pmMerge.push_back(st.pmesh.get());
                dmMerge.push_back(st.dmesh.get());
            }

            PmPtr polyMesh(rcAllocPolyMesh());
            DmPtr detailMesh(rcAllocPolyMeshDetail());
            if (!polyMesh || !detailMesh ||
                !rcMergePolyMeshes(&ctx, pmMerge.data(), int(pmMerge.size()), *polyMesh) ||
                !rcMergePolyMeshDetails(&ctx, dmMerge.data(), int(dmMerge.size()), *detailMesh))
            {
                logError(where() + "mesh merge failed");
                return false;
            }
            subTiles.clear();

            // Detour walks `flags`, Recast filled `areas`; carry the area bits across so
            // NAV_WATER / NAV_MAGMA / NAV_SLIME survive into the query filter.
            for (int i = 0; i < polyMesh->npolys; ++i)
            {
                if (polyMesh->areas[i] & RC_WALKABLE_AREA)
                {
                    polyMesh->flags[i] = polyMesh->areas[i];
                }
            }

            if (polyMesh->nverts >= 0xffff)
            {
                logError(where() + "too many vertices (" +
                         std::to_string(polyMesh->nverts) + ")");
                return false;
            }

            dtNavMeshCreateParams np{};
            np.verts = polyMesh->verts;
            np.vertCount = polyMesh->nverts;
            np.polys = polyMesh->polys;
            np.polyAreas = polyMesh->areas;
            np.polyFlags = polyMesh->flags;
            np.polyCount = polyMesh->npolys;
            np.nvp = polyMesh->nvp;
            np.detailMeshes = detailMesh->meshes;
            np.detailVerts = detailMesh->verts;
            np.detailVertsCount = detailMesh->nverts;
            np.detailTris = detailMesh->tris;
            np.detailTriCount = detailMesh->ntris;
            np.walkableHeight = mb.cfg.cellSize * float(cfg.walkableHeight);
            np.walkableRadius = mb.cfg.cellSize * float(cfg.walkableRadius);
            np.walkableClimb = mb.cfg.cellSize * float(cfg.walkableClimb);
            np.tileX = int(((bmin[0] + bmax[0]) * 0.5f - mb.orig[0]) / GRID_SIZE);
            np.tileY = int(((bmin[2] + bmax[2]) * 0.5f - mb.orig[2]) / GRID_SIZE);
            np.tileLayer = 0;
            rcVcopy(np.bmin, bmin);
            rcVcopy(np.bmax, bmax);
            np.cs = cfg.cs;
            np.ch = cfg.ch;
            np.buildBvTree = true;

            unsigned char* navData = nullptr;
            int navDataSize = 0;
            if (!dtCreateNavMeshData(&np, &navData, &navDataSize))
            {
                logError(where() + "dtCreateNavMeshData failed");
                return false;
            }

            MmapTileHeader header;
            header.size = uint32_t(navDataSize);
            header.usesLiquids = !liquid.empty();

            // Runtime loads mmaps/<map><navTileX><navTileY>.mmtile == (mapId, gy, gx).
            char nameBuf[32];
            std::snprintf(nameBuf, sizeof nameBuf, "%04u%02i%02i.mmtile",
                          mb.mapId, navTileX, navTileY);
            const bool ok = writeFile(mb.outDir + "/" + nameBuf, &header, sizeof header,
                                      navData, size_t(navDataSize));
            dtFree(navData);
            return ok;
        }
    } // namespace

    NavMeshBuilder::NavMeshBuilder(std::string tileDir, std::string outDir, NavConfig cfg)
        : m_tileDir(std::move(tileDir)), m_outDir(std::move(outDir)), m_cfg(cfg)
    {
    }

    int NavMeshBuilder::bakeMap(uint32_t mapId, const std::vector<std::pair<int, int>>& grids)
    {
        // Navmesh tile coords are the grid coords swapped (recast X == world Y).
        int navTileXMax = 0;
        int navTileYMax = 0;
        for (const auto& [gx, gy] : grids)
        {
            navTileXMax = std::max(navTileXMax, gy);
            navTileYMax = std::max(navTileYMax, gx);
        }

        // The navmesh origin is the min corner of the highest-indexed tile, matching the
        // convention the server's dtNavMesh was initialised with.
        float orig[3];
        float originMax[3];
        tileBoundsXZ(navTileXMax, navTileYMax, orig, originMax);
        orig[1] = FLT_MIN;

        dtNavMeshParams params{};
        params.orig[0] = orig[0];
        params.orig[1] = orig[1];
        params.orig[2] = orig[2];
        params.tileWidth = GRID_SIZE;
        params.tileHeight = GRID_SIZE;
        params.maxTiles = int(grids.size());
        params.maxPolys = 1 << DT_POLY_BITS;

        char nameBuf[32];
        std::snprintf(nameBuf, sizeof nameBuf, "%04u.mmap", mapId);
        if (!writeFile(m_outDir + "/" + nameBuf, &params, sizeof params, nullptr, 0))
        {
            return 0;
        }

        MapBake mb;
        mb.mapId = mapId;
        mb.tileDir = m_tileDir;
        mb.outDir = m_outDir;
        mb.cfg = m_cfg;
        rcVcopy(mb.orig, orig);
        mb.subTileSize = m_cfg.subTileSize;
        mb.subTilesPerTile = int(GRID_SIZE / m_cfg.cellSize + 0.5f) / m_cfg.subTileSize;
        mb.borderSize = m_cfg.walkableRadius + 3;

        unsigned hw = m_cfg.threads > 0 ? unsigned(m_cfg.threads)
                                        : std::thread::hardware_concurrency();
        if (hw == 0)
        {
            hw = 1;   // hardware_concurrency is allowed to say "no idea"
        }
        const unsigned workers = unsigned(std::min<size_t>(hw, grids.size()));

        std::atomic<size_t> nextTile{0};
        std::atomic<int> written{0};
        std::atomic<int> finished{0};

        // Tiles are handed out one at a time rather than sliced up front: their cost
        // varies by an order of magnitude (a dense city tile against an empty coast), so
        // a fixed partition would leave most threads idle behind the slowest slice.
        const auto worker = [&]()
        {
            for (;;)
            {
                const size_t i = nextTile.fetch_add(1, std::memory_order_relaxed);
                if (i >= grids.size())
                {
                    break;
                }

                if (bakeTile(mb, grids[i].first, grids[i].second))
                {
                    written.fetch_add(1, std::memory_order_relaxed);
                }

                const int n = finished.fetch_add(1, std::memory_order_relaxed) + 1;
                if (n % PROGRESS_EVERY == 0)
                {
                    // Deliberately unlocked: one character per 10 tiles cannot tear, and
                    // the marks carry no order, so interleaving costs nothing.
                    std::cout << '*' << std::flush;
                }
            }
        };

        std::vector<std::thread> pool;
        pool.reserve(workers);
        for (unsigned t = 0; t < workers; ++t)
        {
            pool.emplace_back(worker);
        }
        for (auto& t : pool)
        {
            t.join();
        }

        std::cout << std::endl;
        return written.load(std::memory_order_relaxed);
    }

    int NavMeshBuilder::bakeAll(long mapFilter)
    {
        std::error_code ec;
        std::filesystem::create_directories(m_outDir, ec);

        if (!std::filesystem::is_directory(m_tileDir, ec))
        {
            std::cerr << "[nav] tile directory not found: " << m_tileDir << "\n";
            return -1;
        }

        // Discover the baked tiles: t_<map>_<gx>_<gy>.tile
        std::map<uint32_t, std::vector<std::pair<int, int>>> maps;
        for (const auto& entry : std::filesystem::directory_iterator(m_tileDir, ec))
        {
            if (!entry.is_regular_file())
            {
                continue;
            }
            const std::string name = entry.path().filename().string();
            unsigned mapId = 0;
            int gx = 0;
            int gy = 0;
            if (std::sscanf(name.c_str(), "t_%u_%d_%d.tile", &mapId, &gx, &gy) != 3)
            {
                continue;   // w_<map>.tile (global WMO) and anything else
            }
            if (mapFilter >= 0 && long(mapId) != mapFilter)
            {
                continue;
            }
            maps[mapId].emplace_back(gx, gy);
        }

        if (maps.empty())
        {
            std::cerr << "[nav] no .tile files found in " << m_tileDir
                      << " -- bake the `tile` component first\n";
            return -1;
        }

        int total = 0;
        for (auto& [mapId, grids] : maps)
        {
            std::sort(grids.begin(), grids.end());
            // No newline: bakeMap trails one '*' per PROGRESS_EVERY tiles on this line and
            // terminates it.
            std::cout << "  nav map " << mapId << " (" << grids.size() << " tiles) "
                      << std::flush;
            total += bakeMap(mapId, grids);
        }
        return total;
    }

} // namespace world::nav
