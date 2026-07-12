#include "terrain/WdtParser.hpp"
#include "terrain/AdtParser.hpp"    // for readModf
#include "terrain/ChunkReaders.hpp" // for rdU32, tagIs, etc.
#include <cstring>

namespace world::terrain
{

    using namespace world::terrain::internal;

    bool parseWdt(const uint8_t *data, size_t size, WdtData &out)
    {
        if (!data || size < 8)
            return false;

        out = WdtData{};

        const uint8_t *mwmo = nullptr;
        uint32_t mwmoSize = 0;
        const uint8_t *modf = nullptr;
        uint32_t modfSize = 0;

        size_t pos = 0;
        while (pos + 8 <= size)
        {
            const uint8_t *tag = data + pos;
            uint32_t csize = rdU32(data + pos + 4);
            const uint8_t *body = data + pos + 8;

            if (pos + 8 + csize > size)
                break;

            if (tagIs(tag, "MPHD"))
            {
                if (csize >= 4)
                {
                    out.mphdFlags = rdU32(body);
                    out.hasGlobalWmo = (out.mphdFlags & 0x0001) != 0;
                }
            }
            else if (tagIs(tag, "MAIN"))
            {
                constexpr size_t ENTRY_SIZE = 8;
                const size_t maxEntries = csize / ENTRY_SIZE;
                const size_t entriesToParse = std::min<size_t>(maxEntries, 64ULL * 64);
                for (size_t i = 0; i < entriesToParse; ++i)
                {
                    const uint8_t *entry = body + i * ENTRY_SIZE;
                    uint32_t flags = rdU32(entry);
                    const int y = static_cast<int>(i / 64);
                    const int x = static_cast<int>(i % 64);
                    out.hasAdt[y][x] = (flags & 0x1) != 0;
                }

                out.hasMainChunk = true;
            }
            else if (tagIs(tag, "MWMO"))
            {
                mwmo = body;
                mwmoSize = csize;
            }
            else if (tagIs(tag, "MODF"))
            {
                modf = body;
                modfSize = csize;
            }

            pos += 8 + csize;
        }

        // Extract the global WMO name
        if (out.hasGlobalWmo && mwmo && mwmoSize > 0)
        {
            const char *s = reinterpret_cast<const char *>(mwmo);
            uint32_t len = 0;
            while (len < mwmoSize && s[len] != '\0')
                ++len;
            out.globalWmoName.assign(s, len);
        }

        // Parse the global WMO placement (usually at most one)
        if (out.hasGlobalWmo && modf && modfSize >= 64)
        {
            AdtPlacement placement = readModf(modf); // reuse the AdtParser helper
            placement.scale = 1.0f;                  // MODF has no scale
            out.globalWmoPlacement = placement;
        }

        return true;
    }

} // namespace world::terrain
