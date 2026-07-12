#include "terrain/CollisionModel.hpp"

#include <algorithm>

namespace world::terrain
{

    namespace
    {
        Aabb triBounds(const Tri& t)
        {
            Aabb b;
            b.expand(t.a);
            b.expand(t.b);
            b.expand(t.c);
            return b;
        }
        Vec3 triCentroid(const Tri& t)
        {
            return (t.a + t.b + t.c) * (1.0f / 3.0f);
        }
    } // namespace

    CollisionModel::CollisionModel(std::vector<Vec3> verts,
                                   std::vector<std::array<uint32_t, 3>> tris)
        : m_verts(std::move(verts)), m_tris(std::move(tris))
    {
        m_order.resize(m_tris.size());
        for (uint32_t i = 0; i < m_order.size(); ++i)
            m_order[i] = i;
        for (const auto& v : m_verts)
            m_bounds.expand(v);
        if (!m_tris.empty())
        {
            m_nodes.reserve(m_tris.size() * 2);
            build(0, static_cast<int>(m_order.size()));
        }
    }

    // Median-split BVH over [start,end) of m_order. Leaves hold up to 4 triangles.
    int CollisionModel::build(int start, int end)
    {
        const int nodeIdx = static_cast<int>(m_nodes.size());
        m_nodes.emplace_back();

        Aabb box;
        for (int i = start; i < end; ++i)
            box.expand(triBounds(triangle(m_order[i])));

        const int count = end - start;
        if (count <= 4)
        {
            Node leaf;
            leaf.box = box;
            leaf.left = -1;
            leaf.first = start;
            leaf.count = count;
            m_nodes[nodeIdx] = leaf;
            return nodeIdx;
        }

        // Split on the longest axis of the centroid bounds, at the median.
        Vec3 ext = box.hi - box.lo;
        int axis = (ext.x >= ext.y && ext.x >= ext.z) ? 0 : (ext.y >= ext.z ? 1 : 2);
        const int mid = start + count / 2;
        std::nth_element(m_order.begin() + start, m_order.begin() + mid, m_order.begin() + end,
                         [&](uint32_t a, uint32_t b)
                         {
                             const Vec3 ca = triCentroid(triangle(a));
                             const Vec3 cb = triCentroid(triangle(b));
                             return (&ca.x)[axis] < (&cb.x)[axis];
                         });

        const int leftChild  = build(start, mid);
        const int rightChild = build(mid, end);

        Node node;
        node.box = box;
        node.left = leftChild;
        node.first = rightChild;  // reuse `first` to store the right child index
        node.count = 0;           // internal
        m_nodes[nodeIdx] = node;
        return nodeIdx;
    }

    std::optional<float> CollisionModel::raycastNearest(const Vec3& origin, const Vec3& dir,
                                                        float tMax) const
    {
        if (m_nodes.empty())
            return std::nullopt;

        // A FINITE stand-in for the infinite inverse, matching Accelerators. True infinity
        // is a trap here: a ray whose origin lies exactly on a slab plane computes
        // (lo - o) * inf == 0 * inf == NaN, and every comparison downstream of a NaN is
        // false, so the box test's answer becomes a coin toss.
        auto inv = [](float v) { return std::fabs(v) > 1e-9f ? 1.f / v : 1e30f; };
        const Vec3 invDir{inv(dir.x), inv(dir.y), inv(dir.z)};

        float best = tMax;
        bool  hit = false;

        int stack[64];
        int sp = 0;
        stack[sp++] = 0;
        while (sp > 0)
        {
            const Node& n = m_nodes[stack[--sp]];
            if (!n.box.intersectsRay(origin, invDir, best))
                continue;

            if (n.count > 0)  // leaf
            {
                for (int i = 0; i < n.count; ++i)
                    if (auto t = rayTri(origin, dir, triangle(m_order[n.first + i])))
                        if (*t < best)
                        {
                            best = *t;
                            hit = true;
                        }
            }
            else
            {
                // Unconditional. The build is a median split, so depth is bounded by
                // log2(tris/4) and the stack cannot overflow. The old `else if (sp + 2 <= 64)`
                // silently dropped both children when full, losing a whole subtree and any
                // hit in it -- see Bvh::raycast for the same bug.
                stack[sp++] = n.left;
                stack[sp++] = n.first;  // right child index
            }
        }
        return hit ? std::optional<float>(best) : std::nullopt;
    }

} // namespace world::terrain
