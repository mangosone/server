#include "terrain/WmoModel.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace world::terrain
{

    namespace
    {
        constexpr float LIQUID_TILE_SIZE = 533.333f / 128.f;
    }

    WmoModel::WmoModel(TriSoup soup, std::vector<uint16_t> triGroup, std::vector<Group> groups,
                       uint32_t rootWmoId, Bvh bvh)
        : m_soup(std::move(soup)), m_triGroup(std::move(triGroup)), m_groups(std::move(groups)),
          m_bvh(std::move(bvh)), m_rootId(rootWmoId)
    {
        // A tile hands us a BVH already built (and soup.tris already in its order). Anything
        // else -- a synthetic model, a test -- gets one built now.
        if (m_bvh.empty() && !m_soup.tris.empty())
        {
            m_bvh.build(m_soup, &m_triGroup, 4);
        }
        deriveBounds_();
    }

    void WmoModel::deriveBounds_()
    {
        m_bounds = Aabb{};
        m_empty = true;

        for (uint32_t i = 0; i < m_soup.tris.size(); ++i)
        {
            m_empty = false;
            m_bounds.expand(m_soup.triBounds(i));
        }

        // Liquid counts as content too (so a liquid-only group isn't skipped as "empty"),
        // and its footprint must be inside the bounds for the column cull to reach it.
        for (const Group &g : m_groups)
        {
            if (!g.hasLiquid || g.liquid.heights.empty())
            {
                continue;
            }
            m_empty = false;
            float zmin = g.liquid.heights[0], zmax = g.liquid.heights[0];
            for (float hz : g.liquid.heights)
            {
                zmin = std::min(zmin, hz);
                zmax = std::max(zmax, hz);
            }
            const Vec3 c = g.liquid.corner;
            const float ex = c.x + g.liquid.tilesX * LIQUID_TILE_SIZE;
            const float ey = c.y + g.liquid.tilesY * LIQUID_TILE_SIZE;
            m_bounds.expand({c.x, c.y, zmin});
            m_bounds.expand({ex, ey, zmax});
        }
    }

    std::optional<float> WmoModel::raycastNearest(const Vec3 &origin, const Vec3 &dir,
                                                  float tMax) const
    {
        return m_bvh.raycast(m_soup, origin, dir, tMax);
    }

    std::optional<WmoModel::AreaResult> WmoModel::areaInfo(const Vec3 &origin, const Vec3 &dir,
                                                           float tMax) const
    {
        uint32_t tri = 0;
        auto t = m_bvh.raycast(m_soup, origin, dir, tMax, &tri);
        if (!t)
        {
            return std::nullopt;
        }
        // The triangle carries the group it came from, so the "which room" answer falls out
        // of the same traversal that found the floor -- no second pass over the groups.
        if (tri >= m_triGroup.size())
        {
            return std::nullopt;
        }
        const uint16_t gi = m_triGroup[tri];
        if (gi >= m_groups.size())
        {
            return std::nullopt;
        }
        return AreaResult{m_groups[gi].groupWmoId, m_groups[gi].mogpFlags, *t};
    }

    // MLIQ surface at a model-space point: locate the liquid tile, reject if the tile flag
    // says "no liquid", then triangle-interpolate the corner heights.
    std::optional<ICollisionModel::LocalLiquid> WmoModel::liquidLocal(const Vec3 &p) const
    {
        for (const Group &g : m_groups)
        {
            if (!g.hasLiquid)
            {
                continue;
            }
            const Liquid &lq = g.liquid;
            if (lq.tilesX == 0 || lq.tilesY == 0 || lq.heights.empty())
            {
                continue;
            }

            const float txf = (p.x - lq.corner.x) / LIQUID_TILE_SIZE;
            const float tyf = (p.y - lq.corner.y) / LIQUID_TILE_SIZE;
            const int tx = static_cast<int>(txf), ty = static_cast<int>(tyf);
            if (tx < 0 || tx >= static_cast<int>(lq.tilesX) || ty < 0 ||
                ty >= static_cast<int>(lq.tilesY))
            {
                continue;
            }
            const size_t fi = static_cast<size_t>(tx) + static_cast<size_t>(ty) * lq.tilesX;
            if (fi < lq.flags.size() && (lq.flags[fi] & 0x0F) == 0x0F)
            {
                continue;  // this tile carries no liquid
            }

            const float dx = txf - tx, dy = tyf - ty;
            const uint32_t row = lq.tilesX + 1;
            auto H = [&](int a, int b) { return lq.heights[a + b * row]; };
            float z;
            if (dx > dy)
            {
                const float sx = H(tx + 1, ty) - H(tx, ty);
                const float sy = H(tx + 1, ty + 1) - H(tx + 1, ty);
                z = H(tx, ty) + dx * sx + dy * sy;
            }
            else
            {
                const float sx = H(tx + 1, ty + 1) - H(tx, ty + 1);
                const float sy = H(tx, ty + 1) - H(tx, ty);
                z = H(tx, ty) + dx * sx + dy * sy;
            }
            return LocalLiquid{z, lq.entry, lq.kind};
        }
        return std::nullopt;
    }

} // namespace world::terrain
