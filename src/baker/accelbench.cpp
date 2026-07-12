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

// mangos-accelbench -- which spatial structure should answer WMO collision?
//
// This is the tool that retired Blizzard's authored BSP. The client ships one per WMO
// group (MOBN/MOBR) and the runtime used to traverse it, which sounded principled but
// measured badly: its nodes carry no bounding box, so a ray that misses a group still
// descends that group's tree, and it indexes EVERY face when only ~40% of them collide.
// A binned-SAH BVH over the collidable faces alone beat it decisively, and is what the
// runtime (WmoModel) now bakes and ships.
//
// The tool remains as the regression check behind that decision: it rebuilds a BVH, a BIH
// and a kd-tree over each WMO's soup and re-measures them, with BRUTE FORCE as the
// arbiter -- the only reference that assumes nothing. "BVH(shipped)" is the one actually
// baked into the tile; the rest keep the choice honest as the data changes.
//
// Rays are cast in the WMO's own model space, exactly as the runtime casts them, in two
// mixes:
//   * "floor"   -- straight down from above the model. What GetHeight does; the query
//                  the server makes far more than any other.
//   * "segment" -- random chords across the model. What line-of-sight does.
//
//   mangos-accelbench <tileDir> <mapId> [rays]

#include "terrain/Accelerators.hpp"
#include "terrain/TileSerializer.hpp"
#include "terrain/Terrain.hpp"
#include "terrain/WmoModel.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <vector>

using namespace world::terrain;
using Clock = std::chrono::steady_clock;

namespace
{
    struct Ray
    {
        Vec3 o, d;
        float tMax;
    };

    double ms(Clock::time_point a, Clock::time_point b)
    {
        return std::chrono::duration<double, std::milli>(b - a).count();
    }

    struct Result
    {
        double buildMs = 0;
        double queryMs = 0;
        size_t bytes = 0;
        size_t nodes = 0;
        int depth = 0;
        long long mismatches = 0;
        double worstDelta = 0;
    };

    void report(const char *name, const Result &r, double refQueryMs)
    {
        char spd[32];
        std::snprintf(spd, sizeof spd, "%.2fx", refQueryMs / (r.queryMs > 0 ? r.queryMs : 1));
        std::printf("  %-13s build %8.1fms  query %8.2fms (%-7s) %7.1f MB %9zu nodes  d=%-3d"
                    "  mismatch %lld (worst %.4f)\n",
                    name, r.buildMs, r.queryMs, spd, r.bytes / 1048576.0, r.nodes, r.depth,
                    r.mismatches, r.worstDelta);
    }
} // namespace

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        std::fprintf(stderr, "usage: mangos-accelbench <tileDir> <mapId> [rays]\n");
        return 2;
    }
    const std::string dir = argv[1];
    const int mapId = std::atoi(argv[2]);
    const int nRays = argc > 3 ? std::atoi(argv[3]) : 20000;

    // The map's unique WMO models (a building placed 50 times is ONE model).
    std::vector<std::shared_ptr<const WmoModel>> models;
    std::set<const void *> seen;
    auto harvest = [&](const std::shared_ptr<const TerrainTile> &tile)
    {
        if (!tile)
        {
            return;
        }
        for (const auto &inst : tile->instances)
        {
            auto wmo = std::dynamic_pointer_cast<const WmoModel>(inst.model);
            if (wmo && !wmo->empty() && seen.insert(wmo.get()).second)
            {
                models.push_back(wmo);
            }
        }
    };
    for (int tx = 0; tx < 64; ++tx)
    {
        for (int ty = 0; ty < 64; ++ty)
        {
            char path[512];
            std::snprintf(path, sizeof path, "%s/t_%d_%d_%d.tile", dir.c_str(), mapId, tx, ty);
            harvest(readTile(path));
        }
    }
    {
        char gpath[512];
        std::snprintf(gpath, sizeof gpath, "%s/w_%d.tile", dir.c_str(), mapId);
        harvest(readTile(gpath));
    }
    if (models.empty())
    {
        std::printf("map %d: no WMO models found under %s\n", mapId, dir.c_str());
        return 1;
    }

    std::printf("=== Acceleration structure bench -- map %d, %zu unique WMOs, %d rays each\n\n",
                mapId, models.size(), nRays);

    Result rBrute, rShipped, rBvh, rBih, rKd;
    long long tris = 0;
    std::mt19937 rng(12345);

    for (const auto &wmo : models)
    {
        // Already exactly the collidable faces, in the shipped BVH's leaf order.
        const TriSoup &soup = wmo->soup();
        if (soup.tris.empty())
        {
            continue;
        }
        tris += static_cast<long long>(soup.tris.size());

        const Aabb &box = wmo->bounds();

        // The BVH the tile shipped: no build cost to charge it, it was baked offline.
        rShipped.bytes += wmo->bvh().bytes();
        rShipped.nodes += wmo->bvh().nodeCount();

        // Rebuild the three for comparison. The BVH permutes the soup it is handed, so it
        // gets its own copy; BIH and kd index the soup without touching it.
        TriSoup bvhSoup = soup;
        Bvh bvh;
        Bih bih;
        KdTree kd;
        const auto t0 = Clock::now();
        bvh.build(bvhSoup, nullptr, 4);
        const auto t1 = Clock::now();
        bih.build(soup, 4);
        const auto t2 = Clock::now();
        kd.build(soup, 8, 28);
        const auto t3 = Clock::now();
        rBvh.buildMs += ms(t0, t1);
        rBih.buildMs += ms(t1, t2);
        rKd.buildMs += ms(t2, t3);

        rBvh.bytes += bvh.bytes();
        rBvh.nodes += bvh.nodeCount();
        rBih.bytes += bih.bytes();
        rBih.nodes += bih.nodeCount();
        rKd.bytes += kd.bytes();
        rKd.nodes += kd.nodeCount();
        rShipped.depth = std::max(rShipped.depth, bvh.maxDepth());
        rBvh.depth = std::max(rBvh.depth, bvh.maxDepth());
        rBih.depth = std::max(rBih.depth, bih.maxDepth());
        rKd.depth = std::max(rKd.depth, kd.maxDepth());

        // Half straight-down floor probes (what GetHeight does), half random chords (what
        // line-of-sight does).
        std::uniform_real_distribution<float> ux(box.lo.x, box.hi.x);
        std::uniform_real_distribution<float> uy(box.lo.y, box.hi.y);
        std::uniform_real_distribution<float> uz(box.lo.z, box.hi.z);
        const float span = std::max({box.hi.x - box.lo.x, box.hi.y - box.lo.y,
                                     box.hi.z - box.lo.z, 1.f});

        std::vector<Ray> rays;
        rays.reserve(nRays);
        for (int i = 0; i < nRays; ++i)
        {
            if (i % 2 == 0)
            {
                rays.push_back(
                    {Vec3{ux(rng), uy(rng), box.hi.z + 1.f}, Vec3{0, 0, -1}, span * 2.f});
            }
            else
            {
                const Vec3 a{ux(rng), uy(rng), uz(rng)};
                const Vec3 b{ux(rng), uy(rng), uz(rng)};
                rays.push_back({a, Vec3{b.x - a.x, b.y - a.y, b.z - a.z}, 1.f});
            }
        }

        std::vector<float> refT(rays.size());
        auto run = [&](Result &r, auto &&fn, bool isRef)
        {
            const auto s = Clock::now();
            for (size_t i = 0; i < rays.size(); ++i)
            {
                auto h = fn(rays[i]);
                const float v = h ? *h : -1.f;
                if (isRef)
                {
                    refT[i] = v;
                }
                else if (std::fabs(v - refT[i]) > 1e-2f)
                {
                    ++r.mismatches;
                    r.worstDelta = std::max<double>(r.worstDelta, std::fabs(v - refT[i]));
                }
            }
            r.queryMs += ms(s, Clock::now());
        };

        // Ground truth is BRUTE FORCE over the same soup -- the only arbiter that assumes
        // nothing. Scoring the trees against whichever one we happen to ship would defeat
        // the point of the exercise.
        auto brute = [&](const Ray &r) -> std::optional<float>
        {
            float best = r.tMax;
            for (uint32_t i = 0; i < soup.tris.size(); ++i)
            {
                if (auto t = rayTri(r.o, r.d, soup.at(i)))
                {
                    if (*t >= 0.f && *t < best)
                    {
                        best = *t;
                    }
                }
            }
            return best < r.tMax ? std::optional<float>(best) : std::nullopt;
        };

        run(rBrute, brute, true);
        run(rShipped, [&](const Ray &r) { return wmo->raycastNearest(r.o, r.d, r.tMax); }, false);
        run(rBvh, [&](const Ray &r) { return bvh.raycast(bvhSoup, r.o, r.d, r.tMax); }, false);
        run(rBih, [&](const Ray &r) { return bih.raycast(soup, r.o, r.d, r.tMax); }, false);
        run(rKd, [&](const Ray &r) { return kd.raycast(soup, r.o, r.d, r.tMax); }, false);
    }

    std::printf("  %lld collidable triangles"
                " (the render-only ones are no longer baked at all)\n\n", tris);

    report("Brute", rBrute, rShipped.queryMs);
    report("BVH(shipped)", rShipped, rShipped.queryMs);
    report("BVH(rebuilt)", rBvh, rShipped.queryMs);
    report("BIH", rBih, rShipped.queryMs);
    report("KdTree", rKd, rShipped.queryMs);

    std::printf("\n  speed is relative to the BVH the tile ships (WmoModel).\n"
                "  mismatch = rays whose nearest hit differs from BRUTE FORCE by >0.01."
                " It must be 0.\n");
    return 0;
}
