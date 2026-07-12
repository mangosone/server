#include "terrain/MpqTileSource.hpp"
#include "terrain/AdtParser.hpp"
#include "terrain/WdtParser.hpp"

#include <cmath>

namespace world::terrain
{

    namespace
    {

        constexpr float kDeg2Rad = 3.14159265358979323846f / 180.0f;

        // World-space AABB of a model placed by `xf`: transform the 8 corners of the
        // model-local bounds and expand. Used only for cheap column/vertical culling.
        Aabb worldBounds(const Aabb &local, const Transform &xf)
        {
            Aabb out;
            if (!local.valid())
                return out;
            for (int i = 0; i < 8; ++i)
            {
                Vec3 c{(i & 1) ? local.hi.x : local.lo.x,
                       (i & 2) ? local.hi.y : local.lo.y,
                       (i & 4) ? local.hi.z : local.lo.z};
                out.expand(xf.localToWorld(c));
            }
            return out;
        }

        // Build the placement Transform in the SAME world frame as the terrain (the
        // player/Map coordinate system).
        // In player space:
        //   pos   = ( mid - pos.z, mid - pos.x, pos.y )
        //   R     = Rz(180°) · Rz(rot.y) · Ry(rot.x) · Rx(rot.z)
        // i.e. the runtime rotation R = Rz(rot.y)Ry(rot.x)Rx(rot.z) with 180° added to
        // the Z euler (the diag(-1,-1,1) axis flip is exactly a half-turn about Z).
        constexpr float kMid = MAP_CENTER * TILE_SIZE; // 32 * 533.33333

        Transform placementTransform(const AdtPlacement &p)
        {
            Transform xf;
            xf.pos = {kMid - p.pos.z, kMid - p.pos.x, p.pos.y};
            xf.rot = Mat3::fromEuler(p.rotDeg.z * kDeg2Rad,
                                     p.rotDeg.x * kDeg2Rad,
                                     (p.rotDeg.y + 180.0f) * kDeg2Rad);
            xf.scale = p.scale;
            return xf;
        }

        // Placement of a WDT global WMO. Same MODF rotation convention as the per-tile
        // ADT case, but a global WMO's MODF is already in world coordinates, so the
        // position is used directly -- no kMid re-centring. worldBounds are baked from
        // this xf, and the runtime pulls world queries back through it (worldToLocal),
        // so the query must never assume the model already lives in world space.
        Transform placementTransformGlobalWmo(const AdtPlacement &p)
        {
            Transform xf;
            xf.pos = {p.pos.z, p.pos.x, p.pos.y};
            xf.rot = Mat3::fromEuler(p.rotDeg.z * kDeg2Rad,
                                     p.rotDeg.x * kDeg2Rad,
                                     (p.rotDeg.y+180.0f) * kDeg2Rad);
            xf.scale = p.scale;
            return xf;
        }

        // Transform for a doodad placed inside a WMO, composed with that WMO's own
        // placement so the result maps the doodad's hull straight to world space.
        //
        // Two spaces meet here and the join is easy to get silently wrong:
        //
        //  * MODD gives pos/quat/scale in the WMO's MODEL space -- the same space MOVT
        //    lives in -- and its quaternion is authored against the M2's RAW model space.
        //  * M2Loader, however, stores hull vertices Y-NEGATED (see its comment): that is
        //    the fix which lets an ADT-level MDDF doodad reuse the WMO-style placement
        //    transform. Those stored vertices are therefore N*v_raw, N = diag(1,-1,1).
        //
        // So the rotation that acts on the STORED vertices is R(quat) * N, not R(quat):
        //   v_wmo = pos + scale * R * v_raw
        //         = pos + scale * R * N * v_stored        (N is its own inverse)
        // Skip the N and every doodad comes out mirrored about the model's Y axis --
        // which mostly still overlaps its own bounding box, so it looks plausible and
        // quietly puts the collision in the wrong place.
        //
        // R*N is orthogonal but improper (det = -1). That is fine throughout: Transform
        // inverts rotations with transpose(), which is the inverse of any orthogonal
        // matrix, and rayTri is two-sided so the flipped winding does not matter.
        Transform wmoDoodadTransform(const Transform &wmoXf, const WmoDoodad &d)
        {
            Mat3 r = Mat3::fromQuat(d.quat[0], d.quat[1], d.quat[2], d.quat[3]);
            // r = r * diag(1,-1,1): negate the middle COLUMN (Mat3 is row-stored).
            r.m[1] = -r.m[1];
            r.m[4] = -r.m[4];
            r.m[7] = -r.m[7];

            Transform xf;
            xf.pos = wmoXf.localToWorld(d.pos);
            xf.rot = Mat3::mulm(wmoXf.rot, r);
            xf.scale = wmoXf.scale * d.scale;
            return xf;
        }

    } // namespace

    void MpqTileSource::attachWmoDoodads(const AdtPlacement &p, const std::string &wmoPath,
                                         const Transform &wmoXf, TerrainTile &tile)
    {
        const WmoDoodadData *dd = m_wmo.doodads(wmoPath);
        if (!dd || dd->sets.empty())
            return;

        // The placement names the one furnishing set that actually exists in the world.
        // Baking every set would stack alternative furniture in the same room.
        const uint32_t setIdx = (p.doodadSet < dd->sets.size()) ? p.doodadSet : 0u;
        const WmoDoodadSet &set = dd->sets[setIdx];

        const uint64_t end = uint64_t(set.start) + set.count;
        for (uint64_t i = set.start; i < end && i < dd->defs.size(); ++i)
        {
            const WmoDoodad &d = dd->defs[i];
            if (d.name.empty())
                continue;
            auto model = m_m2.load(d.name);
            if (!model || model->empty())
                continue;   // no collision hull -- grass, banners, most small props

            StaticInstance inst;
            inst.xf = wmoDoodadTransform(wmoXf, d);
            inst.model = model;
            inst.worldBounds = worldBounds(model->bounds(), inst.xf);
            inst.adtId = p.nameSet;  // the doodad belongs to its host WMO's area
            tile.instances.push_back(std::move(inst));
        }
    }

    std::string MpqTileSource::mapName(uint32_t mapId) const
    {
        if (m_mapStore)
        {
            std::string dir = m_mapStore->getDirectory(mapId);
            if (!dir.empty())
                return dir;
        }
        // Built-in TBC continents/instances (the ones whose directory name differs
        // from a trivial guess).
        switch (mapId)
        {
        case 0:
            return "Azeroth"; // Eastern Kingdoms
        case 1:
            return "Kalimdor";
        case 530:
            return "Expansion01"; // Outland
        case 571:
            return "Northrend"; // (WotLK; harmless to keep)
        default:
            return std::string();
        }
    }

    std::string MpqTileSource::adtPath(uint32_t mapId, int tx, int ty) const
    {
        const std::string name = mapName(mapId);
        if (name.empty())
            return std::string();
        // ADT file order is _<ty>_<tx> (ty = tile index from world Y, tx from world
        // X): Azeroth_<x>_<y> with x=gy(=ty), y=gx(=tx).
        return "World\\Maps\\" + name + "\\" + name + "_" +
               std::to_string(ty) + "_" + std::to_string(tx) + ".adt";
    }
    std::string MpqTileSource::wdtPath(uint32_t mapId) const
    {
        const std::string name = mapName(mapId);
        if (name.empty())
            return std::string();
        return "World\\Maps\\" + name + "\\" + name + ".wdt";
    }
    
    std::shared_ptr<TerrainTile> MpqTileSource::tryloadWdt(uint32_t mapId)
    {
        // 1. If the global tile is already built, return it (once per map).
        if (auto git = m_globalWmoCache.find(mapId); git != m_globalWmoCache.end())
            return git->second;

        // 2. Look up WdtData in the cache (avoid re-parsing the WDT).
        auto wit = m_wdtCache.find(mapId);
        if (wit != m_wdtCache.end())
        {
            const WdtData& wdt = wit->second;
            if (!wdt.hasGlobalWmo || wdt.globalWmoName.empty())
                return nullptr;

            auto model = m_wmo.load(wdt.globalWmoName);
            if (!model || model->empty())
                return nullptr;

            auto tile = std::make_shared<TerrainTile>();
            tile->tx = 0;
            tile->ty = 0;
            tile->hasTerrain = false;
            tile->isGlobalWmo = true;

            const Transform xf = placementTransformGlobalWmo(*wdt.globalWmoPlacement);
            StaticInstance inst;
            inst.xf = xf;
            inst.model = model;
            inst.worldBounds = worldBounds(model->bounds(), inst.xf);
            inst.adtId = wdt.globalWmoPlacement->nameSet;
            tile->instances.push_back(std::move(inst));

            // A dungeon IS one big WMO, so its furniture is all doodads -- this is where
            // interior props matter most.
            attachWmoDoodads(*wdt.globalWmoPlacement, wdt.globalWmoName, xf, *tile);

            m_globalWmoCache[mapId] = tile;
            return tile;
        }

        // 3. First time: load the WDT from disk.
        const std::string path = wdtPath(mapId);
        if (path.empty())
            return nullptr;

        std::vector<uint8_t> bytes;
        if (!m_archive.read(path, bytes))
            return nullptr;

        WdtData wdt;
        if (!parseWdt(bytes, wdt))
            return nullptr;

        const bool hasGlobalWmo = wdt.hasGlobalWmo && !wdt.globalWmoName.empty();

        m_wdtCache.emplace(mapId, std::move(wdt));

        if (!hasGlobalWmo)
            return nullptr;

        // Build the tile once.
        const WdtData& cached = m_wdtCache.at(mapId);

        auto model = m_wmo.load(cached.globalWmoName);
        if (!model || model->empty())
            return nullptr;

        auto tile = std::make_shared<TerrainTile>();
        tile->tx = 0;
        tile->ty = 0;
        tile->hasTerrain = false;
        tile->isGlobalWmo = true;

        const Transform gxf = placementTransformGlobalWmo(*cached.globalWmoPlacement);
        StaticInstance inst;
        inst.xf = gxf;
        inst.model = model;
        inst.worldBounds = worldBounds(model->bounds(), inst.xf);
        inst.adtId = cached.globalWmoPlacement->nameSet;
        tile->instances.push_back(std::move(inst));

        attachWmoDoodads(*cached.globalWmoPlacement, cached.globalWmoName, gxf, *tile);

        m_globalWmoCache[mapId] = tile;
        return tile;
    }

    
    std::shared_ptr<TerrainTile> MpqTileSource::tryloadAdt(uint32_t mapId, int tx, int ty)
    {
        const std::string path = adtPath(mapId, tx, ty);
        if (path.empty())
            return nullptr;

        std::vector<uint8_t> bytes;
        if (!m_archive.read(path, bytes))
            return nullptr;

        AdtData adt;
        if (!parseAdt(bytes, adt) || !adt.hasTerrain)
            return nullptr;

        auto tile = std::make_shared<TerrainTile>();
        tile->tx = tx;
        tile->ty = ty;
        tile->hasTerrain = true;
        tile->v9 = std::move(adt.v9);
        tile->v8 = std::move(adt.v8);
        tile->holes = adt.holes;
        tile->areaIds = adt.areaIds;
        tile->hasLiquid = adt.hasLiquid;
        tile->liquidHeight = std::move(adt.liquidHeight);
        tile->liquidShow = std::move(adt.liquidShow);
        tile->liquidType = std::move(adt.liquidType);
        if (m_loadStatics)
        {
            auto attach = [&](const AdtPlacement &p,
                              const std::shared_ptr<const ICollisionModel> &model)
            {
                if (!model || model->empty())
                    return;
                StaticInstance inst;
                inst.xf = placementTransform(p);
                inst.model = model;
                inst.worldBounds = worldBounds(model->bounds(), inst.xf);
                inst.adtId = p.nameSet;  // WMOAreaTable adtId (0 for M2 placements)
                tile->instances.push_back(std::move(inst));
            };

            for (const auto &p : adt.wmoPlacements)
            {
                if (p.nameIndex >= adt.wmoNames.size())
                    continue;
                const std::string &wmoPath = adt.wmoNames[p.nameIndex];
                attach(p, m_wmo.load(wmoPath));
                // ...and the props inside it, which the ADT never names.
                attachWmoDoodads(p, wmoPath, placementTransform(p), *tile);
            }

            for (const auto &p : adt.m2Placements)
                if (p.nameIndex < adt.m2Names.size())
                    attach(p, m_m2.load(adt.m2Names[p.nameIndex]));
        }
        return tile;
    }

    std::shared_ptr<TerrainTile> MpqTileSource::load(uint32_t mapId, int tx, int ty)
    {
        
        if (auto adtTile = tryloadAdt(mapId, tx, ty))
            return adtTile;

      
        return tryloadWdt(mapId);
    }

} // namespace world::terrain
