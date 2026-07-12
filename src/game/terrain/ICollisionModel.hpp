#pragma once

// A piece of static collision geometry, queried in its own MODEL-LOCAL space
// (the world ray is transformed in, never the geometry — that is point (2)).
// Two implementations share this interface so a placement can hold either:
//   * CollisionModel — triangle soup + a BVH we build (synthetic/test, M2 hulls);
//   * WmoModel       — a WMO's collidable faces + a BVH baked over them.
// A StaticInstance refers to one of these via the interface and the height query
// is identical regardless of which acceleration structure is underneath.

#include "terrain/Geometry.hpp"

#include <optional>

namespace world::terrain {

class ICollisionModel {
public:
    virtual ~ICollisionModel() = default;

    // Nearest hit of ray (origin, dir) with parameter t in [0, tMax], in model
    // space. For a downward floor query this is the highest surface below the
    // origin. Returns the ray parameter t, or nullopt if nothing is hit.
    virtual std::optional<float> raycastNearest(const Vec3& origin, const Vec3& dir,
                                                float tMax) const = 0;

    // Model-local AABB of the collidable geometry (for world-space culling once
    // transformed by the placement).
    virtual const Aabb& bounds() const = 0;

    virtual bool empty() const = 0;

    // Liquid surface inside the model at a model-space point: the surface Z (model
    // space), plus the liquid's identity. Only WMOs carry interior liquid (MLIQ);
    // the default is "no liquid". The caller transforms the Z back to world space.
    //
    // Both fields are resolved by the baker, not here. Deriving the category from the
    // MLIQ chunk at query time is not possible: MLIQ's trailing uint16 is a materialId,
    // and the liquid identity actually lives in MOGP.groupLiquid (plus the root MOHD
    // flags, which decide whether it is a raw LiquidType.dbc id). See WmoLoader.
    struct LocalLiquid {
        float    z     = 0.f;
        uint16_t entry = 0;  // LiquidType.dbc id (0 = none)
        uint8_t  kind  = 0;  // LiquidKind (see Terrain.hpp), resolved at bake time
    };
    virtual std::optional<LocalLiquid> liquidLocal(const Vec3& pModel) const {
        (void)pModel;
        return std::nullopt;
    }
};

}  // namespace world::terrain
