#pragma once

// An ITileSource backed by client data: given (mapId, tx, ty) it reads the
// matching .adt out of an IMpqArchive, parses it, and returns a ready TerrainTile.
// Returns nullptr for tiles with no ADT (ocean / off-map), which the caller
// (CachedTileSource) remembers as "known empty".
//
// mapId -> map directory name comes from a small built-in table covering the
// TBC maps; setMapName() overrides/extends it for anything exotic. Backslash
// paths match the in-MPQ convention (World\Maps\<Name>\<Name>_<ty>_<tx>.adt).

#include "terrain/IMpqArchive.hpp"
#include "terrain/M2Loader.hpp"
#include "terrain/Terrain.hpp"
#include "terrain/WmoLoader.hpp"
#include "terrain/WdtParser.hpp"
#include "stores/LiquidTypeStore.hpp"
#include "stores/MapDBCStore.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace world::terrain
{
    struct WdtData;

    class MpqTileSource : public ITileSource
    {
    public:
        // m_liquidTypes is declared before m_wmo so it is alive by the time a WMO is
        // parsed. A missing LiquidType.dbc is not fatal: only WMOs whose root asks for a
        // raw dbc id need it, and those then fall back to "water".
        explicit MpqTileSource(IMpqArchive &archive)
            : m_archive(archive), m_wmo(archive, &m_liquidTypes), m_m2(archive),
              m_mapStore(loadMapStore(archive))
        {
            m_liquidTypes.loadFromDbc(archive);
        }

        std::shared_ptr<TerrainTile> load(uint32_t mapId, int tx, int ty) override;

        // When true (default), MODF/MWO + MDDF placements are turned into collision
        // StaticInstances on the tile (Stage B2). When false, only terrain is built
        // — handy for isolating height math from model loading.
        void setLoadStatics(bool on) { m_loadStatics = on; }

    private:
        std::string mapName(uint32_t mapId) const;
        std::string adtPath(uint32_t mapId, int tx, int ty) const;
        std::string wdtPath(uint32_t mapId) const;

        std::shared_ptr<TerrainTile> tryloadAdt(uint32_t mapId, int tx, int ty);
        std::shared_ptr<TerrainTile> tryloadWdt(uint32_t mapId);

        // Append the collision of the doodads the WMO placement `p` furnishes itself with
        // (its MODS set) to `tile`. `wmoXf` is that placement's transform, already built.
        // Doodads with no collision hull -- most of them: grass, banners, small props --
        // add nothing.
        void attachWmoDoodads(const AdtPlacement &p, const std::string &wmoPath,
                              const Transform &wmoXf, TerrainTile &tile);

        IMpqArchive &m_archive;
        world::LiquidTypeStore m_liquidTypes;   // must precede m_wmo (see the ctor)
        WmoLoader m_wmo;
        M2Loader m_m2;
        std::unique_ptr<MapStore> m_mapStore;
        std::unordered_map<uint32_t, world::terrain::WdtData> m_wdtCache;
        std::unordered_map<uint32_t, std::shared_ptr<TerrainTile>> m_globalWmoCache;
        bool m_loadStatics = true;
    };

} // namespace world::terrain
