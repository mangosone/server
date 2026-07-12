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

// mangos-losbench -- measures how a change to the LINE-OF-SIGHT ray changes the answer.
//
// The height probes (mangos-height-check) drive the downward column query and are blind
// to segment queries, so they cannot see a LoS change at all. This tool closes that gap.
//
// The sample is not synthetic: endpoints are real creature spawns from probes.csv, paired
// with other spawns on the same map within a plausible sight range, and lifted by the same
// +2.0 yards WorldObject::IsWithinLOS applies. So a pair here is a sightline the server
// actually evaluates.
//
// Both rays are computed by ONE binary against ONE set of tiles, which is the whole point:
// no control build to get stale, no second tree to disagree with. The engine now takes the
// caller's segment verbatim, so the retired behaviour is reproduced by simply lifting both
// endpoints by `--lift` before asking.
//
//   mangos-losbench <tileDir> <probes.csv> [--lift L] [--range R] [--max N]
//
//   --lift  L   the retired engine-side lift, in yards          (default 1.0)
//   --range R   pair spawns within R yards of each other        (default 40)
//   --max   N   stop after N pairs                              (default 200000)

#include "FusedTerrain.h"
#include "terrain/Terrain.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
    const char* const RED = "\033[31m";
    const char* const GREEN = "\033[32m";
    const char* const CYAN = "\033[36m";
    const char* const RESET = "\033[0m";

    struct Spawn
    {
        float x, y, z;
    };

    // One map's tally of how the two rays disagreed.
    struct Tally
    {
        uint64_t pairs = 0;
        uint64_t blockedOld = 0;
        uint64_t blockedNew = 0;
        uint64_t nowBlocked = 0;    // old said clear, verbatim ray says blocked
        uint64_t nowClear = 0;      // old said blocked, verbatim ray says clear
    };

    // probes.csv is "guid, map, x, y, z, expectedFloor" -- we want the position and the
    // map; the expected floor is the height probes' business, not ours.
    bool loadSpawns(const std::string& path, std::map<uint32_t, std::vector<Spawn>>& out)
    {
        std::ifstream f(path);
        if (!f)
        {
            return false;
        }

        std::string line;
        while (std::getline(f, line))
        {
            if (line.empty() || line[0] == '#')
            {
                continue;
            }
            for (char& c : line)
            {
                if (c == ',')
                {
                    c = ' ';
                }
            }
            std::istringstream is(line);
            double guid = 0.0;
            uint32_t map = 0;
            Spawn s{};
            if (!(is >> guid >> map >> s.x >> s.y >> s.z))
            {
                continue;
            }
            out[map].push_back(s);
        }
        return true;
    }

    bool blocked(float frac)
    {
        return frac <= 1.0f;
    }
}

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::printf("usage: mangos-losbench <tileDir> <probes.csv> "
                    "[--lift L] [--range R] [--max N]\n");
        return 2;
    }

    const std::string tileDir = argv[1];
    const std::string probesPath = argv[2];

    float lift = 1.0f;
    float range = 40.0f;
    uint64_t maxPairs = 200000;

    for (int i = 3; i + 1 < argc; i += 2)
    {
        const std::string a = argv[i];
        if (a == "--lift")
        {
            lift = static_cast<float>(std::atof(argv[i + 1]));
        }
        else if (a == "--range")
        {
            range = static_cast<float>(std::atof(argv[i + 1]));
        }
        else if (a == "--max")
        {
            maxPairs = std::strtoull(argv[i + 1], nullptr, 10);
        }
    }

    std::map<uint32_t, std::vector<Spawn>> spawns;
    if (!loadSpawns(probesPath, spawns))
    {
        std::printf("%serror: cannot read %s%s\n", RED, probesPath.c_str(), RESET);
        return 1;
    }

    FusedTerrain::SetTileDir(tileDir);

    // The eye height the server itself applies to BOTH endpoints before it ever reaches
    // the terrain (WorldObject::IsWithinLOS). Without it we would be benchmarking a ray
    // the game never casts.
    const float kEye = 2.0f;

    std::printf("tiles : %s\n", tileDir.c_str());
    std::printf("sample: creature spawns from %s\n", probesPath.c_str());
    std::printf("ray   : eye +%.1f (as IsWithinLOS), pairs within %.0f yd, "
                "retired lift = +%.1f\n\n", kEye, range, lift);

    Tally total;
    std::map<uint32_t, Tally> perMap;

    for (const auto& [mapId, list] : spawns)
    {
        if (total.pairs >= maxPairs)
        {
            break;
        }

        // Bucket spawns on a `range`-sized XY lattice so pairing is local instead of
        // quadratic: a partner can only be in this cell or one of the 8 around it.
        std::unordered_map<uint64_t, std::vector<uint32_t>> cells;
        auto key = [&](float x, float y)
        {
            const int64_t cx = static_cast<int64_t>(std::floor(x / range));
            const int64_t cy = static_cast<int64_t>(std::floor(y / range));
            return (static_cast<uint64_t>(cx + 100000) << 32) ^
                   static_cast<uint64_t>(cy + 100000);
        };
        for (uint32_t i = 0; i < list.size(); ++i)
        {
            cells[key(list[i].x, list[i].y)].push_back(i);
        }

        FusedTerrain terrain(mapId);
        Tally t;

        for (uint32_t i = 0; i < list.size() && total.pairs < maxPairs; ++i)
        {
            const Spawn& a = list[i];
            const int64_t cx = static_cast<int64_t>(std::floor(a.x / range));
            const int64_t cy = static_cast<int64_t>(std::floor(a.y / range));

            for (int64_t dx = -1; dx <= 1 && total.pairs < maxPairs; ++dx)
            {
                for (int64_t dy = -1; dy <= 1 && total.pairs < maxPairs; ++dy)
                {
                    const uint64_t k = (static_cast<uint64_t>(cx + dx + 100000) << 32) ^
                                       static_cast<uint64_t>(cy + dy + 100000);
                    auto it = cells.find(k);
                    if (it == cells.end())
                    {
                        continue;
                    }

                    for (uint32_t j : it->second)
                    {
                        if (j <= i || total.pairs >= maxPairs)
                        {
                            continue;   // each unordered pair once
                        }
                        const Spawn& b = list[j];

                        const float ddx = b.x - a.x;
                        const float ddy = b.y - a.y;
                        if (ddx * ddx + ddy * ddy > range * range)
                        {
                            continue;
                        }

                        // The ray the server casts today, verbatim...
                        const float fNew = terrain.NearestHitFraction(
                            a.x, a.y, a.z + kEye, b.x, b.y, b.z + kEye);
                        // ...and the same ray as the retired code saw it, lifted.
                        const float fOld = terrain.NearestHitFraction(
                            a.x, a.y, a.z + kEye + lift, b.x, b.y, b.z + kEye + lift);

                        const bool bNew = blocked(fNew);
                        const bool bOld = blocked(fOld);

                        ++t.pairs;
                        ++total.pairs;
                        if (bOld) { ++t.blockedOld; }
                        if (bNew) { ++t.blockedNew; }
                        if (!bOld && bNew) { ++t.nowBlocked; }
                        if (bOld && !bNew) { ++t.nowClear; }
                    }
                }
            }
        }

        if (t.pairs)
        {
            perMap[mapId] = t;
            total.blockedOld += t.blockedOld;
            total.blockedNew += t.blockedNew;
            total.nowBlocked += t.nowBlocked;
            total.nowClear += t.nowClear;
        }
    }

    if (!total.pairs)
    {
        std::printf("%sno sightline pairs found -- wrong tileDir, or range too small%s\n",
                    RED, RESET);
        return 1;
    }

    const uint64_t flips = total.nowBlocked + total.nowClear;
    auto pct = [&](uint64_t n) { return 100.0 * double(n) / double(total.pairs); };

    std::printf("%-10s %10s %10s %10s %10s %10s\n",
                "map", "pairs", "blk(old)", "blk(new)", "+blocked", "+clear");
    for (const auto& [mapId, t] : perMap)
    {
        if (!t.nowBlocked && !t.nowClear)
        {
            continue;   // only maps where the two rays actually disagreed
        }
        std::printf("%-10u %10llu %10llu %10llu %10llu %10llu\n", mapId,
                    (unsigned long long)t.pairs, (unsigned long long)t.blockedOld,
                    (unsigned long long)t.blockedNew, (unsigned long long)t.nowBlocked,
                    (unsigned long long)t.nowClear);
    }

    std::printf("\n%sTOTAL%s  %llu sightlines over %zu maps\n",
                CYAN, RESET, (unsigned long long)total.pairs, perMap.size());
    std::printf("  blocked, lifted ray (retired) : %llu  (%.2f%%)\n",
                (unsigned long long)total.blockedOld, pct(total.blockedOld));
    std::printf("  blocked, verbatim ray (now)   : %llu  (%.2f%%)\n",
                (unsigned long long)total.blockedNew, pct(total.blockedNew));
    std::printf("  %snewly BLOCKED%s (lift hid a wall) : %llu  (%.3f%%)\n",
                RED, RESET, (unsigned long long)total.nowBlocked, pct(total.nowBlocked));
    std::printf("  %snewly CLEAR%s   (lift saw a wall)  : %llu  (%.3f%%)\n",
                GREEN, RESET, (unsigned long long)total.nowClear, pct(total.nowClear));
    std::printf("\n  sightlines whose answer changed : %llu  (%.3f%%)\n",
                (unsigned long long)flips, pct(flips));
    return 0;
}
