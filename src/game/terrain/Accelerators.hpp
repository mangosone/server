#pragma once

// Three ray-acceleration structures over one triangle soup, plus the soup itself.
//
// The engine answers WMO collision with the BVH below, built over the COLLIDABLE faces
// alone and baked into the .tile (see WmoModel). It does NOT use Blizzard's authored
// per-group MOBN/MOBR BSP; that was the original design (WmoBspModel), and these
// structures exist because it was measured against them -- see the mangos-accelbench
// tool -- and lost:
//
//   * A BSP node carries a split plane and nothing else, so a traversal cannot tell that
//     a ray misses a group; it just descends. A WMO is many groups (Black Temple: 49) and
//     every one of them was walked for every ray. An AABB gate alone recovered 13.7x.
//   * The BSP indexes EVERY face, render-only ones included, and only ~40% of a WMO's
//     faces are collidable -- so it carried ~2.5x the geometry collision needs and
//     re-tested the MOPY filter at each leaf face it touched.
//
// The binned-SAH BVH beat even the AABB-gated BSP by a further ~2.7x AND let the other
// ~60% of the triangles be dropped from the tile. (The kd-tree was faster still, ~5.9x,
// but duplicates straddling faces into both children at 6x the memory -- not a trade a
// server holding many maps resident can make. It is kept here for the bench.)
//
// All three take a ray in model-local space and return the nearest hit's ray parameter t,
// so they remain drop-in comparable.

#include "terrain/Geometry.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>
#include <utility>

namespace world::terrain
{

    // A flat triangle soup in model space -- what every structure below indexes.
    struct TriSoup
    {
        std::vector<Vec3> verts;
        std::vector<std::array<uint32_t, 3>> tris;

        Tri at(uint32_t i) const
        {
            const auto &t = tris[i];
            return Tri{verts[t[0]], verts[t[1]], verts[t[2]]};
        }
        Aabb triBounds(uint32_t i) const
        {
            const Tri t = at(i);
            Aabb b;
            b.expand(t.a);
            b.expand(t.b);
            b.expand(t.c);
            return b;
        }
        Vec3 centroid(uint32_t i) const
        {
            const Tri t = at(i);
            return Vec3{(t.a.x + t.b.x + t.c.x) / 3.f, (t.a.y + t.b.y + t.c.y) / 3.f,
                        (t.a.z + t.b.z + t.c.z) / 3.f};
        }
        size_t size() const { return tris.size(); }
    };

    // Bounding Volume Hierarchy, binned-SAH. This is the structure the runtime ships:
    // WmoModel stores one per WMO, built over that WMO's collidable faces and baked into
    // the .tile, so nothing is rebuilt at load.
    //
    // Building REORDERS the soup so each leaf owns a contiguous run of triangles. That is
    // deliberate: it removes the usual per-triangle indirection array entirely (4 bytes a
    // face, and a dependent load in the innermost loop), which is why the node table below
    // is the only index this structure has.
    class Bvh
    {
    public:
        // Deepest node build_ will create. raycast()'s traversal stack is sized from this
        // so a push can NEVER overflow: the DFS pops one node and pushes two, netting at
        // most +1 entry per level. Keep the two in step if you ever raise this.
        static constexpr int kMaxDepth = 48;

        // POD, and written to the .tile verbatim -- do not reorder or resize the fields.
        struct Node
        {
            Aabb box;
            // Children are allocated depth-first, so the right child is NOT left+1: the
            // whole left subtree sits between them. Both indices are stored.
            int32_t left = -1;  // inner: left child; leaf: -1
            int32_t right = -1; // inner: right child
            uint32_t first = 0; // leaf: first triangle (index into soup.tris)
            uint32_t count = 0; // leaf: triangle count
        };

        // Builds over `soup`, PERMUTING soup.tris in place. `parallel`, when given, is
        // permuted elementwise alongside it (WmoModel passes its per-triangle group ids,
        // which must stay aligned with the triangles).
        void build(TriSoup &soup, std::vector<uint16_t> *parallel = nullptr, int leafSize = 4);

        // Nearest hit. `hitTri`, when given, receives the index into soup.tris of the
        // triangle that produced it -- which is how WmoModel answers "whose floor is this".
        std::optional<float> raycast(const TriSoup &soup, const Vec3 &o, const Vec3 &d,
                                     float tMax, uint32_t *hitTri = nullptr) const;

        // Serialization: the node table is the whole structure.
        const std::vector<Node> &nodes() const { return m_nodes; }
        void adopt(std::vector<Node> nodes) { m_nodes = std::move(nodes); }

        size_t nodeCount() const { return m_nodes.size(); }
        size_t bytes() const { return m_nodes.size() * sizeof(Node); }
        int maxDepth() const { return m_maxDepth; }
        bool empty() const { return m_nodes.empty(); }

    private:
        int build_(const TriSoup &soup, std::vector<uint32_t> &order, uint32_t first,
                   uint32_t count, int leafSize, int depth);

        std::vector<Node> m_nodes;
        int m_maxDepth = 0;
    };

    // Bounding Interval Hierarchy. An inner node stores only TWO planes on one axis --
    // the right edge of the left child and the left edge of the right child -- instead of
    // two full boxes. Half the node memory of a BVH and no box to load per child, at the
    // cost of looser culling. Object-partitioning like the BVH: every triangle lands in
    // exactly one leaf.
    class Bih
    {
    public:
        void build(const TriSoup &soup, int leafSize = 4);
        std::optional<float> raycast(const TriSoup &soup, const Vec3 &o, const Vec3 &d,
                                     float tMax) const;

        size_t nodeCount() const { return m_nodes.size(); }
        size_t bytes() const;
        int maxDepth() const { return m_maxDepth; }
        const Aabb &bounds() const { return m_bounds; }

    private:
        struct Node
        {
            // axis 0..2 for an inner node, 3 for a leaf.
            uint8_t axis = 3;
            float clipLeft = 0.f;  // max coord of the left child on `axis`
            float clipRight = 0.f; // min coord of the right child on `axis`
            int32_t left = -1;     // inner: left child (depth-first: right is NOT left+1)
            int32_t right = -1;    // inner: right child
            uint32_t first = 0, count = 0; // leaf
        };
        int build_(const TriSoup &soup, uint32_t first, uint32_t count, const Aabb &box,
                   int leafSize, int depth);

        std::vector<Node> m_nodes;
        std::vector<uint32_t> m_order;
        Aabb m_bounds;
        int m_maxDepth = 0;
    };

    // Kd-tree, spatial (not object) partitioning: a triangle straddling the split plane is
    // referenced by BOTH children. That duplication is the price for perfectly disjoint
    // cells and the early-exit that comes with them -- the first hit found in the nearest
    // cell is final, so a downward floor probe can stop immediately. Median split with a
    // depth cap; duplication is what makes its memory the interesting number to watch.
    class KdTree
    {
    public:
        void build(const TriSoup &soup, int leafSize = 8, int maxDepth = 32);
        std::optional<float> raycast(const TriSoup &soup, const Vec3 &o, const Vec3 &d,
                                     float tMax) const;

        size_t nodeCount() const { return m_nodes.size(); }
        size_t refCount() const { return m_refs.size(); } // >= triangle count, by duplication
        size_t bytes() const;
        int maxDepth() const { return m_maxDepth; }
        const Aabb &bounds() const { return m_bounds; }

    private:
        struct Node
        {
            uint8_t axis = 3; // 3 == leaf
            float split = 0.f;
            int32_t left = -1;             // inner: left child (right is NOT left+1)
            int32_t right = -1;            // inner: right child
            uint32_t first = 0, count = 0; // leaf: range in m_refs
        };
        int build_(const TriSoup &soup, std::vector<uint32_t> &refs, const Aabb &box,
                   int leafSize, int maxDepth, int depth);

        std::vector<Node> m_nodes;
        std::vector<uint32_t> m_refs;
        Aabb m_bounds;
        int m_maxDepth = 0;
    };

} // namespace world::terrain
