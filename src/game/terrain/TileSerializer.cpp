#include "terrain/TileSerializer.hpp"
#include "terrain/CollisionModel.hpp"
#include "terrain/WmoModel.hpp"

#include <cstdint>
#include <cstdio>
#include <unordered_map>
#include <vector>

namespace world::terrain {

namespace {

    constexpr uint32_t kMagic   = 0x58434254;  // "TBCX" (bytes T,B,C,X in file order)
    // v5: + per-MCNK area ids
    // v6: + WMO area info (MOGP flags/ids, rootId, instance adtId)
    // v7: WMO liquid carries a resolved LiquidType.dbc entry + LiquidKind, replacing the
    //     single uint16 that used to hold MLIQ's materialId and was misread as the type.
    // v8: + the collision of doodads placed INSIDE WMOs (the root's MODS/MODD sets). The
    //     payload layout is unchanged -- they are ordinary M2 instances -- but a v7 tile
    //     is missing all of them, so it must be re-baked rather than read.
    // v9: WMOs no longer carry Blizzard's MOBN/MOBR BSP, nor their render-only faces. A
    //     WMO is now one soup of COLLIDABLE triangles + a baked binned-SAH BVH over it
    //     (see WmoModel), which is both faster to query and smaller to store, since ~60%
    //     of a WMO's faces never collide with anything.
    constexpr uint32_t kVersion = 9;

    // Model kinds in the table.
    constexpr uint8_t kKindWmo = 0;  // WmoModel (collidable soup + baked BVH)
    constexpr uint8_t kKindBvh = 1;  // CollisionModel (M2 hull / synthetic)

    template <class T> bool wpod(FILE* f, const T& v) {
        return std::fwrite(&v, sizeof(T), 1, f) == 1;
    }
    template <class T> bool rpod(FILE* f, T& v) {
        return std::fread(&v, sizeof(T), 1, f) == 1;
    }
    template <class T> bool wvec(FILE* f, const std::vector<T>& v) {
        uint32_t n = static_cast<uint32_t>(v.size());
        if (std::fwrite(&n, 4, 1, f) != 1) return false;
        return n == 0 || std::fwrite(v.data(), sizeof(T), n, f) == n;
    }
    // Bounded read: refuse absurd counts so a corrupt file can't ask for GBs.
    template <class T> bool rvec(FILE* f, std::vector<T>& v) {
        uint32_t n = 0;
        if (std::fread(&n, 4, 1, f) != 1) return false;
        if (n > (1u << 28)) return false;  // > 256M elements — corrupt
        v.resize(n);
        return n == 0 || std::fread(v.data(), sizeof(T), n, f) == n;
    }

    // A group is now just identity + liquid: its geometry lives in the model-wide soup.
    bool writeGroup(FILE* f, const WmoModel::Group& g) {
        bool ok = wpod(f, g.mogpFlags) && wpod(f, g.groupWmoId);
        uint8_t hl = g.hasLiquid ? 1 : 0;
        ok = ok && wpod(f, hl);
        if (g.hasLiquid) {
            ok = ok && wpod(f, g.liquid.tilesX) && wpod(f, g.liquid.tilesY) &&
                 wpod(f, g.liquid.corner) && wpod(f, g.liquid.entry) &&
                 wpod(f, g.liquid.kind) &&
                 wvec(f, g.liquid.heights) && wvec(f, g.liquid.flags);
        }
        return ok;
    }
    bool readGroup(FILE* f, WmoModel::Group& g) {
        bool ok = rpod(f, g.mogpFlags) && rpod(f, g.groupWmoId);
        uint8_t hl = 0;
        ok = ok && rpod(f, hl);
        g.hasLiquid = hl != 0;
        if (ok && g.hasLiquid) {
            ok = rpod(f, g.liquid.tilesX) && rpod(f, g.liquid.tilesY) &&
                 rpod(f, g.liquid.corner) && rpod(f, g.liquid.entry) &&
                 rpod(f, g.liquid.kind) &&
                 rvec(f, g.liquid.heights) && rvec(f, g.liquid.flags);
        }
        return ok;
    }

}  // namespace

bool writeTile(const TerrainTile& tile, const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;

    bool ok = wpod(f, kMagic) && wpod(f, kVersion);
    ok = ok && wpod(f, tile.tx) && wpod(f, tile.ty);
    uint8_t ht = tile.hasTerrain ? 1 : 0;
    uint8_t gw = tile.isGlobalWmo ? 1 : 0;
    ok = ok && wpod(f, ht) && wpod(f, gw) && wvec(f, tile.v9) && wvec(f, tile.v8);

    ok = ok && std::fwrite(tile.holes.data(), sizeof(uint16_t), tile.holes.size(), f) ==
                   tile.holes.size();
    ok = ok && std::fwrite(tile.areaIds.data(), sizeof(uint16_t), tile.areaIds.size(), f) ==
                   tile.areaIds.size();

    // Liquid surface.
    uint8_t hl = tile.hasLiquid ? 1 : 0;
    ok = ok && wpod(f, hl) && wvec(f, tile.liquidHeight) && wvec(f, tile.liquidShow) &&
         wvec(f, tile.liquidType);

    // Deduped model table: each distinct model written once (tagged by kind), the
    // instances index it. Both WMO and M2 models round-trip.
    std::unordered_map<const ICollisionModel*, uint32_t> modelIndex;
    std::vector<const ICollisionModel*> models;
    for (const auto& inst : tile.instances) {
        const ICollisionModel* m = inst.model.get();
        if (m && !modelIndex.count(m)) {
            modelIndex[m] = static_cast<uint32_t>(models.size());
            models.push_back(m);
        }
    }
    uint32_t nModels = static_cast<uint32_t>(models.size());
    ok = ok && wpod(f, nModels);
    for (const ICollisionModel* m : models) {
        if (const auto* w = dynamic_cast<const WmoModel*>(m)) {
            ok = ok && wpod(f, kKindWmo) && wpod(f, w->rootId());
            const auto& gs = w->groups();
            uint32_t ng = static_cast<uint32_t>(gs.size());
            ok = ok && wpod(f, ng);
            for (const auto& g : gs) ok = ok && writeGroup(f, g);
            // The collidable soup, the per-triangle group ids, and the BVH built over it.
            // soup.tris is already in the BVH's leaf order, so nothing is rebuilt on load.
            ok = ok && wvec(f, w->soup().verts) && wvec(f, w->soup().tris) &&
                 wvec(f, w->triGroups()) && wvec(f, w->bvh().nodes());
        } else if (const auto* b = dynamic_cast<const CollisionModel*>(m)) {
            ok = ok && wpod(f, kKindBvh);
            ok = ok && wvec(f, b->verts()) && wvec(f, b->tris());
        } else {
            ok = false;  // unknown model type
        }
    }

    uint32_t nInst = static_cast<uint32_t>(tile.instances.size());
    ok = ok && wpod(f, nInst);
    for (const auto& inst : tile.instances) {
        ok = ok && wpod(f, inst.xf.pos);
        ok = ok && std::fwrite(inst.xf.rot.m.data(), sizeof(float), 9, f) == 9;
        ok = ok && wpod(f, inst.xf.scale);
        ok = ok && wpod(f, inst.worldBounds.lo) && wpod(f, inst.worldBounds.hi);
        auto mi = modelIndex.find(inst.model.get());
        uint32_t idx = mi != modelIndex.end() ? mi->second : 0xFFFFFFFFu;
        ok = ok && wpod(f, idx) && wpod(f, inst.adtId);
    }

    std::fclose(f);
    if (!ok) std::remove(path.c_str());  // don't leave a half-written cache file
    return ok;
}

std::shared_ptr<TerrainTile> readTile(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return nullptr;

    uint32_t magic = 0, version = 0;
    if (!rpod(f, magic) || !rpod(f, version) || magic != kMagic || version != kVersion) {
        std::fclose(f);
        return nullptr;
    }

    auto tile = std::make_shared<TerrainTile>();
    bool ok = rpod(f, tile->tx) && rpod(f, tile->ty);

    uint8_t ht = 0, gw = 0;
    ok = ok && rpod(f, ht) && rpod(f, gw);
    tile->hasTerrain = ht != 0;
    tile->isGlobalWmo = gw != 0;
    ok = ok && rvec(f, tile->v9) && rvec(f, tile->v8);
    ok = ok && std::fread(tile->holes.data(), sizeof(uint16_t), tile->holes.size(), f) ==
                   tile->holes.size();
    ok = ok && std::fread(tile->areaIds.data(), sizeof(uint16_t), tile->areaIds.size(), f) ==
                   tile->areaIds.size();

    uint8_t hl = 0;
    ok = ok && rpod(f, hl);
    tile->hasLiquid = hl != 0;
    ok = ok && rvec(f, tile->liquidHeight) && rvec(f, tile->liquidShow) &&
         rvec(f, tile->liquidType);

    uint32_t nModels = 0;
    ok = ok && rpod(f, nModels);
    std::vector<std::shared_ptr<const ICollisionModel>> models;
    if (ok && nModels <= (1u << 20)) {
        models.resize(nModels);
        for (uint32_t i = 0; ok && i < nModels; ++i) {
            uint8_t kind = 0;
            ok = ok && rpod(f, kind);
            if (!ok) break;
            if (kind == kKindWmo) {
                uint32_t rootId = 0, ng = 0;
                ok = ok && rpod(f, rootId) && rpod(f, ng);
                if (!ok || ng > (1u << 20)) { ok = false; break; }
                std::vector<WmoModel::Group> groups(ng);
                for (uint32_t g = 0; ok && g < ng; ++g) ok = ok && readGroup(f, groups[g]);

                TriSoup soup;
                std::vector<uint16_t> triGroup;
                std::vector<Bvh::Node> nodes;
                ok = ok && rvec(f, soup.verts) && rvec(f, soup.tris) && rvec(f, triGroup) &&
                     rvec(f, nodes);
                if (ok && triGroup.size() != soup.tris.size()) ok = false;
                if (ok) {
                    Bvh bvh;
                    bvh.adopt(std::move(nodes));
                    models[i] = std::make_shared<WmoModel>(std::move(soup), std::move(triGroup),
                                                          std::move(groups), rootId,
                                                          std::move(bvh));
                }
            } else if (kind == kKindBvh) {
                std::vector<Vec3> verts;
                std::vector<std::array<uint32_t, 3>> tris;
                ok = ok && rvec(f, verts) && rvec(f, tris);
                if (ok) models[i] = std::make_shared<CollisionModel>(std::move(verts),
                                                                     std::move(tris));
            } else {
                ok = false;
            }
        }
    } else {
        ok = false;
    }

    uint32_t nInst = 0;
    ok = ok && rpod(f, nInst);
    if (ok && nInst <= (1u << 22)) {
        tile->instances.reserve(nInst);
        for (uint32_t i = 0; ok && i < nInst; ++i) {
            StaticInstance inst;
            ok = ok && rpod(f, inst.xf.pos);
            ok = ok && std::fread(inst.xf.rot.m.data(), sizeof(float), 9, f) == 9;
            ok = ok && rpod(f, inst.xf.scale);
            ok = ok && rpod(f, inst.worldBounds.lo) && rpod(f, inst.worldBounds.hi);
            uint32_t idx = 0;
            ok = ok && rpod(f, idx) && rpod(f, inst.adtId);
            if (ok && idx < models.size()) inst.model = models[idx];
            if (ok) tile->instances.push_back(std::move(inst));
        }
    } else {
        ok = false;
    }

    std::fclose(f);
    return ok ? tile : nullptr;
}

}  // namespace world::terrain
