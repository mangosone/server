#pragma once

// A write-through disk cache in front of another ITileSource (the MPQ loader).
// First load of a tile goes to the inner source (decompress + parse the client
// data) and the assembled result is written to a flat cache file; every load
// after reads that file instead — a single sequential read, no decompression, no
// chunk parsing, no acceleration build (measured ~14-40x faster than the MPQ path).
//
// Called from the baker's single loader thread, so no internal locking. Null tiles
// (ocean / off-map) are not written to disk.

#include "terrain/IMpqArchive.hpp"  // (only for include hygiene parity)
#include "terrain/Terrain.hpp"

#include <memory>
#include <string>

namespace world::terrain {

class CachedTileSource : public ITileSource {
public:
    // `cacheDir` is created if missing. An empty cacheDir disables caching (every
    // load goes straight to the inner source) — handy for tests.
    CachedTileSource(std::unique_ptr<ITileSource> inner, std::string cacheDir);

    std::shared_ptr<TerrainTile> load(uint32_t mapId, int tx, int ty) override;

private:
    std::string pathFor(uint32_t mapId, int tx, int ty) const;
    std::string globalWmoPath(uint32_t mapId) const;

    std::unique_ptr<ITileSource> m_inner;
    std::string                  m_dir;     // empty => caching off
};

}  // namespace world::terrain
