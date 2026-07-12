#pragma once

// Builds a model-local collision model for one WMO from the client archive. A WMO
// is a root file (X.wmo, giving the group count) plus N group files (X_000.wmo …)
// whose MOGP holds the geometry: MOVT vertices, MOVI triangles and MOPY per-face flags.
//
// We keep only the COLLIDABLE faces (~40% of them), fold every group into a single soup
// in the WMO's own model space, and build a BVH over it here, offline. Blizzard's own
// MOBN/MOBR BSP is deliberately NOT read: it was measured head-to-head against a BVH,
// a BIH and a kd-tree (mangos-accelbench) and lost badly -- see WmoModel for why.
// The ray is still transformed into model space at query time, never the geometry.
//
// Models are cached by path so an ADT that places the same building many times
// (and adjacent tiles that share it) share one model + its BVH.

#include "terrain/Geometry.hpp"
#include "terrain/ICollisionModel.hpp"
#include "terrain/IMpqArchive.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace world { class LiquidTypeStore; }

namespace world::terrain {

// One doodad (an M2 prop -- crate, platform, brazier) placed INSIDE a WMO by its root
// file, in the WMO's own model space. These are not ADT placements: the ADT never names
// them, so an extractor that reads only MDDF/MODF misses every prop inside every
// building, and anything standing on one falls through it.
struct WmoDoodad {
    std::string name;              // M2 path, from MODN
    Vec3        pos;               // WMO model space
    float       quat[4] = {0, 0, 0, 1};  // rotation (x, y, z, w) -- NOT Euler
    float       scale = 1.f;
};

// A MODS entry: a named furnishing set, as a range into the MODD list. A WMO ships
// several, and each MODF placement picks exactly one (its doodadSet).
struct WmoDoodadSet {
    uint32_t start = 0;
    uint32_t count = 0;
};

struct WmoDoodadData {
    std::vector<WmoDoodadSet> sets;  // MODS
    std::vector<WmoDoodad>    defs;  // MODD
};

class WmoLoader {
public:
    // `liquidTypes` resolves the minority of WMOs whose root MOHD sets flag 0x4, meaning
    // MOGP.groupLiquid is a raw LiquidType.dbc id rather than a legacy code. It may be
    // null (a stand-alone game-object bake); only those WMOs are affected, and the two
    // rows the canonicalisation can otherwise emit are handled without it.
    explicit WmoLoader(IMpqArchive& archive, const LiquidTypeStore* liquidTypes = nullptr)
        : m_archive(archive), m_liquidTypes(liquidTypes) {}

    // Returns the collision model for the WMO at `rootPath` (an in-MPQ path such
    // as "World\\wmo\\Azeroth\\Buildings\\Human_Farm\\Farm.wmo"). Cached; a model
    // with no collidable faces is cached as an empty (non-null) model. Returns
    // nullptr only if the root file itself can't be read.
    std::shared_ptr<const ICollisionModel> load(const std::string& rootPath);

    // The doodad tables of the WMO at `rootPath`, or nullptr if it has none. Cached
    // alongside the collision model; `load` populates both, but this may be called on
    // its own. The caller composes each doodad's transform with the WMO placement's.
    const WmoDoodadData* doodads(const std::string& rootPath);

    size_t cachedModels() const { return m_cache.size(); }

private:
    IMpqArchive&                                                            m_archive;
    const LiquidTypeStore*                                                  m_liquidTypes;
    std::unordered_map<std::string, std::shared_ptr<const ICollisionModel>> m_cache;
    std::unordered_map<std::string, WmoDoodadData>                          m_doodads;
};

}  // namespace world::terrain
