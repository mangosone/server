/**
 * mangos-wmo-probe -- why the terrain answers what it answers.
 *
 * It asks the SERVER'S OWN terrain engine, over the baker's own tiles, and prints the
 * intermediate values the engine has no reason to keep: which WMO instances stood over
 * the column, every surface the downward ray crossed, which group won, and what
 * IsOutdoorWMO then made of its flags. The engine half is deliberately free of the game
 * (FusedTerrain.hpp says so), which is what lets this link `terrain` and answer exactly
 * what mangosd would, with none of the server present.
 *
 * It also reads the CLIENT, through the baker's own MPQ reader and WMO parser, so
 * "the client says X" and "the tile says X" can be compared instead of assumed. That
 * comparison is what cleared the extractor when all of Orgrimmar began reporting
 * indoors: 144 client groups, 144 baked groups, zero flag mismatches. The fault was in
 * the RULE, not the bake -- see IsOutdoorWMO in src/game/WorldHandlers/GridMap.cpp.
 */

#include "ChunkReaders.hpp"
#include "StormLibArchive.hpp"
#include "WmoParser.hpp"
#include "terrain/FusedTerrain.hpp"
#include "terrain/TileSerializer.hpp"
#include "terrain/WmoModel.hpp"

// The on-disk mmtile header and the NAV_* bits are the SERVER's declaration, included
// rather than copied -- the same reason NavMeshBuilder includes them.
#include "MoveMapSharedDefines.h"

#include "DetourNavMesh.h"
#include "DetourNavMeshQuery.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace world::terrain;

namespace
{
    // ------------------------------------------------------------------ dbc ------
    /// Enough of a WDBC reader to resolve a WMOAreaTable triple and name an area. Every
    /// field is four bytes wide, so a field index IS an offset; nothing here needs the
    /// server's DBCFileLoader, and pulling it in would drag `shared` behind it.
    class Dbc
    {
        public:
            bool Load(const std::string& path)
            {
                std::FILE* f = std::fopen(path.c_str(), "rb");
                if (!f)
                {
                    return false;
                }
                if (std::fseek(f, 0, SEEK_END) != 0)
                {
                    std::fclose(f);
                    return false;
                }
                const long size = std::ftell(f);
                if (size <= 20 || std::fseek(f, 0, SEEK_SET) != 0)
                {
                    std::fclose(f);
                    return false;
                }

                m_bytes.resize(size_t(size));
                const bool read =
                    std::fread(m_bytes.data(), 1, m_bytes.size(), f) == m_bytes.size();
                std::fclose(f);
                if (!read || std::memcmp(m_bytes.data(), "WDBC", 4) != 0)
                {
                    return false;
                }

                std::memcpy(&m_records, m_bytes.data() + 4, 4);
                std::memcpy(&m_fields, m_bytes.data() + 8, 4);
                std::memcpy(&m_recordSize, m_bytes.data() + 12, 4);
                std::memcpy(&m_stringSize, m_bytes.data() + 16, 4);

                // A header may claim more than the file holds. Every accessor below
                // indexes on these three numbers without a bounds test, so they are
                // measured against the real length ONCE, here.
                const uint64_t need = uint64_t(20) +
                                      uint64_t(m_records) * m_recordSize + m_stringSize;
                if (m_recordSize < 4 || need > m_bytes.size())
                {
                    m_records = 0;
                    return false;
                }
                m_data = m_bytes.data() + 20;
                m_strings = reinterpret_cast<const char*>(m_data) +
                            size_t(m_records) * m_recordSize;
                return true;
            }

            uint32_t Records() const { return m_records; }

            int32_t Field(uint32_t record, uint32_t field) const
            {
                if (record >= m_records || (field + 1) * 4 > m_recordSize)
                {
                    return 0;
                }
                int32_t v = 0;
                std::memcpy(&v, m_data + size_t(record) * m_recordSize + size_t(field) * 4,
                            4);
                return v;
            }

            std::string Text(uint32_t record, uint32_t field) const
            {
                const uint32_t offset = uint32_t(Field(record, field));
                if (offset >= m_stringSize)
                {
                    return std::string();
                }
                const char* s = m_strings + offset;
                return std::string(s, ::strnlen(s, m_stringSize - offset));
            }

        private:
            std::vector<uint8_t> m_bytes;
            const uint8_t* m_data = nullptr;
            const char* m_strings = nullptr;
            uint32_t m_records = 0, m_fields = 0, m_recordSize = 0, m_stringSize = 0;
    };

    struct WmoArea
    {
        uint32_t flags = 0;
        uint32_t areaId = 0;
    };

    std::map<std::tuple<int32_t, int32_t, int32_t>, WmoArea> g_wmoArea;
    std::map<uint32_t, std::string> g_areaName;
    bool g_haveDbc = false;

    /// Mirrors GridMap.cpp's own reading of the two tables: WMOAreaTable fields are
    /// id, rootId, adtId, groupId, five unused, flags, areaId; AreaTable's name is the
    /// first of its sixteen locale columns.
    void LoadDbc(const std::string& dir)
    {
        Dbc wmo;
        if (wmo.Load(dir + "/WMOAreaTable.dbc"))
        {
            for (uint32_t r = 0; r < wmo.Records(); ++r)
            {
                WmoArea area;
                area.flags = uint32_t(wmo.Field(r, 9));
                area.areaId = uint32_t(wmo.Field(r, 10));
                g_wmoArea[std::make_tuple(wmo.Field(r, 1), wmo.Field(r, 2),
                                          wmo.Field(r, 3))] = area;
            }
            g_haveDbc = true;
        }

        Dbc area;
        if (area.Load(dir + "/AreaTable.dbc"))
        {
            for (uint32_t r = 0; r < area.Records(); ++r)
            {
                g_areaName[uint32_t(area.Field(r, 0))] = area.Text(r, 11);
            }
        }
    }

    const WmoArea* LookupWmoArea(int32_t root, int32_t adt, int32_t group)
    {
        auto it = g_wmoArea.find(std::make_tuple(root, adt, group));
        return it == g_wmoArea.end() ? nullptr : &it->second;
    }

    std::string AreaName(uint32_t areaId)
    {
        auto it = g_areaName.find(areaId);
        return it == g_areaName.end() ? std::string("?") : it->second;
    }

    // -------------------------------------------------------------- the rule ----
    const uint32_t MOGP_FLAG_EXTERIOR = 0x8;
    const uint32_t MOGP_FLAG_INTERIOR = 0x2000;
    const uint32_t MOGP_FLAG_MOUNT_ALLOWED = 0x8000;

    /// When set, MOUNT_ALLOWED is ignored -- the rule as it stood before it was read.
    /// Kept so the regression it caused stays MEASURABLE rather than remembered: run any
    /// mode twice, once with --legacy-flags, and the difference is the fix.
    bool g_legacyFlags = false;

    /// IsOutdoorWMO from src/game/WorldHandlers/GridMap.cpp, and it must stay in step
    /// with it. 2.4.3's AreaTable has no INSIDE/OUTSIDE bits, so there is no atEntry
    /// argument to mirror here.
    bool IsOutdoorWMO(uint32_t mogpFlags, const WmoArea* wmoEntry)
    {
        const uint32_t outdoorBits = g_legacyFlags
                                         ? MOGP_FLAG_EXTERIOR
                                         : (MOGP_FLAG_EXTERIOR | MOGP_FLAG_MOUNT_ALLOWED);
        bool outdoor = (mogpFlags & outdoorBits) != 0;
        if (wmoEntry)
        {
            if (wmoEntry->flags & 4)
            {
                return true;
            }
            if (wmoEntry->flags & 2)
            {
                outdoor = false;
            }
        }
        return outdoor;
    }

    // ---------------------------------------------------------- group bounds ----
    /// vmap decided "am I inside this group" by testing the point against the GROUP's
    /// own bounding box, and WmoModel::Group carries none -- the baker never wrote one.
    /// Recovered here from the group's triangles, which is what vmap's iBound was, so
    /// the older rule can be run against the same tile and compared. It does NOT rescue
    /// Orgrimmar (the group boxes are as tall as the city), which is worth knowing
    /// before anyone re-adds it.
    const std::vector<Aabb>& GroupBounds(const WmoModel* model)
    {
        static std::unordered_map<const WmoModel*, std::vector<Aabb>> cache;
        auto it = cache.find(model);
        if (it != cache.end())
        {
            return it->second;
        }

        std::vector<Aabb> boxes(model->Groups().size());
        const TriSoup& soup = model->Soup();
        const std::vector<uint16_t>& triGroup = model->TriGroups();
        for (size_t t = 0; t < soup.tris.size() && t < triGroup.size(); ++t)
        {
            const uint16_t g = triGroup[t];
            if (g < boxes.size())
            {
                boxes[g].expand(soup.TriBounds(uint32_t(t)));
            }
        }
        return cache.emplace(model, std::move(boxes)).first->second;
    }

    bool Contains(const Aabb& box, const Vec3& p)
    {
        return box.valid() && p.x >= box.lo.x && p.x <= box.hi.x && p.y >= box.lo.y &&
               p.y <= box.hi.y && p.z >= box.lo.z && p.z <= box.hi.z;
    }

    // -------------------------------------------------------------- verdicts ----
    struct Verdict
    {
        bool found = false;
        uint32_t mogpFlags = 0;
        int32_t adtId = 0, rootId = 0, groupId = 0;
        float groundZ = 0.f;
        bool outdoor = true;
    };

    struct Candidate
    {
        const StaticInstance* instance = nullptr;
        const WmoModel* wmo = nullptr;
        bool rejected = false;
        const char* reason = "";
        std::vector<Bvh::Crossing> crossings;
        bool hit = false;
        float hitZ = 0.f;
        uint32_t group = 0, flags = 0;
        bool pointInGroupBox = false;
    };

    /// FusedTerrain::GetAreaInfo, re-run with every intermediate kept. Point mode checks
    /// it against the engine's own answer, so a drift between the two is reported rather
    /// than quietly believed.
    Verdict EngineRule(const TerrainTile* tile, const TerrainTile* global, float x,
                       float y, float z, std::vector<Candidate>* candidates,
                       bool* roofGuardFired)
    {
        const float SEARCH_UP = 2.0f;
        const float MAX_DROP = 300.0f;
        const float ceiling = z + SEARCH_UP;
        const Vec3 originWorld{x, y, ceiling};
        const Vec3 downWorld{0.0f, 0.0f, -1.0f};

        Verdict verdict;
        float bestZ = -std::numeric_limits<float>::max();

        auto scan = [&](const std::vector<StaticInstance>& instances)
        {
            for (const StaticInstance& inst : instances)
            {
                if (!inst.model || inst.model->Kind() != ModelKind::Wmo ||
                    inst.model->Empty())
                {
                    continue;
                }
                const Aabb& box = inst.worldBounds;
                if (!box.coversColumn(x, y))
                {
                    continue;
                }

                Candidate cand;
                cand.instance = &inst;
                cand.wmo = static_cast<const WmoModel*>(inst.model.get());
                if (box.hi.z < z)
                {
                    cand.rejected = true;
                    cand.reason = "roof below the query z";
                }
                else if (box.lo.z > ceiling + 0.1f)
                {
                    cand.rejected = true;
                    cand.reason = "floor above the query z";
                }

                const Vec3 originLocal = inst.xf.worldToLocal(originWorld);
                const Vec3 dirLocal = inst.xf.worldToLocalDirection(downWorld);
                const Vec3 pointLocal = inst.xf.worldToLocal(Vec3{x, y, z});

                if (candidates)
                {
                    cand.wmo->GetBvh().RaycastAll(cand.wmo->Soup(), originLocal, dirLocal,
                                                  MAX_DROP, cand.crossings);
                    std::sort(cand.crossings.begin(), cand.crossings.end(),
                              [](const Bvh::Crossing& a, const Bvh::Crossing& b)
                              { return a.t < b.t; });
                }

                if (auto area = cand.wmo->AreaInfo(originLocal, dirLocal, MAX_DROP))
                {
                    cand.hit = true;
                    cand.hitZ = ceiling - area->t;
                    cand.group = area->groupId;
                    cand.flags = area->mogpFlags;

                    const std::vector<Aabb>& boxes = GroupBounds(cand.wmo);
                    for (size_t g = 0; g < cand.wmo->Groups().size(); ++g)
                    {
                        if (cand.wmo->Groups()[g].groupWmoId == area->groupId)
                        {
                            cand.pointInGroupBox = Contains(boxes[g], pointLocal);
                            break;
                        }
                    }

                    if (!cand.rejected && cand.hitZ <= ceiling && cand.hitZ > bestZ)
                    {
                        bestZ = cand.hitZ;
                        verdict.found = true;
                        verdict.mogpFlags = area->mogpFlags;
                        verdict.groupId = int32_t(area->groupId);
                        verdict.rootId = int32_t(cand.wmo->RootId());
                        verdict.adtId = inst.adtId;
                    }
                }

                if (candidates)
                {
                    candidates->push_back(std::move(cand));
                }
            }
        };

        if (tile)
        {
            scan(tile->instances);
        }
        if (global && global != tile)
        {
            scan(global->instances);
        }

        if (roofGuardFired)
        {
            *roofGuardFired = false;
        }
        if (verdict.found && tile)
        {
            if (auto height = tile->TerrainHeight(x, y))
            {
                if (z + 2.0f > *height && *height > bestZ)
                {
                    verdict.found = false;
                    if (roofGuardFired)
                    {
                        *roofGuardFired = true;
                    }
                }
            }
        }

        verdict.groundZ = bestZ;
        verdict.outdoor =
            verdict.found ? IsOutdoorWMO(verdict.mogpFlags,
                                         LookupWmoArea(verdict.rootId, verdict.adtId,
                                                       verdict.groupId))
                          : true;
        return verdict;
    }

    std::shared_ptr<TerrainTile> LoadTile(const std::string& dir, uint32_t mapId, int tx,
                                          int ty)
    {
        return ReadTile(dir + "/" + TileFileName(mapId, tx, ty));
    }

    void PrintVerdict(const char* label, const Verdict& v)
    {
        if (!v.found)
        {
            std::printf("  %-14s no WMO over this point -> OUTDOORS\n", label);
            return;
        }

        std::printf("  %-14s root=%d adt=%d group=%d mogp=0x%08X%s%s%s\n", label,
                    v.rootId, v.adtId, v.groupId, v.mogpFlags,
                    (v.mogpFlags & MOGP_FLAG_EXTERIOR) ? " EXTERIOR" : "",
                    (v.mogpFlags & MOGP_FLAG_INTERIOR) ? " INTERIOR" : "",
                    (v.mogpFlags & MOGP_FLAG_MOUNT_ALLOWED) ? " MOUNT_ALLOWED" : "");

        const WmoArea* entry = LookupWmoArea(v.rootId, v.adtId, v.groupId);
        if (entry)
        {
            std::printf("  %-14s WMOAreaTable: area %u \"%s\" flags=0x%X\n", "",
                        entry->areaId, AreaName(entry->areaId).c_str(), entry->flags);
        }
        else if (g_haveDbc)
        {
            std::printf("  %-14s WMOAreaTable: no row for this triple\n", "");
        }

        std::printf("  %-14s floor z=%.2f -> %s\n", "", v.groundZ,
                    v.outdoor ? "OUTDOORS" : "INDOORS (outdoors-only spells refused)");
    }

    // ----------------------------------------------------------------- modes ----
    void Point(const std::string& tiles, const std::string& dbc, uint32_t mapId, float x,
               float y, float z, bool verbose)
    {
        (void)dbc;
        const int tx = TileIndex(x), ty = TileIndex(y);
        std::shared_ptr<TerrainTile> tile = LoadTile(tiles, mapId, tx, ty);
        std::shared_ptr<TerrainTile> global =
            ReadTile(tiles + "/" + GlobalWmoFileName(mapId));

        std::printf("\n=== (%.2f, %.2f, %.2f)  map %u  tile %d_%d ===\n", x, y, z, mapId,
                    tx, ty);
        if (!tile)
        {
            std::printf("  no tile there (stale bake? the reader refuses a tile whose "
                        "version is not the current one)\n");
            return;
        }

        auto height = tile->TerrainHeight(x, y);
        std::printf("  terrain height  %s\n",
                    height ? std::to_string(*height).c_str() : "hole / none");
        std::printf("  chunk areaId    %u \"%s\"\n", tile->AreaId(x, y),
                    AreaName(tile->AreaId(x, y)).c_str());

        std::vector<Candidate> candidates;
        bool roofGuard = false;
        const Verdict verdict =
            EngineRule(tile.get(), global.get(), x, y, z, &candidates, &roofGuard);

        // The engine's own answer, so the replica above cannot drift unnoticed.
        FusedTerrain::SetTileDir(tiles);
        FusedTerrain terrain(mapId);
        uint32_t flags = 0;
        int32_t adtId = 0, rootId = 0, groupId = 0;
        float groundZ = 0.f;
        const bool found =
            terrain.GetAreaInfo(x, y, z, flags, adtId, rootId, groupId, groundZ);
        if (found != verdict.found || (found && flags != verdict.mogpFlags))
        {
            std::printf("  !! this tool disagrees with FusedTerrain::GetAreaInfo "
                        "(engine: found=%d flags=0x%08X) -- the replica has drifted\n",
                        int(found), flags);
        }

        std::printf("  WMO candidates over the column: %zu%s\n", candidates.size(),
                    roofGuard ? "   [roof guard fired: terrain lies above the WMO floor]"
                              : "");
        for (const Candidate& cand : candidates)
        {
            const Aabb& box = cand.instance->worldBounds;
            std::printf("   - root=%u adt=%d groups=%zu tris=%zu box z[%.1f..%.1f]%s%s\n",
                        cand.wmo->RootId(), cand.instance->adtId,
                        cand.wmo->Groups().size(), cand.wmo->Soup().tris.size(), box.lo.z,
                        box.hi.z, cand.rejected ? "  REJECTED: " : "", cand.reason);
            if (cand.hit)
            {
                std::printf("       nearest hit z=%.2f group=%u mogp=0x%08X  "
                            "point inside the group box: %s\n",
                            cand.hitZ, cand.group, cand.flags,
                            cand.pointInGroupBox ? "yes" : "no");
            }
            if (verbose)
            {
                for (const Bvh::Crossing& crossing : cand.crossings)
                {
                    const uint16_t g = crossing.tri < cand.wmo->TriGroups().size()
                                           ? cand.wmo->TriGroups()[crossing.tri]
                                           : uint16_t(0xFFFF);
                    const bool known = g < cand.wmo->Groups().size();
                    std::printf("         crossing z=%.2f group=%u mogp=0x%08X\n",
                                z + 2.0f - crossing.t,
                                known ? cand.wmo->Groups()[g].groupWmoId : 0u,
                                known ? cand.wmo->Groups()[g].mogpFlags : 0u);
                }
            }
        }

        PrintVerdict("verdict:", verdict);

        const Column column = terrain.ColumnAt(x, y, z + 2.0f, z - 50.0f);
        std::printf("  ColumnAt, z+2 down to z-50:\n");
        for (const Surface& s : column.Surfaces())
        {
            const char* kind = s.kind == SurfaceKind::Terrain ? "terrain"
                             : s.kind == SurfaceKind::Static  ? "static "
                             : s.kind == SurfaceKind::Live    ? "live   "
                                                              : "liquid ";
            if (s.kind == SurfaceKind::Liquid)
            {
                std::printf("   - %s z=%.2f  LiquidType row %u%s\n", kind, s.z,
                            s.liquidEntry, s.fromAdt ? " (ADT)" : " (WMO)");
            }
            else
            {
                std::printf("   - %s z=%.2f\n", kind, s.z);
            }
        }
    }

    /// The same column at rising heights. This is how "a player FLYING over Orgrimmar is
    /// still indoors" was found: the instance box reaches z=236 and the ray searches 300
    /// yards down, so nothing about being above a building ends the search.
    void Stack(const std::string& tiles, uint32_t mapId, float x, float y, float z0,
               float z1, float step)
    {
        std::shared_ptr<TerrainTile> tile =
            LoadTile(tiles, mapId, TileIndex(x), TileIndex(y));
        std::shared_ptr<TerrainTile> global =
            ReadTile(tiles + "/" + GlobalWmoFileName(mapId));
        if (!tile)
        {
            std::printf("no tile at (%.1f, %.1f)\n", x, y);
            return;
        }
        if (!(step > 0.f))
        {
            std::printf("step must be positive\n");
            return;
        }

        auto height = tile->TerrainHeight(x, y);
        std::printf("\n=== column at (%.2f, %.2f), terrain z=%s ===\n", x, y,
                    height ? std::to_string(*height).c_str() : "none");
        std::printf("      z    verdict    group   mogp\n");
        for (float z = z0; z <= z1 + 0.001f; z += step)
        {
            bool roofGuard = false;
            const Verdict v = EngineRule(tile.get(), global.get(), x, y, z, nullptr,
                                         &roofGuard);
            std::printf("  %7.1f  %-9s  %-6d  0x%08X %s\n", z,
                        v.found ? (v.outdoor ? "outdoors" : "INDOORS") : "no wmo",
                        v.found ? v.groupId : 0, v.mogpFlags,
                        roofGuard ? "[roof guard]" : "");
        }
    }

    /// A file of "x y z" lines -- real spawn positions out of the world database, so the
    /// sample is where units actually stand rather than where a grid happens to land.
    void Batch(const std::string& tiles, uint32_t mapId, const std::string& path)
    {
        std::FILE* f = std::fopen(path.c_str(), "r");
        if (!f)
        {
            std::printf("cannot open %s\n", path.c_str());
            return;
        }

        std::shared_ptr<TerrainTile> global =
            ReadTile(tiles + "/" + GlobalWmoFileName(mapId));
        std::map<std::pair<int, int>, std::shared_ptr<TerrainTile>> cache;
        std::map<int32_t, int> blame;
        size_t total = 0, indoors = 0, noWmo = 0;

        float x = 0.f, y = 0.f, z = 0.f;
        while (std::fscanf(f, "%f %f %f", &x, &y, &z) == 3)
        {
            const std::pair<int, int> key(TileIndex(x), TileIndex(y));
            auto it = cache.find(key);
            if (it == cache.end())
            {
                it = cache.emplace(key, LoadTile(tiles, mapId, key.first, key.second))
                         .first;
            }

            ++total;
            const Verdict v =
                EngineRule(it->second.get(), global.get(), x, y, z, nullptr, nullptr);
            if (!v.found)
            {
                ++noWmo;
            }
            if (!v.outdoor)
            {
                ++indoors;
                blame[v.groupId]++;
            }
        }
        std::fclose(f);

        std::printf("\n=== %zu positions%s ===\n", total,
                    g_legacyFlags ? "   [--legacy-flags: MOUNT_ALLOWED ignored]" : "");
        std::printf("  no WMO over them  : %zu\n", noWmo);
        std::printf("  INDOORS           : %zu (%.1f%%)\n", indoors,
                    total ? 100.0 * double(indoors) / double(total) : 0.0);
        std::printf("  groups answering INDOORS:\n");

        std::vector<std::pair<int, int32_t>> sorted;
        for (const auto& entry : blame)
        {
            sorted.push_back(std::make_pair(entry.second, entry.first));
        }
        std::sort(sorted.rbegin(), sorted.rend());
        for (size_t i = 0; i < sorted.size() && i < 12; ++i)
        {
            std::printf("    %5d positions  group %d\n", sorted[i].first,
                        sorted[i].second);
        }
    }

    /// What the tile says about water over a strip, and what the navmesh bake makes of
    /// it: NavMeshBuilder drops every cell whose deep ("dark") bit is set, so a channel
    /// that is all deep water carries no swimmable polygon at all.
    void Water(const std::string& tiles, uint32_t mapId, float x0, float y0, float x1,
               float y1, float step)
    {
        if (!(step > 0.f))
        {
            std::printf("step must be positive\n");
            return;
        }

        std::map<std::pair<int, int>, std::shared_ptr<TerrainTile>> cache;
        std::map<int, int> kinds, entries;
        size_t samples = 0, wet = 0, deep = 0;

        std::printf("\n=== water over x[%.0f..%.0f] y[%.0f..%.0f], step %.0f ===\n", x0,
                    x1, y0, y1, step);
        std::printf("  '.' dry   '~' swimmable   'D' deep, dropped from the navmesh\n\n");

        for (float x = x1; x >= x0 - 0.001f; x -= step)
        {
            std::string row;
            for (float y = y1; y >= y0 - 0.001f; y -= step)
            {
                const std::pair<int, int> key(TileIndex(x), TileIndex(y));
                auto it = cache.find(key);
                if (it == cache.end())
                {
                    it = cache.emplace(key, LoadTile(tiles, mapId, key.first, key.second))
                             .first;
                }

                ++samples;
                if (!it->second)
                {
                    row += '?';
                    continue;
                }
                auto liquid = it->second->LiquidAt(x, y);
                if (!liquid)
                {
                    row += '.';
                    continue;
                }

                ++wet;
                kinds[int(liquid->kind)]++;
                entries[int(liquid->entry)]++;
                if (liquid->deep)
                {
                    ++deep;
                    row += 'D';
                }
                else
                {
                    row += '~';
                }
            }
            std::printf("  x=%7.0f  %s\n", x, row.c_str());
        }

        std::printf("\n  %zu samples, %zu with liquid, %zu of those deep\n", samples, wet,
                    deep);
        std::printf("  kinds (1 water, 2 ocean, 3 magma, 4 slime):");
        for (const auto& kind : kinds)
        {
            std::printf("  %d x%d", kind.first, kind.second);
        }
        std::printf("\n  LiquidType rows:");
        for (const auto& entry : entries)
        {
            std::printf("  row %d x%d", entry.first, entry.second);
        }
        std::printf("\n");
    }

    /// Every WMO-carried liquid over a range of tiles, with the LiquidType row the baker
    /// wrote. On 2.4.3 only rows 1, 2, 3, 4, 21, 41 and 61 exist; anything else fails the
    /// runtime lookup in GridMap's LiquidFlagsOfRow and is served as plain water.
    void WmoLiquidRows(const std::string& tiles, uint32_t mapId, int tx0, int ty0,
                       int tx1, int ty1)
    {
        std::map<int, int> entries, kinds;
        size_t loaded = 0, groups = 0;

        for (int tx = tx0; tx <= tx1; ++tx)
        {
            for (int ty = ty0; ty <= ty1; ++ty)
            {
                std::shared_ptr<TerrainTile> tile = LoadTile(tiles, mapId, tx, ty);
                if (!tile)
                {
                    continue;
                }
                ++loaded;
                for (const StaticInstance& inst : tile->instances)
                {
                    if (!inst.model || inst.model->Kind() != ModelKind::Wmo)
                    {
                        continue;
                    }
                    const WmoModel* wmo =
                        static_cast<const WmoModel*>(inst.model.get());
                    for (const WmoModel::Group& group : wmo->Groups())
                    {
                        if (!group.hasLiquid)
                        {
                            continue;
                        }
                        ++groups;
                        entries[int(group.liquid.entry)]++;
                        kinds[int(group.liquid.kind)]++;
                    }
                }
            }
        }

        std::printf("\n=== WMO liquid over tiles [%d..%d]x[%d..%d]: %zu tiles, %zu "
                    "liquid groups ===\n", tx0, tx1, ty0, ty1, loaded, groups);
        for (const auto& entry : entries)
        {
            const int row = entry.first;
            const bool exists = row == 1 || row == 2 || row == 3 || row == 4 ||
                                row == 21 || row == 41 || row == 61;
            std::printf("  row %-4d x%-6d %s\n", row, entry.second,
                        exists ? "(a real 2.4.3 LiquidType row)"
                               : "<-- NOT a 2.4.3 row: the runtime lookup misses and "
                                 "serves it as WATER");
        }
        std::printf("  kinds (1 water, 2 ocean, 3 magma, 4 slime):");
        for (const auto& kind : kinds)
        {
            std::printf("  %d x%d", kind.first, kind.second);
        }
        std::printf("\n");
    }

    // ------------------------------------------------------------- the client ---
    /// The MOGP group header as the client stores it. The baker reads flags, groupLiquid
    /// and uniqueId out of it; the bounding box at 0x0C it does not read at all, and this
    /// prints it because that box is what vmap's group containment test was.
    struct Mogp
    {
        bool ok = false;
        uint32_t flags = 0, groupLiquid = 0, uniqueId = 0;
        float lo[3] = {0.f, 0.f, 0.f};
        float hi[3] = {0.f, 0.f, 0.f};
    };

    Mogp ReadMogp(const std::vector<uint8_t>& bytes)
    {
        using namespace world::terrain::internal;

        Mogp mogp;
        size_t pos = 0;
        while (pos + 8 <= bytes.size())
        {
            const uint32_t size = RdU32(bytes.data() + pos + 4);
            // Chunk tags are stored REVERSED on disk. A plain memcmp against "MOGP"
            // matches nothing and every field reads back as zero -- which looks exactly
            // like a client whose flags are all clear, the very thing being investigated.
            if (TagIs(bytes.data() + pos, "MOGP") && pos + 8 + 68 <= bytes.size())
            {
                const uint8_t* h = bytes.data() + pos + 8;
                mogp.ok = true;
                mogp.flags = RdU32(h + 0x08);
                for (int i = 0; i < 3; ++i)
                {
                    mogp.lo[i] = RdF32(h + 0x0C + 4 * i);
                    mogp.hi[i] = RdF32(h + 0x18 + 4 * i);
                }
                mogp.groupLiquid = RdU32(h + 0x34);
                mogp.uniqueId = RdU32(h + 0x38);
                return mogp;
            }
            if (pos + 8 + size > bytes.size())
            {
                break;
            }
            pos += 8 + size;
        }
        return mogp;
    }

    bool IsGroupFile(const std::string& path)
    {
        return path.size() > 8 && path[path.size() - 8] == '_' &&
               std::isdigit(static_cast<unsigned char>(path[path.size() - 7])) != 0;
    }

    /// Every group of one WMO, straight out of the archives, then the same groups as they
    /// were baked. A mismatch here is an extractor bug; agreement moves the question to
    /// the rule that reads the flags.
    void Groups(const std::string& data, const std::string& locale,
                const std::string& pattern, const std::string& rootPath,
                const std::string& tiles, uint32_t mapId, int tx, int ty)
    {
        StormLibArchive archive;
        const int opened = archive.OpenClientData(data, ClientArchives243(),
                                                  ClientLocaleArchives243(), locale);
        std::printf("opened %d archives from %s\n", opened, data.c_str());
        if (!opened)
        {
            return;
        }

        std::string root = rootPath;
        if (root.empty())
        {
            std::vector<std::string> found = archive.FindFiles(pattern);
            std::sort(found.begin(), found.end());
            std::printf("\nWMO roots matching %s:\n", pattern.c_str());
            for (const std::string& file : found)
            {
                if (IsGroupFile(file))
                {
                    continue;
                }
                std::printf("   %s\n", file.c_str());
                if (root.empty())
                {
                    root = file;
                }
            }
            if (root.empty())
            {
                std::printf("   (none)\n");
                return;
            }
            std::printf("\nreading the first: %s\n", root.c_str());
        }

        std::vector<uint8_t> bytes;
        WmoRootData rootData;
        if (!archive.Read(root, bytes) || !ParseWmoRoot(bytes, rootData))
        {
            std::printf("cannot read or parse %s\n", root.c_str());
            return;
        }

        std::printf("\nroot %s\n  rootId=%u groups=%u rootFlags=0x%X\n", root.c_str(),
                    rootData.wmoId, rootData.nGroups, rootData.flags);
        std::printf("\n  %-4s %-9s %-11s %-9s %-9s %-14s %-7s %s\n", "idx", "groupId",
                    "MOGP flags", "EXTERIOR", "INTERIOR", "MOUNT_ALLOWED", "state",
                    "client bbox z");

        std::map<uint32_t, uint32_t> flagsById;
        size_t exterior = 0, mountable = 0, neither = 0;
        for (uint32_t g = 0; g < rootData.nGroups; ++g)
        {
            std::vector<uint8_t> groupBytes;
            if (!archive.Read(WmoGroupPath(root, g), groupBytes))
            {
                std::printf("  %-4u  <group file missing from the archives>\n", g);
                continue;
            }

            const Mogp mogp = ReadMogp(groupBytes);
            WmoGroupData parsed;
            const WmoGroupParse state =
                ParseWmoGroup(groupBytes, rootData.flags, parsed);

            const bool isExterior = (mogp.flags & MOGP_FLAG_EXTERIOR) != 0;
            const bool isMountable = (mogp.flags & MOGP_FLAG_MOUNT_ALLOWED) != 0;
            exterior += isExterior ? 1 : 0;
            mountable += isMountable ? 1 : 0;
            neither += (!isExterior && !isMountable) ? 1 : 0;
            if (state == WmoGroupParse::Loaded)
            {
                flagsById[mogp.uniqueId] = mogp.flags;
            }

            std::printf("  %-4u %-9u 0x%08X  %-9s %-9s %-14s %-7s [%.1f .. %.1f]\n", g,
                        mogp.uniqueId, mogp.flags, isExterior ? "yes" : ".",
                        (mogp.flags & MOGP_FLAG_INTERIOR) ? "yes" : ".",
                        isMountable ? "yes" : ".",
                        state == WmoGroupParse::Loaded  ? "baked"
                        : state == WmoGroupParse::Empty ? "empty"
                                                        : "MALFORMED",
                        mogp.lo[2], mogp.hi[2]);
        }

        std::printf("\n  %u groups: %zu EXTERIOR, %zu MOUNT_ALLOWED, %zu neither "
                    "(those are the ones that read as indoors)\n", rootData.nGroups,
                    exterior, mountable, neither);

        std::shared_ptr<TerrainTile> tile = LoadTile(tiles, mapId, tx, ty);
        if (!tile)
        {
            std::printf("\n(no tile %d_%d to compare the bake against)\n", tx, ty);
            return;
        }

        std::printf("\n=== the same WMO as it was baked into tile %d_%d ===\n", tx, ty);
        for (const StaticInstance& inst : tile->instances)
        {
            if (!inst.model || inst.model->Kind() != ModelKind::Wmo)
            {
                continue;
            }
            const WmoModel* wmo = static_cast<const WmoModel*>(inst.model.get());
            if (wmo->RootId() != rootData.wmoId)
            {
                continue;
            }

            size_t checked = 0, mismatched = 0;
            for (const WmoModel::Group& group : wmo->Groups())
            {
                ++checked;
                auto it = flagsById.find(group.groupWmoId);
                if (it == flagsById.end())
                {
                    std::printf("  group %u: baked, but no client group carries that "
                                "id\n", group.groupWmoId);
                    ++mismatched;
                }
                else if (it->second != group.mogpFlags)
                {
                    std::printf("  group %u: tile 0x%08X != client 0x%08X\n",
                                group.groupWmoId, group.mogpFlags, it->second);
                    ++mismatched;
                }
            }
            std::printf("  %zu baked groups compared: %zu mismatches\n", checked,
                        mismatched);
            return;
        }
        std::printf("  that WMO is not placed in this tile\n");
    }

    // --------------------------------------------------------------- the mesh ---
    /// Runs the SERVER'S OWN query against the baked navmesh: same coordinate order
    /// (y, z, x), same search extents, same two-stage retry as PathFinder, and the
    /// filter a given creature would build. "The mesh has water polygons in it" and
    /// "a swimmer can path across them" are different claims, and only this settles
    /// the second -- polygons that exist but share no edge answer the first and fail
    /// the second.
    void Path(const std::string& mmaps, uint32_t mapId, float x1, float y1, float z1,
              float x2, float y2, float z2, unsigned short includeFlags)
    {
        char name[64];
        std::snprintf(name, sizeof(name), "%04u.mmap", mapId);
        std::FILE* f = std::fopen((mmaps + "/" + name).c_str(), "rb");
        if (!f)
        {
            std::printf("no %s in %s\n", name, mmaps.c_str());
            return;
        }
        dtNavMeshParams params{};
        const bool readParams = std::fread(&params, sizeof(params), 1, f) == 1;
        std::fclose(f);
        if (!readParams)
        {
            std::printf("cannot read %s\n", name);
            return;
        }

        dtNavMesh* mesh = dtAllocNavMesh();
        if (!mesh || dtStatusFailed(mesh->init(&params)))
        {
            std::printf("navmesh init failed\n");
            return;
        }

        size_t loaded = 0;
        for (int tx = 0; tx < 64; ++tx)
        {
            for (int ty = 0; ty < 64; ++ty)
            {
                std::snprintf(name, sizeof(name), "%04u%02i%02i.mmtile", mapId, tx, ty);
                std::FILE* tf = std::fopen((mmaps + "/" + name).c_str(), "rb");
                if (!tf)
                {
                    continue;
                }
                MmapTileHeader header{};
                if (std::fread(&header, sizeof(header), 1, tf) != 1)
                {
                    std::fclose(tf);
                    continue;
                }
                unsigned char* data =
                    static_cast<unsigned char*>(dtAlloc(header.size, DT_ALLOC_PERM));
                if (!data || std::fread(data, header.size, 1, tf) != 1)
                {
                    dtFree(data);
                    std::fclose(tf);
                    continue;
                }
                std::fclose(tf);
                if (dtStatusFailed(mesh->addTile(data, int(header.size),
                                                 DT_TILE_FREE_DATA, 0, nullptr)))
                {
                    dtFree(data);
                    continue;
                }
                ++loaded;
            }
        }

        dtNavMeshQuery* query = dtAllocNavMeshQuery();
        if (!query || dtStatusFailed(query->init(mesh, 1024)))
        {
            std::printf("query init failed\n");
            return;
        }

        dtQueryFilter filter;
        filter.setIncludeFlags(includeFlags);
        filter.setExcludeFlags(0);

        // PathFinder spells a point (y, z, x); getting this wrong searches the map's
        // mirror image and every lookup misses for reasons that look like missing tiles.
        float start[3] = {y1, z1, x1};
        float end[3] = {y2, z2, x2};

        auto nearest = [&](const float* pt, const char* label)
        {
            float extents[3] = {3.0f, 5.0f, 3.0f};
            float closest[3] = {0.f, 0.f, 0.f};
            dtPolyRef ref = 0;
            query->findNearestPoly(pt, extents, &filter, &ref, closest);
            if (!ref)
            {
                extents[1] = 200.0f;   // the retry PathFinder makes
                query->findNearestPoly(pt, extents, &filter, &ref, closest);
            }
            unsigned short flags = 0;
            unsigned char area = 0;
            if (ref)
            {
                mesh->getPolyFlags(ref, &flags);
                mesh->getPolyArea(ref, &area);
            }
            std::printf("  %-5s poly=%llu area=%s\n", label,
                        static_cast<unsigned long long>(ref),
                        area == NAV_GROUND  ? "GROUND"
                        : area == NAV_WATER ? "WATER"
                        : area == NAV_MAGMA ? "MAGMA"
                        : area == NAV_SLIME ? "SLIME"
                                            : "none");
            return ref;
        };

        std::printf("\n=== path (%.1f, %.1f, %.1f) -> (%.1f, %.1f, %.1f), map %u ===\n",
                    x1, y1, z1, x2, y2, z2, mapId);
        std::printf("  %zu mmtiles loaded, filter includeFlags=0x%X (%s%s)\n", loaded,
                    includeFlags, (includeFlags & NAV_GROUND) ? "GROUND " : "",
                    (includeFlags & NAV_WATER) ? "WATER" : "");

        const dtPolyRef startRef = nearest(start, "start");
        const dtPolyRef endRef = nearest(end, "end");
        if (!startRef || !endRef)
        {
            std::printf("  no polygon under one of the ends -> the server would give up "
                        "on the mesh entirely\n");
            return;
        }

        dtPolyRef path[256];
        int count = 0;
        const dtStatus status =
            query->findPath(startRef, endRef, start, end, &filter, path, &count, 256);

        std::printf("  findPath: %s%s, %d polygons\n",
                    dtStatusSucceed(status) ? "success" : "FAILED",
                    dtStatusDetail(status, DT_PARTIAL_RESULT) ? " (PARTIAL)" : "", count);

        if (count > 0)
        {
            std::map<int, int> areas;
            for (int i = 0; i < count; ++i)
            {
                unsigned char area = 0;
                mesh->getPolyArea(path[i], &area);
                areas[area]++;
            }
            std::printf("  polygons by area:");
            for (const auto& a : areas)
            {
                std::printf("  %s x%d",
                            a.first == NAV_GROUND  ? "GROUND"
                            : a.first == NAV_WATER ? "WATER"
                            : a.first == NAV_MAGMA ? "MAGMA"
                            : a.first == NAV_SLIME ? "SLIME"
                                                   : "other",
                            a.second);
            }
            std::printf("\n");
        }
        if (count > 0 && path[count - 1] != endRef)
        {
            std::printf("  the path STOPS SHORT of the destination -- this is what the "
                        "server sees as an incomplete path\n");
        }
    }

    void Usage()
    {
        std::printf(
            "\nmangos-wmo-probe -- why the terrain answers indoors, outdoors or wet.\n"
            "\n"
            "  --tiles <dir>   baked tiles                        (default: tiles)\n"
            "  --dbc <dir>     baked dbc, for area names          (default: dbc)\n"
            "  --data <dir>    the client's Data directory, for `groups`\n"
            "  --locale <loc>  client locale                      (default: enGB)\n"
            "  --map <id>      map id                             (default: 0)\n"
            "  --legacy-flags  ignore MOGP MOUNT_ALLOWED, as the rule did before it\n"
            "                  was read -- run twice to measure what that cost\n"
            "  -v              every surface the ray crosses, not just the winner\n"
            "\n"
            "  point <x> <y> <z>                 everything known about one position\n"
            "  stack <x> <y> <z0> <z1> <step>    the same column at rising heights\n"
            "  batch <file>                      \"x y z\" lines, e.g. real spawns\n"
            "  water <x0> <y0> <x1> <y1> <step>  liquid, and what the navmesh keeps\n"
            "  wmoliquid <tx0> <ty0> <tx1> <ty1> the LiquidType rows WMOs were baked\n"
            "                                    with, over a range of tiles\n"
            "  groups [--root <path>]            one WMO's group flags from the CLIENT,\n"
            "                                    compared against the bake\n"
            "  path <x1> <y1> <z1> <x2> <y2> <z2>  the server's own navmesh query, with\n"
            "                                    a swimmer's filter (--ground-only for\n"
            "                                    a creature that cannot swim)\n"
            "                                    needs --mmaps <dir>\n");
    }
}

int main(int argc, char** argv)
{
    std::string tiles = "tiles";
    std::string dbc = "dbc";
    std::string data = "Data";
    std::string locale = "enGB";
    std::string root;
    std::string pattern = "*.wmo";
    std::string mmaps = "mmaps";
    uint32_t mapId = 0;
    int tx = 0, ty = 0;
    bool verbose = false;
    bool groundOnly = false;

    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i)
    {
        args.push_back(argv[i]);
    }

    size_t i = 0;
    while (i < args.size())
    {
        const std::string& a = args[i];
        const bool hasValue = i + 1 < args.size();
        if (a == "--tiles" && hasValue) { tiles = args[++i]; }
        else if (a == "--dbc" && hasValue) { dbc = args[++i]; }
        else if (a == "--data" && hasValue) { data = args[++i]; }
        else if (a == "--locale" && hasValue) { locale = args[++i]; }
        else if (a == "--root" && hasValue) { root = args[++i]; }
        else if (a == "--find" && hasValue) { pattern = args[++i]; }
        else if (a == "--map" && hasValue) { mapId = uint32_t(std::atoi(args[++i].c_str())); }
        else if (a == "--tile" && i + 2 < args.size())
        {
            tx = std::atoi(args[++i].c_str());
            ty = std::atoi(args[++i].c_str());
        }
        else if (a == "--mmaps" && hasValue) { mmaps = args[++i]; }
        else if (a == "--legacy-flags") { g_legacyFlags = true; }
        else if (a == "--ground-only") { groundOnly = true; }
        else if (a == "-v") { verbose = true; }
        else { break; }
        ++i;
    }

    LoadDbc(dbc);
    std::printf("tiles: %s\ndbc  : %s (%zu WMOAreaTable rows, %zu areas)\n",
                tiles.c_str(), dbc.c_str(), g_wmoArea.size(), g_areaName.size());

    const std::string mode = i < args.size() ? args[i] : std::string();
    auto number = [&](size_t at) { return float(std::atof(args[at].c_str())); };
    auto integer = [&](size_t at) { return std::atoi(args[at].c_str()); };

    if (mode == "point" && i + 3 < args.size())
    {
        Point(tiles, dbc, mapId, number(i + 1), number(i + 2), number(i + 3), verbose);
    }
    else if (mode == "stack" && i + 5 < args.size())
    {
        Stack(tiles, mapId, number(i + 1), number(i + 2), number(i + 3), number(i + 4),
              number(i + 5));
    }
    else if (mode == "batch" && i + 1 < args.size())
    {
        Batch(tiles, mapId, args[i + 1]);
    }
    else if (mode == "water" && i + 5 < args.size())
    {
        Water(tiles, mapId, number(i + 1), number(i + 2), number(i + 3), number(i + 4),
              number(i + 5));
    }
    else if (mode == "wmoliquid" && i + 4 < args.size())
    {
        WmoLiquidRows(tiles, mapId, integer(i + 1), integer(i + 2), integer(i + 3),
                      integer(i + 4));
    }
    else if (mode == "groups")
    {
        Groups(data, locale, pattern, root, tiles, mapId, tx, ty);
    }
    else if (mode == "path" && i + 6 < args.size())
    {
        // A hunter pet: Creature::CanWalk gives GROUND, Pet::CanSwim gives the rest.
        const unsigned short flags =
            groundOnly ? NAV_GROUND
                       : (unsigned short)(NAV_GROUND | NAV_WATER | NAV_MAGMA | NAV_SLIME);
        Path(mmaps, mapId, number(i + 1), number(i + 2), number(i + 3), number(i + 4),
             number(i + 5), number(i + 6), flags);
    }
    else
    {
        Usage();
    }
    return 0;
}
