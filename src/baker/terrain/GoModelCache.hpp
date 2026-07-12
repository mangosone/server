#pragma once

// The game-object collision model bake: one immutable, identity-posed collision model
// per GameObjectDisplayInfo displayId, written as
//
//     <dir>/go_<displayId>.tile
//
// in the exact TileSerializer payload the runtime already reads. At runtime
// GameObjectModel loads it by displayId, shares it across every live game object of
// that display, and places it under the object's live pose (see DynamicCollision).
//
// This replaces the vmap model store (vmaps/*.vmo + the GAMEOBJECT_MODELS list file):
// a door, bridge or chest now collides against a binned-SAH BVH built HERE, offline,
// over the model's collidable faces alone -- a WMO's, or an M2's collision hull.
//
// bakeCollisionModelTile() below is the one-instance, identity-posed tile body, kept
// separate from bakeGoModel() so a future moving-transport deck bake can reuse it.

#include <cstdint>
#include <string>

namespace world::terrain {

class IMpqArchive;

// "<dir>/go_<displayId>.tile"
std::string goModelCachePath(const std::string& dir, uint32_t displayId);

// Shared bake body: read `modelPath` from `archive` (a .wmo keeps its authored BSP; an
// .m2/.mdx gets a BVH over its collision hull), wrap it as a single identity-transform
// StaticInstance, and write that tile to `outPath`. Creates the parent dir if needed.
// Returns false when the model is unreadable or carries no collidable geometry.
// `modelPath` must be the raw DBC path (backslashes) — StormLib needs them.
bool bakeCollisionModelTile(IMpqArchive& archive, const std::string& modelPath,
                            const std::string& outPath);

// OFFLINE: bake one game-object display's collision model to goModelCachePath().
bool bakeGoModel(IMpqArchive& archive, uint32_t displayId,
                 const std::string& modelPath, const std::string& outDir);

}  // namespace world::terrain
