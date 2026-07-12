#include "terrain/Terrain.hpp"
#include "terrain/ChunkReaders.hpp"
#include <cstring>

namespace world::terrain
{

    namespace
    {

        using namespace world::terrain::internal;

        // Split a MWMO/MMDX string block (consecutive null-terminated names) keyed by
        // byte offset, then order them by the MWID/MMID offset table so the placement
        // nameIndex (an index into that table) maps straight to a vector slot.
        std::vector<std::string> resolveNames(const uint8_t *block, uint32_t blockSize,
                                              const uint8_t *offs, uint32_t offsSize)
        {
            std::vector<std::string> names;
            const uint32_t n = offsSize / 4;
            names.reserve(n);
            for (uint32_t i = 0; i < n; ++i)
            {
                uint32_t o = rdU32(offs + i * 4);
                if (o >= blockSize)
                {
                    names.emplace_back();
                    continue;
                }
                const char *s = reinterpret_cast<const char *>(block + o);
                uint32_t maxLen = blockSize - o;
                uint32_t len = 0;
                while (len < maxLen && s[len] != '\0')
                    ++len;
                names.emplace_back(s, len);
            }
            return names;
        }

        // Fill v9/v8/holes (and MCLQ liquid) from one MCNK. mcnk points at the chunk
        // tag ('MCNK'). The MCNK total byte span is 8 + mcnkSize.
        void readMcnk(const uint8_t *mcnk, uint32_t mcnkSize, AdtData &out)
        {
            const uint8_t *h = mcnk + 8;             // header (fields are relative to here)
            const uint32_t flags = rdU32(h + 0);     // MCNK flags (liquid-type bits)
            const uint32_t ix = rdU32(h + 4);        // 0..15 (world-Y dim)
            const uint32_t iy = rdU32(h + 8);        // 0..15 (world-X dim)
            const uint32_t offsMCVT = rdU32(h + 20); // relative to the MCNK tag
            const uint32_t holes = rdU32(h + 60);
            const uint32_t areaId = rdU32(h + 52);   // MCNK header areaid (AreaTable.dbc id)
            const uint32_t offsMCLQ = rdU32(h + 96); // data-relative (zpos is at 104)
            const uint32_t sizeMCLQ = rdU32(h + 100);
            const float baseZ = rdF32(h + 112); // ypos: MCNK base height
            if (ix > 15 || iy > 15)
                return;
            const uint32_t span = mcnkSize + 8; // bytes from the tag

            out.holes[iy * 16 + ix] = static_cast<uint16_t>(holes & 0xFFFF);
            out.areaIds[iy * 16 + ix] = static_cast<uint16_t>(areaId & 0xFFFF); // TBC area ids < 2^16

            if (offsMCVT && offsMCVT + 8 + 145 * 4 <= span)
            {
                const uint8_t *hm = mcnk + offsMCVT + 8; // 145 floats, skip MCVT tag+size
                // V9 corners (9x9) then V8 centers (8x8), per OregonCore's extractor:
                //   V9[i*8+y][j*8+x] += hm[y*17 + x]      (y,x in 0..8)
                //   V8[i*8+y][j*8+x] += hm[y*17 + 9 + x]  (y,x in 0..7)
                // with i=iy (first/stride dim, 129/128 wide) and j=ix.
                for (int y = 0; y <= 8; ++y)
                {
                    int cy = static_cast<int>(iy) * 8 + y;
                    for (int x = 0; x <= 8; ++x)
                    {
                        int cx = static_cast<int>(ix) * 8 + x;
                        out.v9[cy * V9_SIDE + cx] = baseZ + rdF32(hm + (y * 17 + x) * 4);
                    }
                }
                for (int y = 0; y < 8; ++y)
                {
                    int cy = static_cast<int>(iy) * 8 + y;
                    for (int x = 0; x < 8; ++x)
                    {
                        int cx = static_cast<int>(ix) * 8 + x;
                        out.v8[cy * GRID_PER_TILE + cx] = baseZ + rdF32(hm + (y * 17 + 9 + x) * 4);
                    }
                }
            }

            // MCLQ (old per-MCNK liquid; TBC's format — MH2O is WotLK+). Layout after
            // its tag+size: height1,height2 (8B), liquid[9][9] of {u32 light; f32 z}
            // (648B), flags[8][8] (64B). A cell's flags == 0x0F means "no liquid here".
            if (offsMCLQ && sizeMCLQ > 8 && offsMCLQ + 8 + 728 <= span)
            {
                const uint8_t *lq = mcnk + offsMCLQ + 8;  // skip MCLQ tag+size
                const uint8_t *heights = lq + 8;          // after height1,height2
                const uint8_t *lqFlags = lq + 8 + 81 * 8; // flags[8][8] after liquid[9][9]
                if (out.liquidHeight.empty())
                {
                    out.liquidHeight.assign(V9_SIDE * V9_SIDE, 0.0f);
                    out.liquidShow.assign(GRID_PER_TILE * GRID_PER_TILE, 0);
                    out.liquidType.assign(GRID_PER_TILE * GRID_PER_TILE, 0);
                }
                out.hasLiquid = true;
                // Kind from the MCNK header flags (one kind per chunk):
                //   0x04 lq_river   0x08 lq_ocean   0x10 lq_magma   0x20 lq_slime
                // These are the pre-WotLK liquid bits; LiquidType.dbc is NOT consulted,
                // because in the MCLQ era the chunk flags *are* the type. (The dbc only
                // enters through MH2O, which is WotLK+ and never appears in 2.4.3 ADTs.)
                //
                // Precedence matters only for the malformed case of two bits at once;
                // most-specific first, which matches the order the reference extractor's
                // successive overwrites settle on.
                //
                // Do not drop the slime case: without it a slime chunk matches no branch
                // and silently reports as water, so Undercity/Gnomeregan sludge would be
                // swimmable, deal no damage, and bake as NAV_WATER.
                uint8_t kind = static_cast<uint8_t>(LiquidKind::Water);
                if (flags & (1u << 5))
                    kind = static_cast<uint8_t>(LiquidKind::Slime);
                else if (flags & (1u << 4))
                    kind = static_cast<uint8_t>(LiquidKind::Magma);
                else if (flags & (1u << 3))
                    kind = static_cast<uint8_t>(LiquidKind::Ocean);
                // else: lq_river (0x04), or no bit at all -> Water, the default above.
                for (int y = 0; y <= 8; ++y)
                {
                    int cy = static_cast<int>(iy) * 8 + y;
                    for (int x = 0; x <= 8; ++x)
                    {
                        int cx = static_cast<int>(ix) * 8 + x;
                        out.liquidHeight[cy * V9_SIDE + cx] = rdF32(heights + (y * 9 + x) * 8 + 4);
                    }
                }
                for (int y = 0; y < 8; ++y)
                {
                    int cy = static_cast<int>(iy) * 8 + y;
                    for (int x = 0; x < 8; ++x)
                    {
                        int cx = static_cast<int>(ix) * 8 + x;
                        bool show = lqFlags[y * 8 + x] != 0x0F;
                        out.liquidShow[cy * GRID_PER_TILE + cx] = show ? 1 : 0;
                        if (show)
                            out.liquidType[cy * GRID_PER_TILE + cx] = kind;
                    }
                }
            }
        }

    } // namespace

    bool parseAdt(const uint8_t *data, size_t size, AdtData &out)
    {
        if (!data || size < 8)
            return false;

        // First pass holds the raw name-block/offset chunks so we can resolve them
        // once after the walk (order in the file is MWMO before MWID, etc.).
        const uint8_t *mwmo = nullptr;
        uint32_t mwmoSize = 0;
        const uint8_t *mwid = nullptr;
        uint32_t mwidSize = 0;
        const uint8_t *mmdx = nullptr;
        uint32_t mmdxSize = 0;
        const uint8_t *mmid = nullptr;
        uint32_t mmidSize = 0;

        bool sawMcnk = false;

        size_t pos = 0;
        while (pos + 8 <= size)
        {
            const uint8_t *tag = data + pos;
            uint32_t csize = rdU32(data + pos + 4);
            const uint8_t *body = data + pos + 8;
            if (pos + 8 + csize > size)
                break; // truncated chunk — stop gracefully

            if (tagIs(tag, "MCNK"))
            {
                if (!sawMcnk)
                {
                    out.v9.assign(V9_SIDE * V9_SIDE, 0.0f);
                    out.v8.assign(GRID_PER_TILE * GRID_PER_TILE, 0.0f);
                    out.holes.fill(0);
                    sawMcnk = true;
                }
                readMcnk(tag, csize, out);
            }
            else if (tagIs(tag, "MWMO"))
            {
                mwmo = body;
                mwmoSize = csize;
            }
            else if (tagIs(tag, "MWID"))
            {
                mwid = body;
                mwidSize = csize;
            }
            else if (tagIs(tag, "MMDX"))
            {
                mmdx = body;
                mmdxSize = csize;
            }
            else if (tagIs(tag, "MMID"))
            {
                mmid = body;
                mmidSize = csize;
            }
            else if (tagIs(tag, "MODF"))
            {
                for (uint32_t o = 0; o + 64 <= csize; o += 64)
                    out.wmoPlacements.push_back(readModf(body + o));
            }
            else if (tagIs(tag, "MDDF"))
            {
                for (uint32_t o = 0; o + 36 <= csize; o += 36)
                    out.m2Placements.push_back(readMddf(body + o));
            }

            pos += 8 + csize;
        }

        if (mwmo && mwid)
            out.wmoNames = resolveNames(mwmo, mwmoSize, mwid, mwidSize);
        if (mmdx && mmid)
            out.m2Names = resolveNames(mmdx, mmdxSize, mmid, mmidSize);

        out.hasTerrain = sawMcnk;
        return true;
    }

} // namespace world::terrain
