#pragma once

// Loads the collision hull of an M2 doodad. Unlike a WMO, an M2 carries a small
// purpose-built collision mesh in its header — boundingVertices + boundingTriangles
// — separate from the render mesh. Most doodads (grass, small props) have none and
// load as an empty model. The mesh has no BSP, so we build a BVH (CollisionModel),
// sharing the one uniform query path with synthetic geometry.
//
// Vertices are brought into the runtime model space with the same fix OregonCore's
// extractor applies (net: negate Y), so the placement transform — identical to a
// WMO's — lands them in the world. Models are cached by path.

#include "terrain/ICollisionModel.hpp"
#include "terrain/IMpqArchive.hpp"

#include <memory>
#include <string>
#include <unordered_map>

namespace world::terrain {

class M2Loader {
public:
    explicit M2Loader(IMpqArchive& archive) : m_archive(archive) {}

    // `path` is an in-MPQ model path; a .mdx/.mdl extension (as stored in MMDX) is
    // rewritten to .m2 (the on-disk file). Cached; empty (no-collision) models are
    // cached as non-null empties. Returns nullptr only if the file can't be read.
    std::shared_ptr<const ICollisionModel> load(const std::string& path);

    size_t cachedModels() const { return m_cache.size(); }

private:
    IMpqArchive&                                                            m_archive;
    std::unordered_map<std::string, std::shared_ptr<const ICollisionModel>> m_cache;
};

}  // namespace world::terrain
