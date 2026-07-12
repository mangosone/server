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

// mangos-height-check — batch terrain floor/liquid spot-check against the baked .tile
// cache. It queries the SAME runtime path the daemon uses (FusedTerrain, reading the
// offline-baked fused tiles — NO MPQ opened) and scores each probe against an expected
// floor height. Use it to validate a fresh bake, to debug a "creature falls through /
// floats" report (feed it the suspect coordinates), or as a health check.
//
//   mangos-height-check <tileDir> [probesFile]
//
// tileDir    : the baker's <dest>/tiles directory (t_<map>_<tx>_<ty>.tile / w_<map>.tile)
// probesFile : CSV of `name,map,x,y,z,expectedFloor` lines (# / // comments allowed);
//              defaults to "probes.txt" in the working directory.

#include "FusedTerrain.h"
#include "terrain/Terrain.hpp"  // LiquidInfo / LiquidKind, tileIndex

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

using world::terrain::LiquidInfo;
using world::terrain::LiquidKind;

namespace
{
    struct Probe
    {
        std::string name;
        uint32_t    map;
        float       x, y, z;
        float       expected;
    };

    int g_fail = 0;  // floor-vs-expected failures

    // ANSI colors
    const char* const RED   = "\033[31m";
    const char* const GREEN = "\033[32m";
    const char* const CYAN  = "\033[36m";
    const char* const RESET = "\033[0m";

    using Clock = std::chrono::steady_clock;

    double elapsedUs(Clock::time_point a, Clock::time_point b)
    {
        return std::chrono::duration<double, std::micro>(b - a).count();
    }

    // Microseconds for fast queries, milliseconds once a tile load dominates.
    std::string fmtDur(double us)
    {
        char b[24];
        if (us >= 1000.0) { std::snprintf(b, sizeof b, "%.2fms", us / 1000.0); }
        else              { std::snprintf(b, sizeof b, "%.0fus", us); }
        return b;
    }

    const char* liquidKindName(LiquidKind k)
    {
        switch (k)
        {
            case LiquidKind::None:  return "none";
            case LiquidKind::Water: return "water";
            case LiquidKind::Ocean: return "ocean";
            case LiquidKind::Magma: return "magma";
            case LiquidKind::Slime: return "slime";
        }
        return "?";
    }

    std::vector<Probe> loadProbes(const std::string& probesPath)
    {
        std::vector<Probe> probes;
        std::ifstream file(probesPath);
        if (!file.is_open())
        {
            std::printf("  [ERROR] Could not open probes file: %s\n", probesPath.c_str());
            std::printf("  Pass a probes file: mangos-height-check <tileDir> <probesFile>\n");
            return probes;
        }

        std::string line;
        int lineNum = 0;
        while (std::getline(file, line))
        {
            ++lineNum;
            if (line.empty() || line[0] == '#' || line[0] == '/')
            {
                continue;
            }

            Probe p;
            size_t pos = 0;
            auto nextToken = [&]() -> std::string
            {
                size_t n = line.find(',', pos);
                if (n == std::string::npos) { n = line.size(); }
                std::string token = line.substr(pos, n - pos);
                pos = n + 1;
                return token;
            };

            try
            {
                p.name     = nextToken();
                p.map      = static_cast<uint32_t>(std::stoul(nextToken()));
                p.x        = std::stof(nextToken());
                p.y        = std::stof(nextToken());
                p.z        = std::stof(nextToken());
                p.expected = std::stof(nextToken());
                probes.push_back(std::move(p));
            }
            catch (...)
            {
                std::printf("  [WARNING] Failed to parse line %d\n", lineNum);
            }
        }

        std::printf("  Loaded %zu probes from %s\n", probes.size(), probesPath.c_str());
        return probes;
    }

    // Render the liquid result into a compact, fixed-width field. With a floor known,
    // append the liquid depth above it (a quick "is this column submerged?" read).
    std::string formatLiquid(bool hasLiquid, const LiquidInfo& liq, bool hasFloor, float floor)
    {
        if (!hasLiquid)
        {
            return "none";
        }
        char buf[64];
        if (hasFloor)
        {
            std::snprintf(buf, sizeof buf, "%s@%.3f (depth=%+.3f)",
                          liquidKindName(liq.kind), liq.level, liq.level - floor);
        }
        else
        {
            std::snprintf(buf, sizeof buf, "%s@%.3f", liquidKindName(liq.kind), liq.level);
        }
        return buf;
    }

    // One probe row: floor (scored vs expected), liquid, the ADT tile it lands in, and
    // the query time. The first probe to touch a tile pays its flat-cache read; later
    // probes on the same tile are warm.
    void report(const Probe& p, FusedTerrain& terrain, double& accUs)
    {
        const int tx = world::terrain::tileIndex(p.x);
        const int ty = world::terrain::tileIndex(p.y);

        const auto t0 = Clock::now();
        // GetHeightStatic, not the raw GetHeight: this is the call mangosd actually makes,
        // and its second pass is what finds the ground for a probe that sits slightly
        // UNDER the surface. Probing with a bare GetHeight (searchUp 2, maxDrop 200)
        // reported "no floor" for every such point and scored it a failure, which said
        // nothing about the bake.
        const float floorZ = terrain.GetHeightStatic(p.x, p.y, p.z);
        const bool hasFloor = floorZ > INVALID_HEIGHT;
        LiquidInfo liq;
        const bool hasLiquid = terrain.GetLiquid(p.x, p.y, p.z, liq);
        const double us = elapsedUs(t0, Clock::now());
        accUs += us;

        bool        floorOk;
        std::string floorStr;
        if (!hasFloor)
        {
            floorOk  = false;
            floorStr = "floor=(none)";
        }
        else
        {
            const float diff = std::fabs(floorZ - p.expected);
            floorOk = diff < 1.5f;
            char b[96];
            std::snprintf(b, sizeof b, "floor=%.3f exp=%.3f d=%.3f", floorZ, p.expected, diff);
            floorStr = b;
        }
        if (!floorOk) { ++g_fail; }

        std::printf("  %s[%s]%s %-28s tile=(%d,%d)  %-34s  liquid=%s%-30s%s  %s\n",
                    floorOk ? GREEN : RED, floorOk ? "PASS" : "FAIL", RESET,
                    p.name.c_str(), tx, ty, floorStr.c_str(),
                    CYAN, formatLiquid(hasLiquid, liq, hasFloor, floorZ).c_str(), RESET,
                    fmtDur(us).c_str());
    }
} // namespace

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr, "usage: mangos-height-check <tileDir> [probesFile]\n");
        return 2;
    }
    const std::string tileDir    = argv[1];
    const std::string probesPath = argc > 2 ? argv[2] : "probes.txt";

    std::printf("=== Terrain Height + Liquid Check (baked tiles, no MPQ) ===\n");
    std::printf("  tileDir    : %s\n", tileDir.c_str());
    std::printf("  probesFile : %s\n", probesPath.c_str());

    auto kProbes = loadProbes(probesPath);
    if (kProbes.empty())
    {
        std::printf("\n  [ERROR] No probes loaded.\n");
        return 1;
    }

    // One FusedTerrain per map (each owns its own tile grid), exactly as the daemon
    // keeps one per loaded map. A tile with no cache file resolves to "no data"
    // (GetHeight returns false), same as the daemon.
    FusedTerrain::SetTileDir(tileDir);
    std::map<uint32_t, std::unique_ptr<FusedTerrain>> perMap;
    auto terrainFor = [&](uint32_t map) -> FusedTerrain&
    {
        auto& slot = perMap[map];
        if (!slot)
        {
            slot = std::make_unique<FusedTerrain>(map);
        }
        return *slot;
    };

    std::printf("\nRunning terrain height + liquid checks...\n");
    double accUs = 0.0;
    const auto runStart = Clock::now();
    for (const auto& p : kProbes)
    {
        report(p, terrainFor(p.map), accUs);
    }
    const double runUs = elapsedUs(runStart, Clock::now());

    std::printf("\n");
    if (g_fail == 0)
    {
        std::printf("%sRESULT: all %zu floor checks passed%s\n", GREEN, kProbes.size(), RESET);
    }
    else
    {
        std::printf("%sRESULT: %d floor failure(s) of %zu%s\n", RED, g_fail, kProbes.size(), RESET);
    }

    std::printf("\nTimings (%zu probes):\n", kProbes.size());
    std::printf("  cache query total (disk read): %s\n", fmtDur(accUs).c_str());
    std::printf("  wall-clock for the run loop  : %s\n", fmtDur(runUs).c_str());

    return g_fail ? 1 : 0;
}
