#pragma once

// Flat binary (de)serialization of an assembled TerrainTile — terrain grids plus
// the WMO collision statics (each model's collidable triangles and the binned-SAH
// BVH the baker built over them) and the per-instance transforms. This is the
// "hybrid cache" payload: the heavy work of reading + parsing the client MPQs
// (decompress + hundreds of WMO group-file opens) AND of building the BVH is done
// once, offline, and the result is written here in the engine's own memory
// layout so a re-load is a single sequential read with no decompression, no chunk
// parsing, and no acceleration build.
//
// Native-endian, same-machine cache (not a portable archive). A magic+version
// header guards format changes: readTile returns nullptr on mismatch/corruption,
// which the caller treats as a miss and rebuilds from the MPQs.

#include "terrain/Terrain.hpp"

#include <memory>
#include <string>

namespace world::terrain {

// Write `tile` to `path`. Returns false on any I/O error (caller may proceed
// uncached). Instances whose model is not a WMO BSP model are skipped (only WMO
// statics come from the MPQ tile source).
bool writeTile(const TerrainTile& tile, const std::string& path);

// Read a tile previously written by writeTile. Returns nullptr if the file is
// missing, truncated, or a different format version (→ treat as a cache miss).
std::shared_ptr<TerrainTile> readTile(const std::string& path);

}  // namespace world::terrain
