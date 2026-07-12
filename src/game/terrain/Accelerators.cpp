#include "terrain/Accelerators.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <type_traits>

namespace world::terrain
{

    namespace
    {
        constexpr float kInf = std::numeric_limits<float>::max();

        inline float axisOf(const Vec3 &v, int a) { return (&v.x)[a]; }

        inline int widestAxis(const Aabb &b)
        {
            const float dx = b.hi.x - b.lo.x, dy = b.hi.y - b.lo.y, dz = b.hi.z - b.lo.z;
            return (dx > dy) ? ((dx > dz) ? 0 : 2) : ((dy > dz) ? 1 : 2);
        }

        inline float surface(const Aabb &b)
        {
            if (!b.valid())
            {
                return 0.f;
            }
            const float dx = std::max(0.f, b.hi.x - b.lo.x);
            const float dy = std::max(0.f, b.hi.y - b.lo.y);
            const float dz = std::max(0.f, b.hi.z - b.lo.z);
            return 2.f * (dx * dy + dy * dz + dz * dx);
        }
    } // namespace

    // -----------------------------------------------------------------------------
    // BVH -- binned SAH
    // -----------------------------------------------------------------------------

    static_assert(std::is_trivially_copyable<Bvh::Node>::value,
                  "Bvh::Node must stay trivially copyable (it is written raw to the .tile)");

    void Bvh::build(TriSoup &soup, std::vector<uint16_t> *parallel, int leafSize)
    {
        m_nodes.clear();
        m_maxDepth = 0;
        if (soup.tris.empty())
        {
            return;
        }

        std::vector<uint32_t> order(soup.tris.size());
        for (uint32_t i = 0; i < order.size(); ++i)
        {
            order[i] = i;
        }
        m_nodes.reserve(2 * soup.tris.size() / std::max(1, leafSize) + 8);
        build_(soup, order, 0, static_cast<uint32_t>(order.size()), leafSize, 0);

        // Apply the permutation the build settled on, so every leaf's [first,count) is a
        // contiguous run of soup.tris and no indirection survives into the query.
        std::vector<std::array<uint32_t, 3>> tris(order.size());
        for (size_t i = 0; i < order.size(); ++i)
        {
            tris[i] = soup.tris[order[i]];
        }
        soup.tris.swap(tris);

        if (parallel)
        {
            std::vector<uint16_t> p(order.size());
            for (size_t i = 0; i < order.size(); ++i)
            {
                p[i] = (*parallel)[order[i]];
            }
            parallel->swap(p);
        }
    }

    int Bvh::build_(const TriSoup &soup, std::vector<uint32_t> &m_order, uint32_t first,
                    uint32_t count, int leafSize, int depth)
    {
        m_maxDepth = std::max(m_maxDepth, depth);

        const int self = static_cast<int>(m_nodes.size());
        m_nodes.push_back(Node{});

        Aabb box, centroids;
        for (uint32_t i = 0; i < count; ++i)
        {
            const uint32_t t = m_order[first + i];
            box.expand(soup.triBounds(t));
            centroids.expand(soup.centroid(t));
        }
        m_nodes[self].box = box;

        if (count <= static_cast<uint32_t>(leafSize) || depth > kMaxDepth)
        {
            m_nodes[self].left = -1;
            m_nodes[self].first = first;
            m_nodes[self].count = count;
            return self;
        }

        // Binned SAH over the widest centroid axis: 16 buckets, pick the cheapest split.
        const int axis = widestAxis(centroids);
        const float lo = axisOf(centroids.lo, axis), hi = axisOf(centroids.hi, axis);
        uint32_t mid = first + count / 2;

        if (hi - lo > 1e-6f)
        {
            constexpr int kBins = 16;
            struct Bin { Aabb box; uint32_t n = 0; };
            Bin bins[kBins];
            const float scale = kBins / (hi - lo);

            for (uint32_t i = 0; i < count; ++i)
            {
                const uint32_t t = m_order[first + i];
                int b = static_cast<int>((axisOf(soup.centroid(t), axis) - lo) * scale);
                b = std::min(kBins - 1, std::max(0, b));
                bins[b].n++;
                bins[b].box.expand(soup.triBounds(t));
            }

            // Sweep to get, for each of the 15 planes, the cost of splitting there.
            float leftArea[kBins], rightArea[kBins];
            uint32_t leftCnt[kBins], rightCnt[kBins];
            Aabb acc;
            uint32_t n = 0;
            for (int b = 0; b < kBins; ++b)
            {
                acc.expand(bins[b].box);
                n += bins[b].n;
                leftArea[b] = surface(acc);
                leftCnt[b] = n;
            }
            acc = Aabb{};
            n = 0;
            for (int b = kBins - 1; b >= 0; --b)
            {
                acc.expand(bins[b].box);
                n += bins[b].n;
                rightArea[b] = surface(acc);
                rightCnt[b] = n;
            }

            float bestCost = kInf;
            int bestBin = -1;
            for (int b = 0; b < kBins - 1; ++b)
            {
                if (!leftCnt[b] || !rightCnt[b + 1])
                {
                    continue;
                }
                const float cost = leftArea[b] * leftCnt[b] + rightArea[b + 1] * rightCnt[b + 1];
                if (cost < bestCost)
                {
                    bestCost = cost;
                    bestBin = b;
                }
            }

            if (bestBin >= 0)
            {
                auto *beg = m_order.data() + first;
                auto *end = beg + count;
                auto *split = std::partition(beg, end, [&](uint32_t t) {
                    int b = static_cast<int>((axisOf(soup.centroid(t), axis) - lo) * scale);
                    b = std::min(kBins - 1, std::max(0, b));
                    return b <= bestBin;
                });
                mid = first + static_cast<uint32_t>(split - beg);
            }
        }

        if (mid == first || mid == first + count)
        {
            mid = first + count / 2; // degenerate (coincident centroids): split by median
        }

        const int l = build_(soup, m_order, first, mid - first, leafSize, depth + 1);
        const int r = build_(soup, m_order, mid, first + count - mid, leafSize, depth + 1);
        m_nodes[self].left = l;
        m_nodes[self].right = r;
        m_nodes[self].count = 0;
        return self;
    }

    std::optional<float> Bvh::raycast(const TriSoup &soup, const Vec3 &o, const Vec3 &d,
                                      float tMax, uint32_t *hitTri) const
    {
        if (m_nodes.empty())
        {
            return std::nullopt;
        }
        auto inv = [](float v) { return std::fabs(v) > 1e-9f ? 1.f / v : 1e30f; };
        const Vec3 invDir{inv(d.x), inv(d.y), inv(d.z)};

        float best = tMax;
        uint32_t bestTri = 0;
        // Sized from the build's depth cap: this DFS pops one node and pushes two, so it
        // nets at most +1 entry per level and cannot overflow. See Bvh::kMaxDepth.
        int stack[kMaxDepth + 16];
        int sp = 0;
        stack[sp++] = 0;

        while (sp)
        {
            const Node &n = m_nodes[stack[--sp]];
            if (!n.box.intersectsRay(o, invDir, best))
            {
                continue;
            }
            if (n.left < 0)
            {
                // The leaf's triangles are contiguous in the soup -- build() permuted them
                // there, so this is a straight walk with no index chase.
                for (uint32_t i = n.first; i < n.first + n.count; ++i)
                {
                    if (auto t = rayTri(o, d, soup.at(i)))
                    {
                        if (*t >= 0.f && *t < best)
                        {
                            best = *t;
                            bestTri = i;
                        }
                    }
                }
                continue;
            }
            // Unconditional: the stack is sized from kMaxDepth so it cannot overflow. This
            // used to read `if (sp + 2 <= 64)`, which on a full stack SILENTLY DROPPED both
            // children -- discarding an entire subtree, and any hit inside it, with no
            // diagnostic. A broadphase must never quietly lose geometry.
            stack[sp++] = n.left;
            stack[sp++] = n.right;
        }
        if (best >= tMax)
        {
            return std::nullopt;
        }
        if (hitTri)
        {
            *hitTri = bestTri;
        }
        return best;
    }

    // -----------------------------------------------------------------------------
    // BIH
    // -----------------------------------------------------------------------------

    size_t Bih::bytes() const
    {
        return m_nodes.size() * sizeof(Node) + m_order.size() * sizeof(uint32_t);
    }

    void Bih::build(const TriSoup &soup, int leafSize)
    {
        m_nodes.clear();
        m_order.clear();
        m_bounds = Aabb{};
        m_maxDepth = 0;
        if (soup.tris.empty())
        {
            return;
        }
        m_order.resize(soup.tris.size());
        for (uint32_t i = 0; i < m_order.size(); ++i)
        {
            m_order[i] = i;
            m_bounds.expand(soup.triBounds(i));
        }
        m_nodes.reserve(2 * soup.tris.size() / std::max(1, leafSize) + 8);
        build_(soup, 0, static_cast<uint32_t>(m_order.size()), m_bounds, leafSize, 0);
    }

    int Bih::build_(const TriSoup &soup, uint32_t first, uint32_t count, const Aabb &box,
                    int leafSize, int depth)
    {
        m_maxDepth = std::max(m_maxDepth, depth);

        const int self = static_cast<int>(m_nodes.size());
        m_nodes.push_back(Node{});

        if (count <= static_cast<uint32_t>(leafSize) || depth > 48)
        {
            m_nodes[self].axis = 3;
            m_nodes[self].first = first;
            m_nodes[self].count = count;
            return self;
        }

        // Split at the spatial midpoint of the widest axis, partitioning by centroid --
        // the classic BIH build. The two clip planes then record how far each child's
        // geometry ACTUALLY reaches, which is what recovers the culling a plain midpoint
        // split would lose.
        const int axis = widestAxis(box);
        const float mid = 0.5f * (axisOf(box.lo, axis) + axisOf(box.hi, axis));

        auto *beg = m_order.data() + first;
        auto *end = beg + count;
        auto *split = std::partition(
            beg, end, [&](uint32_t t) { return axisOf(soup.centroid(t), axis) < mid; });
        uint32_t nLeft = static_cast<uint32_t>(split - beg);
        if (nLeft == 0 || nLeft == count)
        {
            nLeft = count / 2; // all centroids on one side: fall back to a median split
        }

        float clipLeft = -kInf, clipRight = kInf;
        Aabb lbox, rbox;
        for (uint32_t i = 0; i < count; ++i)
        {
            const uint32_t t = m_order[first + i];
            const Aabb tb = soup.triBounds(t);
            if (i < nLeft)
            {
                clipLeft = std::max(clipLeft, axisOf(tb.hi, axis));
                lbox.expand(tb);
            }
            else
            {
                clipRight = std::min(clipRight, axisOf(tb.lo, axis));
                rbox.expand(tb);
            }
        }

        m_nodes[self].axis = static_cast<uint8_t>(axis);
        m_nodes[self].clipLeft = clipLeft;
        m_nodes[self].clipRight = clipRight;

        const int l = build_(soup, first, nLeft, lbox, leafSize, depth + 1);
        const int r = build_(soup, first + nLeft, count - nLeft, rbox, leafSize, depth + 1);
        m_nodes[self].left = l;
        m_nodes[self].right = r;
        return self;
    }

    std::optional<float> Bih::raycast(const TriSoup &soup, const Vec3 &o, const Vec3 &d,
                                      float tMax) const
    {
        if (m_nodes.empty())
        {
            return std::nullopt;
        }
        auto inv = [](float v) { return std::fabs(v) > 1e-9f ? 1.f / v : 1e30f; };
        const Vec3 invDir{inv(d.x), inv(d.y), inv(d.z)};

        // Clip the ray to the root box first; the traversal below works in [tmin,tmax].
        float t0 = 0.f, t1 = tMax;
        for (int a = 0; a < 3; ++a)
        {
            const float io = (&invDir.x)[a];
            float ta = (axisOf(m_bounds.lo, a) - axisOf(o, a)) * io;
            float tb = (axisOf(m_bounds.hi, a) - axisOf(o, a)) * io;
            if (ta > tb)
            {
                std::swap(ta, tb);
            }
            t0 = std::max(t0, ta);
            t1 = std::min(t1, tb);
        }
        if (t0 > t1)
        {
            return std::nullopt;
        }

        float best = tMax;
        struct Item { int node; float tmin, tmax; };
        Item stack[64];
        int sp = 0;
        stack[sp++] = {0, t0, t1};

        while (sp)
        {
            Item it = stack[--sp];
            if (it.tmin >= best)
            {
                continue;
            }
            const Node &n = m_nodes[it.node];

            if (n.axis == 3)
            {
                for (uint32_t i = 0; i < n.count; ++i)
                {
                    if (auto t = rayTri(o, d, soup.at(m_order[n.first + i])))
                    {
                        if (*t >= 0.f && *t < best)
                        {
                            best = *t;
                        }
                    }
                }
                continue;
            }

            const int a = n.axis;
            const float oa = axisOf(o, a), da = axisOf(d, a), ia = (&invDir.x)[a];

            // Each child is entered only over the sub-interval where the ray is on the
            // near side of that child's clip plane.
            float lmin = it.tmin, lmax = it.tmax;  // left child: coord <= clipLeft
            float rmin = it.tmin, rmax = it.tmax;  // right child: coord >= clipRight
            const float tL = (n.clipLeft - oa) * ia;
            const float tR = (n.clipRight - oa) * ia;

            if (da > 0.f)
            {
                lmax = std::min(lmax, tL);
                rmin = std::max(rmin, tR);
            }
            else if (da < 0.f)
            {
                lmin = std::max(lmin, tL);
                rmax = std::min(rmax, tR);
            }
            else
            {
                if (oa > n.clipLeft)  { lmax = -kInf; }  // parallel and outside
                if (oa < n.clipRight) { rmax = -kInf; }
            }

            const bool hitL = lmin <= lmax;
            const bool hitR = rmin <= rmax;

            // Push the FAR child first so the near one is popped and tested first.
            const bool leftFirst = (da >= 0.f);
            if (leftFirst)
            {
                if (hitR && sp < 64) { stack[sp++] = {n.right, rmin, rmax}; }
                if (hitL && sp < 64) { stack[sp++] = {n.left, lmin, lmax}; }
            }
            else
            {
                if (hitL && sp < 64) { stack[sp++] = {n.left, lmin, lmax}; }
                if (hitR && sp < 64) { stack[sp++] = {n.right, rmin, rmax}; }
            }
        }
        return best < tMax ? std::optional<float>(best) : std::nullopt;
    }

    // -----------------------------------------------------------------------------
    // Kd-tree
    // -----------------------------------------------------------------------------

    size_t KdTree::bytes() const
    {
        return m_nodes.size() * sizeof(Node) + m_refs.size() * sizeof(uint32_t);
    }

    void KdTree::build(const TriSoup &soup, int leafSize, int maxDepth)
    {
        m_nodes.clear();
        m_refs.clear();
        m_bounds = Aabb{};
        m_maxDepth = 0;
        if (soup.tris.empty())
        {
            return;
        }
        std::vector<uint32_t> refs(soup.tris.size());
        for (uint32_t i = 0; i < refs.size(); ++i)
        {
            refs[i] = i;
            m_bounds.expand(soup.triBounds(i));
        }
        build_(soup, refs, m_bounds, leafSize, maxDepth, 0);
    }

    int KdTree::build_(const TriSoup &soup, std::vector<uint32_t> &refs, const Aabb &box,
                       int leafSize, int maxDepth, int depth)
    {
        m_maxDepth = std::max(m_maxDepth, depth);

        const int self = static_cast<int>(m_nodes.size());
        m_nodes.push_back(Node{});

        if (refs.size() <= static_cast<size_t>(leafSize) || depth >= maxDepth)
        {
            m_nodes[self].axis = 3;
            m_nodes[self].first = static_cast<uint32_t>(m_refs.size());
            m_nodes[self].count = static_cast<uint32_t>(refs.size());
            m_refs.insert(m_refs.end(), refs.begin(), refs.end());
            return self;
        }

        const int axis = widestAxis(box);
        const float split = 0.5f * (axisOf(box.lo, axis) + axisOf(box.hi, axis));

        // Spatial split: a triangle whose bbox crosses the plane goes to BOTH sides.
        std::vector<uint32_t> lref, rref;
        lref.reserve(refs.size());
        rref.reserve(refs.size());
        for (uint32_t t : refs)
        {
            const Aabb tb = soup.triBounds(t);
            if (axisOf(tb.lo, axis) <= split)
            {
                lref.push_back(t);
            }
            if (axisOf(tb.hi, axis) >= split)
            {
                rref.push_back(t);
            }
        }

        // No progress (everything straddles): stop rather than recurse forever.
        if (lref.size() == refs.size() && rref.size() == refs.size())
        {
            m_nodes[self].axis = 3;
            m_nodes[self].first = static_cast<uint32_t>(m_refs.size());
            m_nodes[self].count = static_cast<uint32_t>(refs.size());
            m_refs.insert(m_refs.end(), refs.begin(), refs.end());
            return self;
        }

        Aabb lbox = box, rbox = box;
        (&lbox.hi.x)[axis] = split;
        (&rbox.lo.x)[axis] = split;

        m_nodes[self].axis = static_cast<uint8_t>(axis);
        m_nodes[self].split = split;

        const int l = build_(soup, lref, lbox, leafSize, maxDepth, depth + 1);
        const int r = build_(soup, rref, rbox, leafSize, maxDepth, depth + 1);
        m_nodes[self].left = l;
        m_nodes[self].right = r;
        return self;
    }

    std::optional<float> KdTree::raycast(const TriSoup &soup, const Vec3 &o, const Vec3 &d,
                                         float tMax) const
    {
        if (m_nodes.empty())
        {
            return std::nullopt;
        }
        auto inv = [](float v) { return std::fabs(v) > 1e-9f ? 1.f / v : 1e30f; };
        const Vec3 invDir{inv(d.x), inv(d.y), inv(d.z)};

        float t0 = 0.f, t1 = tMax;
        for (int a = 0; a < 3; ++a)
        {
            const float io = (&invDir.x)[a];
            float ta = (axisOf(m_bounds.lo, a) - axisOf(o, a)) * io;
            float tb = (axisOf(m_bounds.hi, a) - axisOf(o, a)) * io;
            if (ta > tb)
            {
                std::swap(ta, tb);
            }
            t0 = std::max(t0, ta);
            t1 = std::min(t1, tb);
        }
        if (t0 > t1)
        {
            return std::nullopt;
        }

        struct Item { int node; float tmin, tmax; };
        Item stack[64];
        int sp = 0;
        stack[sp++] = {0, t0, t1};

        float best = tMax;
        while (sp)
        {
            Item it = stack[--sp];
            if (it.tmin >= best)
            {
                continue;
            }
            const Node &n = m_nodes[it.node];

            if (n.axis == 3)
            {
                for (uint32_t i = 0; i < n.count; ++i)
                {
                    if (auto t = rayTri(o, d, soup.at(m_refs[n.first + i])))
                    {
                        // No clamp to this cell's [tmin,tmax]. It is tempting -- cells are
                        // disjoint, so "the hit must lie in this cell" looks like an
                        // invariant -- but a straddling triangle is duplicated into both
                        // children and can legitimately be hit just outside the cell it was
                        // found in, and clamping then throws that hit away in BOTH cells.
                        // Taking the global minimum over every cell the ray crosses is
                        // correct regardless, because a triangle the ray hits is always in
                        // some cell the ray crosses.
                        if (*t >= 0.f && *t < best)
                        {
                            best = *t;
                        }
                    }
                }
                continue;
            }

            const int a = n.axis;
            const float oa = axisOf(o, a), da = axisOf(d, a);
            const bool nearIsLeft = (oa < n.split || (oa == n.split && da <= 0.f));
            const int nearChild = nearIsLeft ? n.left : n.right;
            const int farChild = nearIsLeft ? n.right : n.left;

            if (std::fabs(da) < 1e-9f)
            {
                // Parallel to the plane: only the side the origin is on can be entered.
                if (sp < 64)
                {
                    stack[sp++] = {nearChild, it.tmin, it.tmax};
                }
                continue;
            }

            const float tSplit = (n.split - oa) * (&invDir.x)[a];

            // tSplit <= 0 means the plane lies BEHIND the origin: going forward the ray
            // never reaches it, so it stays on the origin's (near) side for the whole
            // interval. Treating that as "crossed already, go far" -- which `tSplit <=
            // tmin` alone does, since tmin >= 0 -- sends the ray into the wrong half of
            // the tree and silently loses every triangle on the side it is actually on.
            if (tSplit <= 0.f || tSplit >= it.tmax)
            {
                stack[sp++] = {nearChild, it.tmin, it.tmax};
            }
            else if (tSplit <= it.tmin)
            {
                stack[sp++] = {farChild, it.tmin, it.tmax};
            }
            else
            {
                if (sp + 2 <= 64)
                {
                    stack[sp++] = {farChild, tSplit, it.tmax};
                    stack[sp++] = {nearChild, it.tmin, tSplit};
                }
            }
        }
        return best < tMax ? std::optional<float>(best) : std::nullopt;
    }

} // namespace world::terrain
