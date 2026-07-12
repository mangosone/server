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

// mangos-baker — the OFFLINE baker for the ported terrain collision engine. One pass
// over a WoW 2.4.3 client's MPQ archives produces the warm caches mangosd reads at
// runtime, replacing the old map-extractor + vmap-extractor split. The bake is split
// into independent components you select on the command line:
//
//   dbc      client DBCs                    -> <dest>/dbc/<Name>.dbc
//   tile     fused terrain+collision tiles  -> <dest>/tiles/t_<map>_<tx>_<ty>.tile
//   gomodels game-object collision models   -> <dest>/gomodels/go_<displayId>.tile
//   nav      Detour navmesh, from the tiles -> <dest>/mmaps/<map>.mmap + .mmtile
//
// A `tile` fuses the ADT heightfield/liquid/area with the static WMO/M2 collision
// (each model's collidable faces plus the binned-SAH BVH built here over them; Blizzard's
// authored MOBN/MOBR BSP was measured against it and retired), written in the exact TileSerializer
// payload (magic "TBCX") the runtime FusedTerrain reads. Enumeration mirrors the
// runtime: probe every (tx,ty) with load(mapId,tx,ty), so a baked tile lands where the
// server looks for it.
//
// `nav` builds the Detour navmesh straight from the baked tiles, so the surface the
// pathfinder walks on is exactly the surface FusedTerrain collides against. It reads
// <dest>/tiles, therefore `tile` must have run at least once (no MPQ access needed).
//
//   mangos-baker [components] [options]
//
//   components : any of  dbc tile gomodels nav  (or `all`; default: all)
//   --src  <dir>   client Data/ dir       (default: Data)
//   --dest <dir>   output root dir         (default: cache)
//   --locale <loc> client locale folder    (default: enGB)
//   --map  <id>    bake only this map id    (default: every map in Map.dbc)
//   -h, --help     show usage and exit

#include "terrain/MpqTileSource.hpp"
#include "terrain/CachedTileSource.hpp"
#include "terrain/StormLibArchive.hpp"
#include "terrain/WdtParser.hpp"
#include "terrain/Terrain.hpp"
#include "terrain/GoModelCache.hpp"        // bakeGoModel (model-by-displayId bake)
#include "stores/MapDBCStore.hpp"
#include "stores/GameObjectDisplayInfoStore.hpp"
#include "nav/NavMeshBuilder.hpp"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

using namespace world::terrain;

namespace
{
    // --- CLI ---------------------------------------------------------------------
    struct Options
    {
        std::string src    = "Data";   ///< client Data/ dir (source)
        std::string dest   = "cache";  ///< output root dir (destination)
        std::string locale = "enGB";   ///< client locale folder
        long        mapFilter = -1;    ///< bake only this map id, -1 = all
        bool        dbc      = false;
        bool        tile     = false;
        bool        gomodels = false;
        bool        nav      = false;
    };

    void printUsage(std::ostream& os)
    {
        os <<
            "mangos-baker [components] [options]\n"
            "\n"
            "Bakes the warm caches mangosd reads at runtime. Pick one or more\n"
            "components; with none named, every component runs.\n"
            "\n"
            "Components:\n"
            "  dbc        extract client DBCs        -> <dest>/dbc\n"
            "  tile       bake terrain collision     -> <dest>/tiles\n"
            "  gomodels   bake game-object models    -> <dest>/gomodels\n"
            "  nav        bake Detour navmesh        -> <dest>/mmaps\n"
            "  all        shorthand for all of the above\n"
            "\n"
            "Options:\n"
            "  --src  <dir>    client Data/ dir       (default: Data)\n"
            "  --dest <dir>    output root dir         (default: cache)\n"
            "  --locale <loc>  client locale folder    (default: enGB)\n"
            "  --map  <id>     bake only this map id    (default: all maps)\n"
            "  -h, --help      show this help and exit\n";
    }

    // Returns false on a bad argument (message already printed). On success `opt`
    // holds the parsed options with at least one component enabled.
    bool parseArgs(int argc, char* argv[], Options& opt)
    {
        bool anyComponent = false;

        for (int i = 1; i < argc; ++i)
        {
            const std::string a = argv[i];

            auto needValue = [&](const char* name) -> const char*
            {
                if (i + 1 >= argc)
                {
                    std::cerr << "error: " << name << " needs a value\n";
                    return nullptr;
                }
                return argv[++i];
            };

            if (a == "-h" || a == "--help")
            {
                printUsage(std::cout);
                std::exit(0);
            }
            else if (a == "--src")
            {
                const char* v = needValue("--src");
                if (!v) { return false; }
                opt.src = v;
            }
            else if (a == "--dest")
            {
                const char* v = needValue("--dest");
                if (!v) { return false; }
                opt.dest = v;
            }
            else if (a == "--locale")
            {
                const char* v = needValue("--locale");
                if (!v) { return false; }
                opt.locale = v;
            }
            else if (a == "--map")
            {
                const char* v = needValue("--map");
                if (!v) { return false; }
                opt.mapFilter = std::stol(v);
            }
            else if (a == "dbc")      { opt.dbc = true;      anyComponent = true; }
            else if (a == "tile")     { opt.tile = true;     anyComponent = true; }
            else if (a == "gomodels") { opt.gomodels = true; anyComponent = true; }
            else if (a == "nav")      { opt.nav      = true; anyComponent = true; }
            else if (a == "all")
            {
                opt.dbc = opt.tile = opt.gomodels = opt.nav = true;
                anyComponent = true;
            }
            else
            {
                std::cerr << "error: unknown argument '" << a << "'\n\n";
                printUsage(std::cerr);
                return false;
            }
        }

        // No component named => bake everything (the old all-in-one behaviour).
        if (!anyComponent)
        {
            opt.dbc = opt.tile = opt.gomodels = opt.nav = true;
        }
        return true;
    }

    // --- Progress feedback -------------------------------------------------------
    // The bake spends a while per map inside the tile loop, so print a single
    // carriage-return-updated status line the user can watch instead of a dead cursor.
    // Pads to a fixed width so a shorter line fully overwrites a longer previous one.
    void progress(const std::string& s)
    {
        std::string line = s;
        if (line.size() < 78)
        {
            line.append(78 - line.size(), ' ');
        }
        std::cout << '\r' << line << std::flush;
    }

    void progressDone()
    {
        std::cout << '\n';
    }

    // --- I/O helpers -------------------------------------------------------------
    bool writeFile(const std::string& path, const std::vector<uint8_t>& bytes)
    {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (!f)
        {
            return false;
        }
        f.write(reinterpret_cast<const char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
        return static_cast<bool>(f);
    }

    // Last path component of a WoW in-MPQ path ("DBFilesClient\\Spell.dbc" -> "Spell.dbc").
    std::string wowBasename(const std::string& path)
    {
        const size_t pos = path.find_last_of("\\/");
        return pos == std::string::npos ? path : path.substr(pos + 1);
    }

    // --- Component: DBC ----------------------------------------------------------
    // Dump every *.dbc the client ships into <dest>/dbc as a flat set of files. The
    // archive resolves each name highest-priority-first, so the locale-patched version
    // wins — exactly the single dbc/ folder mangosd loads for the configured locale.
    int bakeDbc(StormLibArchive& archive, const std::string& dbcDir)
    {
        std::error_code ec;
        std::filesystem::create_directories(dbcDir, ec);

        std::cout << "Extracting client DBCs -> " << dbcDir << "\n";
        const std::vector<std::string> files = archive.findFilesByExtension("dbc");
        int written = 0;
        std::vector<uint8_t> bytes;
        for (size_t i = 0; i < files.size(); ++i)
        {
            if (archive.read(files[i], bytes) &&
                writeFile(dbcDir + "/" + wowBasename(files[i]), bytes))
            {
                ++written;
            }
            progress("  dbc " + std::to_string(i + 1) + "/" +
                     std::to_string(files.size()) + "  (" + std::to_string(written) +
                     " written)");
        }
        progressDone();
        std::cout << "DBC files: " << written << " extracted\n\n";
        return written;
    }

    // --- Component: tile ---------------------------------------------------------
    // Bake the fused terrain+collision tiles for every map. Loading a tile through the
    // write-through CachedTileSource decompresses + parses the client data once and
    // writes t_<map>_<tx>_<ty>.tile as a side effect; here we only drive the load and
    // count. Enumeration matches the runtime's (tx,ty) indexing so tiles land where the
    // server looks.
    int bakeTiles(ITileSource& source, StormLibArchive& archive,
                  const std::vector<std::pair<uint32_t, const world::MapInfo*>>& maps)
    {
        int totalTerrain = 0;

        for (size_t n = 0; n < maps.size(); ++n)
        {
            const uint32_t mapId = maps[n].first;
            const world::MapInfo& info = *maps[n].second;
            const std::string label =
                "  [" + std::to_string(n + 1) + "/" + std::to_string(maps.size()) +
                "] map " + std::to_string(mapId) + " " + info.directory;

            const std::string wdtFile =
                "World\\Maps\\" + info.directory + "\\" + info.directory + ".wdt";
            std::vector<uint8_t> wdtBytes;
            WdtData wdt;
            if (!(archive.read(wdtFile, wdtBytes) && parseWdt(wdtBytes, wdt)))
            {
                progress(label + ": no map data");
                progressDone();
                continue;
            }

            int mapTerrain = 0;

            // --- Normal ADT tiles. Probe the whole grid with the SAME (tx,ty) indexing
            //     the runtime uses; load() returns null cheaply where no ADT exists and
            //     write-throughs t_<map>_<tx>_<ty>.tile where one does. ---
            for (int tx = 0; tx < 64; ++tx)
            {
                for (int ty = 0; ty < 64; ++ty)
                {
                    auto tile = source.load(mapId, tx, ty);
                    if (!tile || !tile->hasTerrain)
                    {
                        continue;
                    }
                    ++mapTerrain;
                    ++totalTerrain;
                }
                progress(label + " col " + std::to_string(tx + 1) + "/64  (" +
                         std::to_string(mapTerrain) + " terrain)");
            }

            // --- WDT global-WMO maps (instances/dungeons): one WMO placed in world
            //     space, no ADT terrain. load(mapId,0,0) returns the global-WMO tile,
            //     which CachedTileSource writes to w_<map>.tile. ---
            if (wdt.hasGlobalWmo)
            {
                auto gtile = source.load(mapId, 0, 0);
                if (gtile && gtile->isGlobalWmo && !gtile->instances.empty())
                {
                    ++mapTerrain;
                    ++totalTerrain;
                }
            }

            progress(label + ": " + std::to_string(mapTerrain) + " terrain");
            progressDone();
        }

        return totalTerrain;
    }

    // --- Component: game-object collision models ---------------------------------
    // Every game-object display with a model is baked once into
    // gomodels/go_<displayId>.tile — a single identity-posed WMO/M2 collision model.
    // At runtime GameObjectModel loads it by displayId, shares it across every live
    // object of that display, and places it under the object's live pose. This is what
    // replaced the vmap model store (vmaps/*.vmo + the GAMEOBJECT_MODELS list file).
    int bakeGoModels(StormLibArchive& archive, const std::string& gomodelsDir, int& skipped)
    {
        std::error_code ec;
        std::filesystem::create_directories(gomodelsDir, ec);

        world::GameObjectDisplayInfoStore displayInfo;
        if (!displayInfo.loadFromDbc(archive))
        {
            std::cout << "[gomodels] GameObjectDisplayInfo.dbc not readable - skipping\n";
            return 0;
        }

        std::cout << "Baking game-object collision models -> " << gomodelsDir << "\n";
        int baked = 0;
        skipped = 0;
        const auto& entries = displayInfo.entries();
        size_t seen = 0;
        for (const auto& [displayId, info] : entries)
        {
            ++seen;
            if (!info.modelPath.empty())
            {
                if (world::terrain::bakeGoModel(archive, displayId, info.modelPath, gomodelsDir))
                {
                    ++baked;
                }
                else
                {
                    ++skipped;
                }
            }
            progress("  gomodels " + std::to_string(seen) + "/" +
                     std::to_string(entries.size()) + "  (" + std::to_string(baked) +
                     " baked, " + std::to_string(skipped) + " skipped)");
        }
        progressDone();
        std::cout << "GO models: " << baked << " baked, " << skipped
                  << " skipped (no collision)\n\n";
        return baked;
    }
}  // namespace

int main(int argc, char* argv[])
{
    Options opt;
    if (!parseArgs(argc, argv, opt))
    {
        return 2;
    }

    const std::string dbcDir      = opt.dest + "/dbc";
    // Must match FusedTerrain::SetTileDir() in World.cpp: <DataDir>/tiles.
    const std::string tilesDir    = opt.dest + "/tiles";
    const std::string gomodelsDir = opt.dest + "/gomodels";
    const std::string mmapsDir    = opt.dest + "/mmaps";

    StormLibArchive archive;
    std::cout << "Opening client data from: " << opt.src << " (locale: " << opt.locale << ")\n";
    const int opened = archive.openClientData(opt.src, opt.locale);
    std::cout << "Opened " << opened << " MPQ archives.\n\n";
    if (opened == 0)
    {
        std::cerr << "No archives opened. Check the client Data/ path (--src).\n";
        return 2;
    }

    int dbcCount = 0, totalTerrain = 0, goModels = 0, goSkipped = 0;

    // --- dbc ---------------------------------------------------------------------
    if (opt.dbc)
    {
        dbcCount = bakeDbc(archive, dbcDir);
    }

    // --- tile --------------------------------------------------------------------
    if (opt.tile)
    {
        auto mapStore = world::loadMapStore(archive);
        if (!mapStore)
        {
            std::cerr << "Failed to read DBFilesClient\\Map.dbc from the archives.\n";
            return 1;
        }

        std::error_code ec;
        std::filesystem::create_directories(tilesDir, ec);

        CachedTileSource source(std::make_unique<MpqTileSource>(archive), tilesDir);

        // Collect the maps to bake up front so progress can show "map n/total".
        std::vector<std::pair<uint32_t, const world::MapInfo*>> maps;
        for (const auto& [mapId, info] : mapStore->mapsRegistry)
        {
            if (info.directory.empty())
            {
                continue;
            }
            if (opt.mapFilter >= 0 && static_cast<long>(mapId) != opt.mapFilter)
            {
                continue;
            }
            maps.emplace_back(mapId, &info);
        }

        std::cout << "Baking " << maps.size() << " maps (terrain)\n";
        totalTerrain = bakeTiles(source, archive, maps);
        std::cout << "\n";
    }

    // --- gomodels ----------------------------------------------------------------
    if (opt.gomodels)
    {
        goModels = bakeGoModels(archive, gomodelsDir, goSkipped);
    }

    // --- nav ---------------------------------------------------------------------
    // Reads the tiles we just baked (or a previous run's), never the MPQs.
    int navTiles = 0;
    if (opt.nav)
    {
        std::cout << "Baking navmesh -> " << mmapsDir << "\n";
        world::nav::NavMeshBuilder builder(tilesDir, mmapsDir);
        navTiles = builder.bakeAll(opt.mapFilter);
        if (navTiles < 0)
        {
            std::cerr << "Navmesh bake failed.\n";
            return 1;
        }
        std::cout << "\n";
    }

    // --- Summary -----------------------------------------------------------------
    std::cout << "=== Done ===\n";
    if (opt.dbc)
    {
        std::cout << "DBC files     : " << dbcCount << "  -> " << dbcDir << "\n";
    }
    if (opt.tile)
    {
        std::cout << "Terrain tiles : " << totalTerrain << "  -> " << tilesDir << "\n";
    }
    if (opt.gomodels)
    {
        std::cout << "GO models     : " << goModels << "  -> " << gomodelsDir << "\n";
    }
    if (opt.nav)
    {
        std::cout << "Navmesh tiles : " << navTiles << "  -> " << mmapsDir << "\n";
    }
    return 0;
}
