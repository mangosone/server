#pragma once

// A WMO's collision, queried through a BVH we build over its COLLIDABLE faces alone.
//
// This replaces the previous WmoBspModel, which traversed the axis-aligned BSP Blizzard
// ships in each group's MOBN/MOBR. Keeping the authored tree sounded principled, but it
// was measured (mangos-accelbench) and it is the wrong structure for this job:
//
//   * Its nodes carry split planes and nothing else -- no bounding box. A traversal
//     therefore cannot tell that a ray misses a group; it just descends. And a WMO is
//     many groups (Black Temple: 49), each with its own tree, all of which were walked
//     for every ray. Bolting a per-group AABB gate onto it recovered 13.7x on its own.
//   * The BSP indexes EVERY face, render-only ones included, and only ~40% of a WMO's
//     faces are collidable. So it carries ~2.5x the geometry collision needs, and the
//     MOPY filter had to be re-tested at each leaf face it touched.
//
// A binned-SAH BVH over just the collidable faces beats even the gated BSP by a further
// ~2.7x, and lets the other ~60% of the triangles be dropped from the .tile entirely. A
// kd-tree was faster still (~5.9x) but duplicates straddling faces into both children and
// cost 6x the memory, which is not a trade a server holding many maps resident can make.
//
// The BVH is built OFFLINE by the baker and stored in the tile, so nothing is rebuilt at
// load. Geometry lives in one model-wide soup rather than per group; each triangle
// carries the index of the group it came from, which is all `areaInfo` needs to answer
// "which room am I standing in".

#include "terrain/Accelerators.hpp"
#include "terrain/Geometry.hpp"
#include "terrain/ICollisionModel.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace world::terrain
{

    class WmoModel : public ICollisionModel
    {
    public:
        // A group's interior liquid surface (MLIQ), in model space. iTilesX*iTilesY tiles
        // from `corner`, each LIQUID_TILE_SIZE wide; heights at the (tilesX+1) x (tilesY+1)
        // corners; per-tile flags ((& 0x0F) == 0x0F means "no liquid here").
        struct Liquid
        {
            uint32_t tilesX = 0, tilesY = 0;
            Vec3 corner;
            // Resolved by the baker from MOGP.groupLiquid plus the root MOHD flags -- NOT
            // from MLIQ's trailing uint16, which is a materialId. `entry` is a
            // LiquidType.dbc id (0 = none); `kind` is the LiquidKind it classifies to.
            uint16_t entry = 0;
            uint8_t kind = 0;
            std::vector<float> heights;  // (tilesX+1)*(tilesY+1)
            std::vector<uint8_t> flags;  // tilesX*tilesY
        };

        // What survives of a WMO group now that its geometry has been folded into the
        // model-wide soup: its identity, and any liquid it holds. A group with neither
        // collidable faces nor liquid is not kept at all.
        struct Group
        {
            uint32_t mogpFlags = 0;   // MOGP header flags (indoor/outdoor/...)
            uint32_t groupWmoId = 0;  // MOGP uniqueID (WMOAreaTable group id)
            bool hasLiquid = false;
            Liquid liquid;
        };

        WmoModel() = default;

        // `soup` holds the collidable faces in model space; `triGroup[i]` is the index into
        // `groups` of the group face i came from. `bvh` must already be built over `soup`
        // (which means soup.tris is in the BVH's permuted order) -- the baker builds it, the
        // serializer restores it. Pass an unbuilt Bvh to have one built here.
        WmoModel(TriSoup soup, std::vector<uint16_t> triGroup, std::vector<Group> groups,
                 uint32_t rootWmoId, Bvh bvh = Bvh{});

        std::optional<float> raycastNearest(const Vec3 &origin, const Vec3 &dir,
                                            float tMax) const override;
        std::optional<LocalLiquid> liquidLocal(const Vec3 &pModel) const override;
        const Aabb &bounds() const override { return m_bounds; }
        bool empty() const override { return m_empty; }

        size_t triangleCount() const { return m_soup.tris.size(); }
        uint32_t rootId() const { return m_rootId; }  // MOHD wmoID (WMOAreaTable root id)

        const std::vector<Group> &groups() const { return m_groups; }
        const TriSoup &soup() const { return m_soup; }
        const std::vector<uint16_t> &triGroups() const { return m_triGroup; }
        const Bvh &bvh() const { return m_bvh; }

        // Area info of the WMO group whose surface the model-space ray (origin, dir) hits
        // nearest within tMax -- i.e. "which room am I standing in". Returns that group's
        // WMOAreaTable group id + MOGP flags (indoor/outdoor) and the hit ray parameter t
        // (so the caller can turn it into a world floor Z). nullopt if the ray hits nothing.
        struct AreaResult
        {
            uint32_t groupId = 0;
            uint32_t mogpFlags = 0;
            float t = 0.f;
        };
        std::optional<AreaResult> areaInfo(const Vec3 &origin, const Vec3 &dir, float tMax) const;

    private:
        void deriveBounds_();

        TriSoup m_soup;
        std::vector<uint16_t> m_triGroup;  // parallel to m_soup.tris
        std::vector<Group> m_groups;
        Bvh m_bvh;
        Aabb m_bounds;
        bool m_empty = true;
        uint32_t m_rootId = 0;
    };

} // namespace world::terrain
