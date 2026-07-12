#include "terrain/WmoLoader.hpp"
#include "terrain/Terrain.hpp"
#include "terrain/WmoModel.hpp"

#include "stores/LiquidTypeStore.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>

namespace world::terrain {

namespace {

    // MOPY (SMOPoly) material flags, TBC. Full bit layout, for reference:
    //   0x01 unknown      0x02 no-cam-collide   0x04 DETAIL      0x08 COLLISION
    //   0x10 hint         0x20 RENDER           0x40 unknown     0x80 collide-hit
    // Only the three the collision filter needs are named. The names matter: an earlier
    // version of this file called 0x04 "no collision", 0x08 "hint" and 0x20 "collide
    // hit", and built the filter on that mistake.
    constexpr uint8_t WMO_MATERIAL_DETAIL    = 0x04;
    constexpr uint8_t WMO_MATERIAL_COLLISION = 0x08;
    constexpr uint8_t WMO_MATERIAL_RENDER    = 0x20;

    // MOHD.flags: "the group's liquid id is a raw LiquidType.dbc id, not a legacy code".
    constexpr uint32_t MOHD_USE_LIQUID_DBC_ID = 0x4;
    // MOGP.flags: this group's liquid is ocean rather than river/lake water.
    constexpr uint32_t MOGP_LIQUID_IS_OCEAN = 0x80000;

    // Sentinel groupLiquid / MLIQ tile-flag nibble meaning "no liquid".
    constexpr uint32_t LIQUID_NONE = 15;
    // Ids at or above this are raw LiquidType.dbc rows and must not be remapped.
    constexpr uint32_t LIQUID_FIRST_DBC_ID = 21;

    std::string toLower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    uint32_t rdU32(const uint8_t* p) {
        return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) |
               (uint32_t(p[3]) << 24);
    }
    uint16_t rdU16(const uint8_t* p) { return uint16_t(p[0]) | (uint16_t(p[1]) << 8); }
    int16_t  rdI16(const uint8_t* p) { return static_cast<int16_t>(rdU16(p)); }
    float    rdF32(const uint8_t* p) {
        uint32_t u = rdU32(p);
        float f;
        std::memcpy(&f, &u, 4);
        return f;
    }
    bool tagIs(const uint8_t* p, const char* t) {  // tags are stored reversed
        return p[0] == t[3] && p[1] == t[2] && p[2] == t[1] && p[3] == t[0];
    }

    // Root: read MOHD.nGroups (2nd field, +0x04), MOHD.wmoID (+0x20, the WMOAreaTable
    // root id) and MOHD.flags (+0x3C). Field offsets follow the reference vmap-extractor's
    // read order: nTextures, nGroups, nPortals, nLights, nDoodadNames, nDoodadDefs,
    // nDoodadSets, ambColor, wmoID, bbox min[3], bbox max[3], flags.
    // Returns nGroups (0 if not found); rootWmoId / rootFlags are out-params.
    uint32_t readRootHeader(const std::vector<uint8_t>& bytes, uint32_t& rootWmoId,
                            uint32_t& rootFlags) {
        rootWmoId = 0;
        rootFlags = 0;
        const uint8_t* d = bytes.data();
        size_t pos = 0, n = bytes.size();
        while (pos + 8 <= n) {
            uint32_t sz = rdU32(d + pos + 4);
            if (pos + 8 + sz > n) break;
            if (tagIs(d + pos, "MOHD") && sz >= 8) {
                if (sz >= 0x24)
                    rootWmoId = rdU32(d + pos + 8 + 0x20);  // MOHD.wmoID
                if (sz >= 0x40)
                    rootFlags = rdU32(d + pos + 8 + 0x3C);  // MOHD.flags
                return rdU32(d + pos + 8 + 4);              // MOHD.nGroups
            }
            pos += 8 + sz;
        }
        return 0;
    }

    // Map a legacy liquid code onto the canonical LiquidType.dbc row the reference
    // vmap-extractor writes. Ids >= 21 are already real dbc ids and pass through.
    //
    // The low two bits of (entry - 1) select the family: water, ocean, magma, slime.
    // Water splits further -- the MOGP ocean bit picks row 2 over row 1 -- and two TBC
    // instances override the row outright, keyed on the WMO's path.
    uint32_t canonicalLiquidEntry(uint32_t entry, uint32_t mogpFlags,
                                  const std::string& rootPathLower) {
        if (!entry || entry >= LIQUID_FIRST_DBC_ID)
            return entry;

        switch ((entry - 1u) & 3u) {
            case 0:  // water (or ocean, per the group flag)
                entry = ((mogpFlags & MOGP_LIQUID_IS_OCEAN) != 0) ? 2u : 1u;
                if (entry == 1u && rootPathLower.find("coilfang_raid") != std::string::npos)
                    entry = 41u;  // Serpentshrine Cavern's own water row (carries an aura)
                break;
            case 1: entry = 2u; break;  // ocean
            case 2: entry = 3u; break;  // magma
            case 3:                     // slime
                entry = (rootPathLower.find("stratholme_raid") != std::string::npos) ? 21u : 4u;
                break;
            default: break;
        }
        return entry;
    }

    // Classify a LiquidType.dbc id. Rows 1..4 are the canonical water/ocean/magma/slime
    // and are known by id -- 2.4.3's LiquidType.dbc `Type` column cannot tell ocean from
    // water (it encodes 0=magma, 2=slime, 3=water), so the id is the only way. Rows >= 21
    // are instance-specific and only the dbc knows what they are.
    LiquidKind classifyLiquid(uint32_t entry, const LiquidTypeStore* store) {
        if (!entry)
            return LiquidKind::None;

        if (entry < LIQUID_FIRST_DBC_ID) {
            switch ((entry - 1u) & 3u) {
                case 0: return LiquidKind::Water;
                case 1: return LiquidKind::Ocean;
                case 2: return LiquidKind::Magma;
                case 3: return LiquidKind::Slime;
                default: break;
            }
        }

        if (store) {
            if (const LiquidTypeInfo* info = store->find(entry)) {
                switch (static_cast<LiquidDbcType>(info->type)) {
                    case LiquidDbcType::Magma: return LiquidKind::Magma;
                    case LiquidDbcType::Slime: return LiquidKind::Slime;
                    case LiquidDbcType::Water: return LiquidKind::Water;
                    default: break;
                }
            }
        }

        // No dbc to hand (a stand-alone game-object bake). The only rows >= 21 the
        // canonicalisation above can produce are these two.
        if (entry == 21u) return LiquidKind::Slime;   // Naxxramas slime
        if (entry == 41u) return LiquidKind::Water;   // Serpentshrine Cavern water
        return LiquidKind::Water;
    }

    // One group's parsed geometry, before it is folded into the model-wide soup: its own
    // MOVT vertices and just the COLLIDABLE triangles among its MOVI faces.
    struct GroupGeom {
        std::vector<Vec3>                    verts;   // MOVT
        std::vector<std::array<uint16_t, 3>> tris;    // collidable faces only
        WmoModel::Group                      meta;    // identity + liquid
    };

    // Parse one group file. MOBN/MOBR (Blizzard's BSP) are deliberately NOT read: we build
    // our own BVH over the collidable faces, which is both faster and smaller (see
    // WmoModel). A useful side effect is that a group shipping geometry but no BSP -- which
    // the old loader discarded outright, since it had no way to query it -- now collides
    // like any other. Returns false if the group carries neither collision nor liquid.
    bool parseGroup(const std::vector<uint8_t>& bytes, uint32_t rootFlags,
                    const std::string& rootPathLower, const LiquidTypeStore* liqStore,
                    GroupGeom& out) {
        const uint8_t* d = bytes.data();
        size_t pos = 0, n = bytes.size();

        const uint8_t* mopy = nullptr; uint32_t mopySize = 0;
        const uint8_t* movi = nullptr; uint32_t moviSize = 0;
        const uint8_t* movt = nullptr; uint32_t movtSize = 0;
        const uint8_t* mliq = nullptr; uint32_t mliqSize = 0;
        // MOGP 68-byte header: flags @+0x08, groupLiquid @+0x34, uniqueID @+0x38.
        const uint8_t* mogp = nullptr;

        while (pos + 8 <= n) {
            const uint8_t* tag = d + pos;
            uint32_t sz = rdU32(d + pos + 4);
            // MOGP is a container: its 68-byte header is followed by the nested
            // geometry chunks. Step *into* it (advance by header only) so the
            // chunks below get walked as if top-level — the vmap-extractor trick.
            uint32_t advance = sz;
            if (tagIs(tag, "MOGP"))      { advance = 68; mogp = d + pos + 8; }
            else if (tagIs(tag, "MOPY")) { mopy = d + pos + 8; mopySize = sz; }
            else if (tagIs(tag, "MOVI")) { movi = d + pos + 8; moviSize = sz; }
            else if (tagIs(tag, "MOVT")) { movt = d + pos + 8; movtSize = sz; }
            else if (tagIs(tag, "MLIQ")) { mliq = d + pos + 8; mliqSize = sz; }

            if (advance != 68 && pos + 8 + sz > n) break;
            pos += 8 + advance;
        }

        // MOGP header carries this group's indoor/outdoor flags, the liquid it holds, and
        // its WMOAreaTable group id (uniqueID). All live in the fixed 68-byte header.
        uint32_t groupLiquid = 0;
        if (mogp && mogp + 0x3C <= d + n) {
            out.meta.mogpFlags  = rdU32(mogp + 0x08);
            groupLiquid         = rdU32(mogp + 0x34);
            out.meta.groupWmoId = rdU32(mogp + 0x38);
        }

        // MLIQ: SMOLiquidHeader (xverts,yverts,xtiles,ytiles,baseCoords[3],materialId
        // = 30B), then (xverts*yverts) verts of {u16,u16,f32 height}, then
        // (xtiles*ytiles) tile flags.
        //
        // The trailing uint16 is a materialId, NOT the liquid type -- the reference
        // vmap-extractor declares it `type` but overwrites it before use. The liquid's
        // identity comes from MOGP.groupLiquid, resolved below. Reading it from here is
        // how WMO lava and slime end up classified as water.
        if (mliq && mliqSize >= 30) {
            const uint32_t xverts = rdU32(mliq + 0), yverts = rdU32(mliq + 4);
            const uint32_t xtiles = rdU32(mliq + 8), ytiles = rdU32(mliq + 12);
            const float px = rdF32(mliq + 16), py = rdF32(mliq + 20), pz = rdF32(mliq + 24);
            const uint64_t vbytes = uint64_t(xverts) * yverts * 8;
            const uint64_t fbytes = uint64_t(xtiles) * ytiles;
            if (xverts && yverts && xtiles && ytiles && 30 + vbytes + fbytes <= mliqSize) {
                const uint8_t* verts = mliq + 30;
                const uint8_t* flags = verts + vbytes;
                out.meta.hasLiquid = true;
                out.meta.liquid.tilesX = xtiles;
                out.meta.liquid.tilesY = ytiles;
                out.meta.liquid.corner = {px, py, pz};
                out.meta.liquid.heights.resize(uint64_t(xverts) * yverts);
                for (size_t i = 0; i < out.meta.liquid.heights.size(); ++i)
                    out.meta.liquid.heights[i] = rdF32(verts + i * 8 + 4);  // height at +4
                out.meta.liquid.flags.assign(flags, flags + fbytes);

                // Resolve the liquid id, following the reference exactly:
                //   root says "dbc id"  -> take groupLiquid verbatim
                //   groupLiquid == 15   -> no liquid
                //   otherwise           -> groupLiquid + 1 (legacy code -> row)
                uint32_t entry;
                if (rootFlags & MOHD_USE_LIQUID_DBC_ID)
                    entry = groupLiquid;
                else if (groupLiquid == LIQUID_NONE)
                    entry = 0;
                else
                    entry = groupLiquid + 1;

                // Some groups leave MOGP.groupLiquid at zero and only name the liquid in
                // the per-tile flags; take it from the first tile that has any.
                if (!entry) {
                    for (uint8_t tf : out.meta.liquid.flags) {
                        if ((tf & 0x0F) != LIQUID_NONE) {
                            entry = uint32_t(tf & 0x0F) + 1u;
                            break;
                        }
                    }
                }

                entry = canonicalLiquidEntry(entry, out.meta.mogpFlags, rootPathLower);
                out.meta.liquid.entry = static_cast<uint16_t>(entry);
                out.meta.liquid.kind =
                    static_cast<uint8_t>(classifyLiquid(entry, liqStore));
            }
        }

        // No geometry at all: keep the group only if it carried an MLIQ surface (a
        // liquid-only group), which the return below already intends to accept.
        if (!movi || !movt)
            return out.meta.hasLiquid;

        const uint32_t nVert = movtSize / 12;
        out.verts.reserve(nVert);
        for (uint32_t v = 0; v < nVert; ++v)
            out.verts.push_back({rdF32(movt + v * 12 + 0), rdF32(movt + v * 12 + 4),
                                 rdF32(movt + v * 12 + 8)});

        const uint32_t nTri = moviSize / 6;  // 3 uint16 indices per triangle
        out.tris.reserve(nTri);
        for (uint32_t t = 0; t < nTri; ++t) {
            // MOPY parallels MOVI (one 2-byte entry per triangle: flags, then materialId).
            const uint8_t flags = (mopy && 2 * t < mopySize) ? static_cast<uint8_t>(mopy[2 * t]) : 0;

            // A face collides if it is explicitly flagged COLLISION, or if it is a plain
            // rendered face that is not "detail" decoration. This is the reference
            // vmap-extractor's rule, and the COLLISION half of it matters: a face may be
            // both DETAIL and COLLISION (0x0C), and it still collides. Requiring !DETAIL
            // of every face -- as this once did -- discards exactly those.
            const bool isRenderFace = (flags & WMO_MATERIAL_RENDER) && !(flags & WMO_MATERIAL_DETAIL);
            const bool isCollision  = (flags & WMO_MATERIAL_COLLISION) || isRenderFace;

            // With no MOPY at all, take every face. Non-collidable faces are DROPPED here
            // rather than flagged: nothing downstream has any use for them, and they are
            // ~60% of a WMO.
            if (mopy && mopySize != 0 && !isCollision)
                continue;

            const uint16_t a = rdU16(movi + (3 * t + 0) * 2);
            const uint16_t b = rdU16(movi + (3 * t + 1) * 2);
            const uint16_t c = rdU16(movi + (3 * t + 2) * 2);
            if (a >= nVert || b >= nVert || c >= nVert)
                continue;
            out.tris.push_back({a, b, c});
        }

        return !out.tris.empty() || out.meta.hasLiquid;
    }

    // Doodad tables from the WMO root:
    //   MODS -- 32B per set: char name[20], uint32 startIndex, uint32 count, uint32 pad
    //   MODN -- a blob of NUL-terminated M2 filenames
    //   MODD -- 40B per doodad: uint32 (nameOffset:24 | flags:8), float pos[3],
    //           float quat[4] (x,y,z,w), float scale, uint32 colour
    // MODD's name field is a BYTE OFFSET into MODN, not an index -- reading it as an
    // index is the classic way to end up placing the wrong models.
    void readDoodads(const std::vector<uint8_t>& bytes, WmoDoodadData& out) {
        const uint8_t* d = bytes.data();
        const size_t n = bytes.size();
        const uint8_t* mods = nullptr; uint32_t modsSize = 0;
        const uint8_t* modn = nullptr; uint32_t modnSize = 0;
        const uint8_t* modd = nullptr; uint32_t moddSize = 0;

        size_t pos = 0;
        while (pos + 8 <= n) {
            const uint32_t sz = rdU32(d + pos + 4);
            if (pos + 8 + sz > n) break;
            if (tagIs(d + pos, "MODS"))      { mods = d + pos + 8; modsSize = sz; }
            else if (tagIs(d + pos, "MODN")) { modn = d + pos + 8; modnSize = sz; }
            else if (tagIs(d + pos, "MODD")) { modd = d + pos + 8; moddSize = sz; }
            pos += 8 + sz;
        }
        if (!modd || !modn || moddSize < 40)
            return;

        if (mods) {
            const uint32_t nSets = modsSize / 32;
            out.sets.reserve(nSets);
            for (uint32_t i = 0; i < nSets; ++i) {
                const uint8_t* p = mods + i * 32;
                out.sets.push_back({rdU32(p + 20), rdU32(p + 24)});
            }
        }
        // A WMO with MODD but no MODS still has an implicit single set: all of them.
        if (out.sets.empty())
            out.sets.push_back({0, moddSize / 40});

        const uint32_t nDefs = moddSize / 40;
        out.defs.reserve(nDefs);
        for (uint32_t i = 0; i < nDefs; ++i) {
            const uint8_t* p = modd + i * 40;
            const uint32_t nameOfs = rdU32(p + 0) & 0x00FFFFFFu;  // low 24 bits; high 8 = flags

            WmoDoodad dd;
            if (nameOfs < modnSize) {
                const char* s = reinterpret_cast<const char*>(modn + nameOfs);
                const size_t maxLen = modnSize - nameOfs;
                dd.name.assign(s, ::strnlen(s, maxLen));
            }
            dd.pos     = {rdF32(p + 4), rdF32(p + 8), rdF32(p + 12)};
            dd.quat[0] = rdF32(p + 16);
            dd.quat[1] = rdF32(p + 20);
            dd.quat[2] = rdF32(p + 24);
            dd.quat[3] = rdF32(p + 28);
            dd.scale   = rdF32(p + 32);
            if (!(dd.scale > 0.f))
                dd.scale = 1.f;
            out.defs.push_back(std::move(dd));
        }
    }

    // "X.wmo" -> "X_003.wmo"
    std::string groupPath(const std::string& root, uint32_t i) {
        std::string base = root;
        const size_t dot = base.find_last_of('.');
        if (dot != std::string::npos)
            base.erase(dot);
        char suf[16];
        std::snprintf(suf, sizeof(suf), "_%03u.wmo", i);
        return base + suf;
    }

}  // namespace

const WmoDoodadData* WmoLoader::doodads(const std::string& rootPath) {
    auto it = m_doodads.find(rootPath);
    if (it != m_doodads.end())
        return it->second.defs.empty() ? nullptr : &it->second;

    // Not primed yet (doodads() called without load()): read the root for it.
    std::vector<uint8_t> rootBytes;
    WmoDoodadData dd;
    if (m_archive.read(rootPath, rootBytes))
        readDoodads(rootBytes, dd);

    auto& slot = m_doodads.emplace(rootPath, std::move(dd)).first->second;
    return slot.defs.empty() ? nullptr : &slot;
}

std::shared_ptr<const ICollisionModel> WmoLoader::load(const std::string& rootPath) {
    auto it = m_cache.find(rootPath);
    if (it != m_cache.end())
        return it->second;

    std::vector<uint8_t> rootBytes;
    if (!m_archive.read(rootPath, rootBytes)) {
        m_cache.emplace(rootPath, nullptr);
        m_doodads.emplace(rootPath, WmoDoodadData{});
        return nullptr;
    }

    uint32_t rootWmoId = 0;
    uint32_t rootFlags = 0;
    const uint32_t nGroups = readRootHeader(rootBytes, rootWmoId, rootFlags);

    // Same root bytes, so read the furnishing tables while we have them.
    if (m_doodads.find(rootPath) == m_doodads.end()) {
        WmoDoodadData dd;
        readDoodads(rootBytes, dd);
        m_doodads.emplace(rootPath, std::move(dd));
    }

    // Two TBC instances override their liquid row by WMO path; match case-insensitively.
    const std::string rootPathLower = toLower(rootPath);

    // Fold every group's collidable faces into ONE soup for the whole WMO. Each triangle
    // remembers which group it came from (that is all `areaInfo` needs), and each group's
    // vertices are re-indexed as they are appended -- so the ~60% of MOVT vertices that
    // only ever fed render-only faces are simply never carried over.
    TriSoup soup;
    std::vector<uint16_t> triGroup;
    std::vector<WmoModel::Group> groups;

    for (uint32_t g = 0; g < nGroups; ++g) {
        std::vector<uint8_t> gb;
        if (!m_archive.read(groupPath(rootPath, g), gb))
            continue;

        GroupGeom geom;
        if (!parseGroup(gb, rootFlags, rootPathLower, m_liquidTypes, geom))
            continue;

        const uint16_t gi = static_cast<uint16_t>(groups.size());
        groups.push_back(std::move(geom.meta));

        // Only the vertices the kept faces actually touch make it into the soup.
        std::unordered_map<uint16_t, uint32_t> remap;
        remap.reserve(geom.tris.size() * 3);
        auto vertexOf = [&](uint16_t local) {
            auto it = remap.find(local);
            if (it != remap.end())
                return it->second;
            const uint32_t idx = static_cast<uint32_t>(soup.verts.size());
            soup.verts.push_back(geom.verts[local]);
            remap.emplace(local, idx);
            return idx;
        };

        for (const auto& t : geom.tris) {
            soup.tris.push_back({vertexOf(t[0]), vertexOf(t[1]), vertexOf(t[2])});
            triGroup.push_back(gi);
        }
    }

    // Build the BVH now, offline, and hand it to the model already built: the tile stores
    // it verbatim, so the server never pays to construct one.
    Bvh bvh;
    bvh.build(soup, &triGroup, 4);

    auto model = std::make_shared<WmoModel>(std::move(soup), std::move(triGroup),
                                            std::move(groups), rootWmoId, std::move(bvh));
    m_cache.emplace(rootPath, model);
    return model;
}

}  // namespace world::terrain
