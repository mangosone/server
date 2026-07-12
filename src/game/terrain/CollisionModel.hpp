#pragma once

// A piece of static collision geometry in its own MODEL-LOCAL space (a WMO group,
// or an M2's collision hull). Held once and shared by every placement of that
// model (a WMO instanced 50 times stores its triangles once — the placements only
// differ by Transform). Queries run in model space: the world ray is transformed
// in, not the geometry out — that is point (2), "use the model's own acceleration
// structure / ray-in-model-space", and it is what avoids duplicating geometry per
// instance.
//
// The acceleration structure is a compact BVH we build over the triangles. Used
// for synthetic/test geometry and for M2 collision hulls (which ship no BSP). WMO
// WMO geometry gets its own BVH too — see WmoModel — built offline over just the
// collidable faces and baked into the tile.

#include "terrain/Geometry.hpp"
#include "terrain/ICollisionModel.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace world::terrain {

class CollisionModel : public ICollisionModel {
public:
    CollisionModel(std::vector<Vec3> verts,
                   std::vector<std::array<uint32_t, 3>> tris);

    std::optional<float> raycastNearest(const Vec3& origin, const Vec3& dir,
                                        float tMax) const override;

    const Aabb& bounds() const override { return m_bounds; }
    bool   empty() const override { return m_tris.empty(); }
    size_t triangleCount() const { return m_tris.size(); }

    // Geometry accessors (for the flat-cache serializer; M2 hulls have no BSP, so
    // they round-trip through these raw triangle lists).
    const std::vector<Vec3>& verts() const { return m_verts; }
    const std::vector<std::array<uint32_t, 3>>& tris() const { return m_tris; }

private:
    struct Node {
        Aabb box;
        int  left  = -1;   // child node index, or -1 for a leaf
        int  first = 0;    // leaf: first index into m_order
        int  count = 0;    // leaf: triangle count (>0 only for leaves)
    };

    Tri triangle(uint32_t i) const {
        const auto& t = m_tris[i];
        return Tri{m_verts[t[0]], m_verts[t[1]], m_verts[t[2]]};
    }
    int  build(int start, int end);

    std::vector<Vec3>                    m_verts;
    std::vector<std::array<uint32_t, 3>> m_tris;
    std::vector<uint32_t>                m_order;   // triangle indices, BVH-ordered
    std::vector<Node>                    m_nodes;
    Aabb                                 m_bounds;
};

}  // namespace world::terrain
