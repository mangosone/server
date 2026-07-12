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

// mangos-bvhdebug -- bisects a BVH nearest-hit mismatch down to the node that causes it.
//
// mangos-accelbench TELLS you the BVH disagrees with brute force ("mismatch 10"); it cannot
// tell you WHY. This does. Brute force is the oracle: it names the ray and the exact triangle
// it hit. We then locate the LEAF that owns that triangle, reconstruct the root->leaf path,
// and walk it asking two independent questions at every node:
//
//   1. does this node's box actually CONTAIN the hit point?   no => the BUILD is wrong
//                                                                   (a box that fails to
//                                                                   bound its own triangles)
//   2. does intersectsRay() accept this node for this ray?    no => the SLAB TEST is wrong
//
// The first node answering "yes" to (1) and "no" to (2) is the bug, and the tool then replays
// that node's slab arithmetic per axis at full float precision so the mechanism is visible
// rather than inferred. That separation is what matters: "the tree is built wrong" and "the
// tree is queried wrong" look identical from the outside and have nothing in common.
//
// It found exactly one real bug on first use, which is a fair description of what it is for:
// a downward floor probe whose x sat 0.141 mm outside a node's x-slab with dx == 0, rejected
// by an exact box test even though rayTri's 1e-5 barycentric slop accepted the triangle
// inside it. See Aabb::intersectsRay, which is now padded.
//
//   mangos-bvhdebug <tileDir> <mapId> [rays]
//
// Reports at most the first 6 mismatches, then stops -- they are nearly always the same bug.

#include "terrain/Accelerators.hpp"
#include "terrain/Geometry.hpp"
#include "terrain/TileSerializer.hpp"
#include "terrain/Terrain.hpp"
#include "terrain/WmoModel.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <vector>

using namespace world::terrain;

namespace
{
    struct Ray
    {
        Vec3 o, d;
        float tMax;
    };

    bool boxContains(const Aabb& b, const Vec3& p, float eps)
    {
        return p.x >= b.lo.x - eps && p.x <= b.hi.x + eps &&
               p.y >= b.lo.y - eps && p.y <= b.hi.y + eps &&
               p.z >= b.lo.z - eps && p.z <= b.hi.z + eps;
    }

    void printBox(const Aabb& b)
    {
        std::printf("[%.3f %.3f %.3f .. %.3f %.3f %.3f]",
                    b.lo.x, b.lo.y, b.lo.z, b.hi.x, b.hi.y, b.hi.z);
    }
}

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::fprintf(stderr, "usage: mangos-bvhdebug <tileDir> <mapId> [rays]\n");
        return 2;
    }
    const std::string dir = argv[1];
    const int mapId = std::atoi(argv[2]);
    const int nRays = argc > 3 ? std::atoi(argv[3]) : 4000;

    std::vector<std::shared_ptr<const WmoModel>> models;
    std::set<const void*> seen;
    auto harvest = [&](const std::shared_ptr<const TerrainTile>& tile)
    {
        if (!tile) { return; }
        for (const auto& inst : tile->instances)
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
    std::printf("map %d: %zu unique WMOs, %d rays each\n\n", mapId, models.size(), nRays);

    std::mt19937 rng(12345);
    int reported = 0;
    long long totalMismatch = 0;

    for (size_t wi = 0; wi < models.size() && reported < 6; ++wi)
    {
        // Build our own BVH over our own copy, and run brute force over THE SAME copy, so
        // triangle indices refer to one and the same soup.
        TriSoup soup = models[wi]->soup();
        if (soup.tris.empty()) { continue; }
        Bvh bvh;
        bvh.build(soup, nullptr, 4);

        const Aabb& box = models[wi]->bounds();
        std::uniform_real_distribution<float> ux(box.lo.x, box.hi.x);
        std::uniform_real_distribution<float> uy(box.lo.y, box.hi.y);
        std::uniform_real_distribution<float> uz(box.lo.z, box.hi.z);
        const float span = std::max({box.hi.x - box.lo.x, box.hi.y - box.lo.y,
                                     box.hi.z - box.lo.z, 1.f});

        for (int i = 0; i < nRays && reported < 6; ++i)
        {
            Ray r;
            if (i % 2 == 0)
            {
                r = Ray{Vec3{ux(rng), uy(rng), box.hi.z + 1.f}, Vec3{0, 0, -1}, span * 2.f};
            }
            else
            {
                const Vec3 a{ux(rng), uy(rng), uz(rng)};
                const Vec3 b{ux(rng), uy(rng), uz(rng)};
                r = Ray{a, Vec3{b.x - a.x, b.y - a.y, b.z - a.z}, 1.f};
            }

            // Oracle.
            float bt = r.tMax;
            uint32_t bTri = UINT32_MAX;
            for (uint32_t t = 0; t < soup.tris.size(); ++t)
            {
                if (auto h = rayTri(r.o, r.d, soup.at(t)))
                {
                    if (*h >= 0.f && *h < bt) { bt = *h; bTri = t; }
                }
            }
            const bool bruteHit = (bTri != UINT32_MAX);

            auto bh = bvh.raycast(soup, r.o, r.d, r.tMax);
            const float bv = bh ? *bh : -1.f;
            const float bruteV = bruteHit ? bt : -1.f;
            if (std::fabs(bv - bruteV) <= 1e-2f) { continue; }

            ++totalMismatch;
            if (!bruteHit) { continue; }   // BVH found something brute did not: different bug
            ++reported;

            const Vec3 hit{r.o.x + r.d.x * bt, r.o.y + r.d.y * bt, r.o.z + r.d.z * bt};
            std::printf("=== MISMATCH #%d  wmo=%zu ray=%d  (%s)\n", reported, wi, i,
                        (i % 2 == 0) ? "floor probe" : "chord");
            std::printf("  o=(%.3f %.3f %.3f) d=(%.6f %.6f %.6f) tMax=%.3f\n",
                        r.o.x, r.o.y, r.o.z, r.d.x, r.d.y, r.d.z, r.tMax);
            std::printf("  brute t=%.5f tri=%u  hitPt=(%.3f %.3f %.3f)\n", bt, bTri,
                        hit.x, hit.y, hit.z);
            std::printf("  bvh   %s\n", bh ? "hit" : "MISS (returned nullopt)");

            // Locate the leaf owning bTri, then the root->leaf path (parents by scan).
            const auto& nodes = bvh.nodes();
            std::vector<int> parent(nodes.size(), -1);
            for (size_t n = 0; n < nodes.size(); ++n)
            {
                if (nodes[n].left >= 0)
                {
                    parent[nodes[n].left] = static_cast<int>(n);
                    parent[nodes[n].right] = static_cast<int>(n);
                }
            }
            int leaf = -1;
            for (size_t n = 0; n < nodes.size(); ++n)
            {
                const auto& nd = nodes[n];
                if (nd.left < 0 && bTri >= nd.first && bTri < nd.first + nd.count)
                {
                    leaf = static_cast<int>(n);
                    break;
                }
            }
            if (leaf < 0)
            {
                std::printf("  !! triangle %u belongs to NO leaf -- build lost it\n\n", bTri);
                continue;
            }

            std::vector<int> path;
            for (int n = leaf; n >= 0; n = parent[n]) { path.push_back(n); }
            std::reverse(path.begin(), path.end());

            auto inv = [](float v) { return std::fabs(v) > 1e-9f ? 1.f / v : 1e30f; };
            const Vec3 invDir{inv(r.d.x), inv(r.d.y), inv(r.d.z)};

            std::printf("  root->leaf path (%zu nodes):\n", path.size());
            for (size_t k = 0; k < path.size(); ++k)
            {
                const auto& nd = nodes[path[k]];
                const bool contains = boxContains(nd.box, hit, 1e-3f);
                const bool accepts = nd.box.intersectsRay(r.o, invDir, r.tMax);
                std::printf("    [%2zu] node=%-8d %s  containsHit=%-3s  intersectsRay=%-3s  ",
                            k, path[k], nd.left < 0 ? "LEAF" : "    ",
                            contains ? "yes" : "NO", accepts ? "yes" : "NO");
                printBox(nd.box);
                std::printf("\n");
                if (contains && !accepts)
                {
                    std::printf("    ^^^^ SLAB TEST REJECTS A BOX THAT CONTAINS THE HIT POINT\n");
                    // Replay the slab test per axis, at full precision, so the mechanism is
                    // visible rather than inferred.
                    float t0 = 0.f, t1 = r.tMax;
                    for (int a = 0; a < 3; ++a)
                    {
                        const float oa = (&r.o.x)[a], id = (&invDir.x)[a];
                        const float loa = (&nd.box.lo.x)[a], hia = (&nd.box.hi.x)[a];
                        float ta = (loa - oa) * id, tb = (hia - oa) * id;
                        if (ta > tb) { std::swap(ta, tb); }
                        const float p0 = std::max(t0, ta), p1 = std::min(t1, tb);
                        std::printf("        axis %d: o=%.9g lo=%.9g hi=%.9g  d=%.9g invD=%.9g\n",
                                    a, oa, loa, hia, (&r.d.x)[a], id);
                        std::printf("                lo-o=%.9g  ta=%.9g tb=%.9g -> t0=%.9g t1=%.9g%s\n",
                                    loa - oa, ta, tb, p0, p1, (p0 > p1) ? "   <<< REJECT" : "");
                        t0 = p0; t1 = p1;
                        if (t0 > t1) { break; }
                    }
                }
                if (!contains)
                {
                    std::printf("    ^^^^ BOX DOES NOT CONTAIN THE HIT -- BUILD BUG\n");
                }
            }
            std::printf("\n");
        }
    }

    std::printf("total mismatches seen: %lld\n", totalMismatch);
    return 0;
}
