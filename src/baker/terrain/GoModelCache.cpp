#include "terrain/GoModelCache.hpp"

#include "terrain/M2Loader.hpp"
#include "terrain/Terrain.hpp"
#include "terrain/TileSerializer.hpp"
#include "terrain/WmoLoader.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace world::terrain {

std::string goModelCachePath(const std::string& dir, uint32_t displayId) {
    return dir + "/go_" + std::to_string(displayId) + ".tile";
}

bool bakeCollisionModelTile(IMpqArchive& archive, const std::string& modelPath,
                            const std::string& outPath) {
    // Resolve the collision model by model kind. Both get a BVH built HERE -- a WMO over
    // its collidable faces alone (WmoLoader, which deliberately does not read Blizzard's
    // MOBN/MOBR BSP), an M2/MDX over its collision hull (M2Loader). Both yield an
    // ICollisionModel that TileSerializer round-trips (kKindWmo, kKindBvh), so everything
    // below is model-agnostic.
    std::string ext = modelPath;
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const bool isWmo = ext.size() >= 4 && ext.compare(ext.size() - 4, 4, ".wmo") == 0;

    std::shared_ptr<const ICollisionModel> model;
    if (isWmo) {
        WmoLoader loader(archive);
        model = loader.load(modelPath);
    } else {
        M2Loader loader(archive);  // handles the .mdx/.mdl -> .m2 on-disk rewrite
        model = loader.load(modelPath);
    }
    if (!model || model->empty())
        return false;  // unreadable or no collidable geometry — nothing to place

    // A single model at the identity transform: geometry stays in its own model-local
    // frame (Transform{} == identity rot, scale 1, zero pos). The runtime replaces this
    // transform with the object's live world pose; the stored one only round-trips the
    // model bytes.
    TerrainTile tile;
    tile.hasTerrain = false;
    StaticInstance inst;
    inst.xf = Transform{};                 // identity
    inst.model = model;
    inst.worldBounds = model->bounds();    // == model-local bounds at identity
    tile.instances.push_back(std::move(inst));

    std::error_code ec;
    const std::filesystem::path p(outPath);
    if (p.has_parent_path())
        std::filesystem::create_directories(p.parent_path(), ec);  // ok if it exists

    return writeTile(tile, outPath);
}

bool bakeGoModel(IMpqArchive& archive, uint32_t displayId,
                 const std::string& modelPath, const std::string& outDir) {
    return bakeCollisionModelTile(archive, modelPath, goModelCachePath(outDir, displayId));
}

}  // namespace world::terrain
