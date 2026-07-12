#pragma once

#include "terrain/AdtParser.hpp" // for AdtPlacement

namespace world::terrain::internal
{

    // Little-endian scalar reads. ADTs are always little-endian; we read by byte
    // so the parser is endian-independent and never aliases unaligned pointers.
    inline uint32_t rdU32(const uint8_t *p)
    {
        return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) |
               (uint32_t(p[3]) << 24);
    }
    inline uint16_t rdU16(const uint8_t *p)
    {
        return uint16_t(p[0]) | (uint16_t(p[1]) << 8);
    }
    inline float rdF32(const uint8_t *p)
    {
        uint32_t u = rdU32(p);
        float f;
        std::memcpy(&f, &u, 4);
        return f;
    }

    // Chunk tags are stored reversed on disk ('MHDR' -> bytes 'R','D','H','M').
    inline bool tagIs(const uint8_t *p, const char *tag)
    {
        return p[0] == tag[3] && p[1] == tag[2] && p[2] == tag[1] && p[3] == tag[0];
    }

    inline AdtPlacement readModf(const uint8_t *p)
    { // 64-byte WMO placement
        AdtPlacement pl;
        pl.nameIndex = rdU32(p + 0);
        // p+4: uniqueId
        pl.pos = {rdF32(p + 8), rdF32(p + 12), rdF32(p + 16)};
        pl.rotDeg = {rdF32(p + 20), rdF32(p + 24), rdF32(p + 28)};
        pl.scale = 1.0f;                  // MODF has no scale
        pl.doodadSet = rdU16(p + 0x3A);   // which MODS set furnishes this placement
        pl.nameSet = rdU16(p + 0x3C);     // nameSet -> WMOAreaTable adtId
        return pl;
    }

    inline AdtPlacement readMddf(const uint8_t *p)
    { // 36-byte M2/doodad placement
        AdtPlacement pl;
        pl.nameIndex = rdU32(p + 0);
        // p+4: uniqueId
        pl.pos = {rdF32(p + 8), rdF32(p + 12), rdF32(p + 16)};
        pl.rotDeg = {rdF32(p + 20), rdF32(p + 24), rdF32(p + 28)};
        pl.scale = rdU16(p + 32) / 1024.0f;
        return pl;
    }
} // namespace world::terrain::internal
