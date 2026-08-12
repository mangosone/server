#include <string>
#include "WmoParser.hpp"
#include "ChunkReaders.hpp"

#include <cstdio>
#include <cstring>

namespace world::terrain
{
    namespace
    {
        using namespace world::terrain::internal;

        // MOPY material flags. An earlier port of this filter had 0x04 as "no collision"
        // and 0x08 as "hint", and built the rule on that mistake.
        constexpr uint8_t MATERIAL_DETAIL = 0x04;
        constexpr uint8_t MATERIAL_COLLISION = 0x08;
        constexpr uint8_t MATERIAL_RENDER = 0x20;

        constexpr uint32_t MOHD_USE_LIQUID_DBC_ID = 0x4;
        constexpr uint32_t MOGP_LIQUID_IS_OCEAN = 0x80000;

        constexpr uint32_t LIQUID_NONE = 15;
        constexpr uint32_t LIQUID_FIRST_DBC_ID = 21;

        constexpr size_t MOGP_HEADER = 68;
        constexpr size_t MOGP_FLAGS = 0x08;
        constexpr size_t MOGP_GROUP_LIQUID = 0x34;
        constexpr size_t MOGP_UNIQUE_ID = 0x38;

        constexpr size_t MOHD_N_GROUPS = 0x04;
        constexpr size_t MOHD_WMO_ID = 0x20;
        constexpr size_t MOHD_FLAGS = 0x3C;

        // 2.4.3's canonical WMO liquid rows, and the warning the old comment carried is
        // the reason these changed rather than a reason to leave them: a WMO lava pool
        // written as the wrong LiquidType.dbc row does report as the wrong liquid.
        //
        // It was pointing the wrong way. 13/14/19/20 are 3.3.5a's rows, and this client's
        // LiquidType.dbc holds SEVEN: 1 Water, 2 Ocean, 3 Magma, 4 Slime, plus 21, 41 and
        // 61. Rows 13, 14, 19 and 20 are simply absent, so GridMap's LiquidFlagsOfRow
        // looked them up, missed, and fell through to sound bank 0 -- every WMO liquid in
        // the world, lava and slime included, reported as plain WATER, with no fire damage
        // and no swim rules of its own. Measured on a full Kalimdor bake: all 77 WMO
        // liquid groups carried row 13.
        //
        // The ADT half of a tile has always written 1..4 (AdtParser's MCLQ path), so this
        // also ends two numberings meeting inside one file. Both agree with the row ids
        // GridMap hard-codes (LIQUID_OCEAN_ROW = 2).
        constexpr uint32_t ROW_WATER = 1;
        constexpr uint32_t ROW_OCEAN = 2;
        constexpr uint32_t ROW_MAGMA = 3;
        constexpr uint32_t ROW_SLIME = 4;

        uint32_t CanonicalLiquidEntry(uint32_t entry, uint32_t mogpFlags)
        {
            if (!entry || entry >= LIQUID_FIRST_DBC_ID)
            {
                return entry;
            }

            switch ((entry - 1u) & 3u)
            {
                case 0:
                    return ((mogpFlags & MOGP_LIQUID_IS_OCEAN) != 0) ? ROW_OCEAN : ROW_WATER;
                case 1:
                    return ROW_OCEAN;
                case 2:
                    return ROW_MAGMA;
                case 3:
                    return ROW_SLIME;
                default:
                    return entry;
            }
        }

        void ReadDoodads(const uint8_t* d, size_t n, WmoRootData& out)
        {
            const uint8_t* mods = nullptr;
            uint32_t modsSize = 0;
            const uint8_t* modn = nullptr;
            uint32_t modnSize = 0;
            const uint8_t* modd = nullptr;
            uint32_t moddSize = 0;

            size_t pos = 0;
            while (pos + 8 <= n)
            {
                const uint32_t sz = RdU32(d + pos + 4);
                if (pos + 8 + sz > n)
                {
                    break;
                }
                if (TagIs(d + pos, "MODS"))
                {
                    mods = d + pos + 8;
                    modsSize = sz;
                }
                else if (TagIs(d + pos, "MODN"))
                {
                    modn = d + pos + 8;
                    modnSize = sz;
                }
                else if (TagIs(d + pos, "MODD"))
                {
                    modd = d + pos + 8;
                    moddSize = sz;
                }
                pos += 8 + sz;
            }

            if (!modd || !modn || moddSize < 40)
            {
                return;
            }

            if (mods)
            {
                const uint32_t nSets = modsSize / 32;
                out.sets.reserve(nSets);
                for (uint32_t i = 0; i < nSets; ++i)
                {
                    const uint8_t* p = mods + i * 32;
                    out.sets.push_back({RdU32(p + 20), RdU32(p + 24)});
                }
            }
            if (out.sets.empty())
            {
                out.sets.push_back({0, moddSize / 40});
            }

            const uint32_t nDefs = moddSize / 40;
            out.doodads.reserve(nDefs);
            for (uint32_t i = 0; i < nDefs; ++i)
            {
                const uint8_t* p = modd + i * 40;
                // MODD's name field is a BYTE OFFSET into MODN, not an index.
                const uint32_t nameOfs = RdU32(p + 0) & 0x00FFFFFFu;

                WmoDoodad dd;
                if (nameOfs < modnSize)
                {
                    const char* s = reinterpret_cast<const char*>(modn + nameOfs);
                    dd.name.assign(s, ::strnlen(s, modnSize - nameOfs));
                }
                dd.pos = {RdF32(p + 4), RdF32(p + 8), RdF32(p + 12)};
                dd.quat[0] = RdF32(p + 16);
                dd.quat[1] = RdF32(p + 20);
                dd.quat[2] = RdF32(p + 24);
                dd.quat[3] = RdF32(p + 28);
                dd.scale = RdF32(p + 32);
                if (!(dd.scale > 0.f))
                {
                    dd.scale = 1.f;
                }
                out.doodads.push_back(std::move(dd));
            }
        }
    }

    std::string WmoGroupPath(const std::string& root, uint32_t index)
    {
        std::string base = root;
        const size_t dot = base.find_last_of('.');
        if (dot != std::string::npos)
        {
            base.erase(dot);
        }
        char suffix[16];
        std::snprintf(suffix, sizeof(suffix), "_%03u.wmo", index);
        return base + suffix;
    }

    bool ParseWmoRoot(const uint8_t* d, size_t n, WmoRootData& out)
    {
        out = WmoRootData{};
        if (!d || n < 8)
        {
            return false;
        }

        bool sawHeader = false;
        bool truncated = false;
        size_t pos = 0;
        while (pos + 8 <= n)
        {
            const uint32_t sz = RdU32(d + pos + 4);
            if (static_cast<uint64_t>(pos) + 8 + sz > n)
            {
                truncated = true;
                break;
            }
            if (TagIs(d + pos, "MOHD") && sz >= 8)
            {
                const uint8_t* h = d + pos + 8;
                out.nGroups = RdU32(h + MOHD_N_GROUPS);
                if (sz >= MOHD_WMO_ID + 4)
                {
                    out.wmoId = RdU32(h + MOHD_WMO_ID);
                }
                if (sz >= MOHD_FLAGS + 4)
                {
                    out.flags = RdU32(h + MOHD_FLAGS);
                }
                sawHeader = true;
            }
            pos += 8 + sz;
        }

        ReadDoodads(d, n, out);

        // MOHD is the FIRST chunk, so sawHeader alone says only that the file began
        // correctly. A root cut anywhere after it -- in MODN or MODD, which is where the
        // bulk of a root's bytes are -- still returned true, and WmoLoader then baked the
        // groups against doodads that were partial or absent: the building stands, the
        // furniture inside it has no collision, and the extraction reports success.
        // `pos != n` is the same hole one byte earlier, for the same reason it is checked
        // in ParseWmoGroup: the overrun flag needs a COMPLETE header to fire.
        return sawHeader && !truncated && pos == n;
    }

    WmoGroupParse ParseWmoGroup(const uint8_t* d, size_t n, uint32_t rootFlags,
                                WmoGroupData& out)
    {
        out = WmoGroupData{};
        if (!d || n < 8)
        {
            return WmoGroupParse::Malformed;
        }

        const uint8_t* mopy = nullptr;
        uint32_t mopySize = 0;
        const uint8_t* movi = nullptr;
        uint32_t moviSize = 0;
        const uint8_t* movt = nullptr;
        uint32_t movtSize = 0;
        const uint8_t* mliq = nullptr;
        uint32_t mliqSize = 0;
        const uint8_t* mogp = nullptr;
        bool truncated = false;

        size_t pos = 0;
        while (pos + 8 <= n)
        {
            const uint8_t* tag = d + pos;
            const uint32_t sz = RdU32(d + pos + 4);
            // MOGP is a container: step into it by its header only, so the geometry
            // chunks nested inside get walked as if they were top-level.
            uint32_t advance = sz;
            bool isContainer = false;
            if (TagIs(tag, "MOGP"))
            {
                isContainer = true;
                // The container is exempted from the bounds check below, because stepping
                // in by the header is the whole point -- so its own declared size is
                // checked HERE or nowhere. Too short to hold the header, or declaring more
                // bytes than the file has, and the nested walk reads whatever follows as
                // if it were geometry.
                if (sz < MOGP_HEADER || static_cast<uint64_t>(pos) + 8 + sz > n)
                {
                    truncated = true;
                    break;
                }
                advance = MOGP_HEADER;
                mogp = d + pos + 8;
            }
            else if (TagIs(tag, "MOPY"))
            {
                mopy = d + pos + 8;
                mopySize = sz;
            }
            else if (TagIs(tag, "MOVI"))
            {
                movi = d + pos + 8;
                moviSize = sz;
            }
            else if (TagIs(tag, "MOVT"))
            {
                movt = d + pos + 8;
                movtSize = sz;
            }
            else if (TagIs(tag, "MLIQ"))
            {
                mliq = d + pos + 8;
                mliqSize = sz;
            }

            // On the container flag, NOT on `advance != MOGP_HEADER`: that compared a
            // value, so any ordinary chunk whose size happened to be exactly 68 bytes
            // exempted itself from the only bounds check in this loop.
            if (!isContainer && static_cast<uint64_t>(pos) + 8 + sz > n)
            {
                truncated = true;
                break;
            }
            pos += 8 + advance;
        }

        // A group file IS a MOGP container, so no MOGP -- or one whose header runs past
        // the end, or a chunk declaring more bytes than the file holds -- is a broken
        // file, not a group with nothing in it. Everything past here is a real group.
        //
        // `pos != n` is the case the overrun flag cannot see: stepping into MOGP by its
        // header and walking the nested chunks consumes exactly the container's bytes, so
        // a well-formed group ends ON the end. A file cut 1-7 bytes into a nested header
        // leaves the loop by its `pos + 8 <= n` condition without ever entering the body,
        // and used to answer Empty or Loaded with that geometry silently missing.
        if (truncated || pos != n || !mogp || mogp + MOGP_HEADER > d + n)
        {
            return WmoGroupParse::Malformed;
        }

        out.mogpFlags = RdU32(mogp + MOGP_FLAGS);
        const uint32_t groupLiquid = RdU32(mogp + MOGP_GROUP_LIQUID);
        out.groupWmoId = RdU32(mogp + MOGP_UNIQUE_ID);

        // MLIQ's trailing uint16 is a materialId, NOT the liquid type. Reading it as
        // the type is how WMO lava and slime end up classified as water.
        if (mliq && mliqSize >= 30)
        {
            const uint32_t xverts = RdU32(mliq + 0), yverts = RdU32(mliq + 4);
            const uint32_t xtiles = RdU32(mliq + 8), ytiles = RdU32(mliq + 12);
            const float px = RdF32(mliq + 16), py = RdF32(mliq + 20), pz = RdF32(mliq + 24);
            const uint64_t vbytes = uint64_t(xverts) * yverts * 8;
            const uint64_t fbytes = uint64_t(xtiles) * ytiles;

            // The corner grid must be exactly one wider than the tile grid on each axis.
            // Everything downstream -- the serializer's screen, WmoModel::GroupLiquidAt's
            // four-corner interpolation -- assumes that relation and indexes on it, so a
            // MLIQ that does not hold it is dropped here rather than baked into a tile
            // that reads off the end of its own height vector.
            if (xverts && yverts && xtiles && ytiles && xverts == xtiles + 1 &&
                yverts == ytiles + 1 && 30 + vbytes + fbytes <= mliqSize)
            {
                const uint8_t* verts = mliq + 30;
                const uint8_t* tileFlags = verts + vbytes;

                out.hasLiquid = true;
                out.liquid.tilesX = xtiles;
                out.liquid.tilesY = ytiles;
                out.liquid.corner = {px, py, pz};
                out.liquid.heights.resize(size_t(xverts) * yverts);
                for (size_t i = 0; i < out.liquid.heights.size(); ++i)
                {
                    out.liquid.heights[i] = RdF32(verts + i * 8 + 4);
                }
                out.liquid.flags.assign(tileFlags, tileFlags + fbytes);

                uint32_t entry;
                if (rootFlags & MOHD_USE_LIQUID_DBC_ID)
                {
                    entry = groupLiquid;
                }
                else if (groupLiquid == LIQUID_NONE)
                {
                    entry = 0;
                }
                else
                {
                    entry = groupLiquid + 1;
                }

                if (!entry)
                {
                    for (uint8_t tf : out.liquid.flags)
                    {
                        if ((tf & 0x0F) != LIQUID_NONE)
                        {
                            entry = uint32_t(tf & 0x0F) + 1u;
                            break;
                        }
                    }
                }

                out.liquid.entry =
                    static_cast<uint16_t>(CanonicalLiquidEntry(entry, out.mogpFlags));
            }
        }

        // No geometry chunks at all is the ordinary shape of a portal or ambient group.
        if (!movi || !movt)
        {
            return out.hasLiquid ? WmoGroupParse::Loaded : WmoGroupParse::Empty;
        }

        const uint32_t nVert = movtSize / 12;
        out.verts.reserve(nVert);
        for (uint32_t v = 0; v < nVert; ++v)
        {
            out.verts.push_back({RdF32(movt + v * 12 + 0), RdF32(movt + v * 12 + 4),
                                 RdF32(movt + v * 12 + 8)});
        }

        const uint32_t nTri = moviSize / 6;
        out.tris.reserve(nTri);
        for (uint32_t t = 0; t < nTri; ++t)
        {
            const uint8_t flags =
                (mopy && 2 * t < mopySize) ? static_cast<uint8_t>(mopy[2 * t]) : 0;

            // A face may be both DETAIL and COLLISION (0x0C) and still collide, so the
            // COLLISION bit must be tested on its own, not merely !DETAIL.
            const bool isRenderFace = (flags & MATERIAL_RENDER) && !(flags & MATERIAL_DETAIL);
            const bool collides = (flags & MATERIAL_COLLISION) || isRenderFace;
            if (mopy && mopySize != 0 && !collides)
            {
                continue;
            }

            const uint16_t a = RdU16(movi + (3 * t + 0) * 2);
            const uint16_t b = RdU16(movi + (3 * t + 1) * 2);
            const uint16_t c = RdU16(movi + (3 * t + 2) * 2);
            if (a >= nVert || b >= nVert || c >= nVert)
            {
                continue;
            }
            out.tris.push_back({a, b, c});
        }

        return (!out.tris.empty() || out.hasLiquid) ? WmoGroupParse::Loaded
                                                    : WmoGroupParse::Empty;
    }
}
