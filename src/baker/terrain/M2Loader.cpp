#include "terrain/M2Loader.hpp"
#include "terrain/CollisionModel.hpp"

#include <array>
#include <cctype>
#include <cstring>

namespace world::terrain {

namespace {

    uint32_t rdU32(const uint8_t* p) {
        return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) |
               (uint32_t(p[3]) << 24);
    }
    uint16_t rdU16(const uint8_t* p) { return uint16_t(p[0]) | (uint16_t(p[1]) << 8); }
    float    rdF32(const uint8_t* p) {
        uint32_t u = rdU32(p);
        float f;
        std::memcpy(&f, &u, 4);
        return f;
    }

    // ModelHeader field byte offsets (pack(1); see vmap_extractor/modelheaders.h).
    // id[4]@0, version[4]@4, ... floats[14]@180, then the bounding-collision block:
    constexpr size_t kOfsNBoundTris  = 236;
    constexpr size_t kOfsOfsBoundTris = 240;
    constexpr size_t kOfsNBoundVerts = 244;
    constexpr size_t kOfsOfsBoundVerts = 248;
    constexpr size_t kHeaderNeeded   = 252;  // bytes we must be able to read

    // MMDX stores .mdx (or .mdl); the on-disk model is .m2.
    std::string toM2(std::string p) {
        auto ends = [&](const char* ext) {
            size_t n = std::strlen(ext);
            if (p.size() < n) return false;
            for (size_t i = 0; i < n; ++i)
                if (std::tolower(static_cast<unsigned char>(p[p.size() - n + i])) != ext[i])
                    return false;
            return true;
        };
        if (ends(".mdx") || ends(".mdl"))
            p.replace(p.size() - 4, 4, ".m2");
        return p;
    }

}  // namespace

std::shared_ptr<const ICollisionModel> M2Loader::load(const std::string& path) {
    const std::string key = toM2(path);
    auto it = m_cache.find(key);
    if (it != m_cache.end())
        return it->second;

    std::vector<uint8_t> bytes;
    if (!m_archive.read(key, bytes)) {
        m_cache.emplace(key, nullptr);
        return nullptr;
    }

    std::vector<Vec3> verts;
    std::vector<std::array<uint32_t, 3>> tris;

    if (bytes.size() >= kHeaderNeeded && bytes[0] == 'M' && bytes[1] == 'D' &&
        bytes[2] == '2' && bytes[3] == '0') {
        const uint8_t* d = bytes.data();
        const uint32_t nTriIdx  = rdU32(d + kOfsNBoundTris);    // index count (3 per face)
        const uint32_t ofsTri   = rdU32(d + kOfsOfsBoundTris);
        const uint32_t nVerts   = rdU32(d + kOfsNBoundVerts);
        const uint32_t ofsVerts = rdU32(d + kOfsOfsBoundVerts);

        const bool vertsOk = nVerts && ofsVerts + uint64_t(nVerts) * 12 <= bytes.size();
        const bool trisOk  = nTriIdx && ofsTri + uint64_t(nTriIdx) * 2 <= bytes.size();
        if (vertsOk && trisOk) {
            verts.reserve(nVerts);
            for (uint32_t v = 0; v < nVerts; ++v) {
                const uint8_t* p = d + ofsVerts + v * 12;
                // M2 model space -> runtime model space: negate Y (the net of the
                // extractor's fixCoordSystem + y/z swap), so the WMO-style placement
                // transform lands these vertices in the world.
                verts.push_back({rdF32(p + 0), -rdF32(p + 4), rdF32(p + 8)});
            }
            for (uint32_t i = 0; i + 2 < nTriIdx; i += 3) {
                const uint32_t a = rdU16(d + ofsTri + (i + 0) * 2);
                const uint32_t b = rdU16(d + ofsTri + (i + 1) * 2);
                const uint32_t c = rdU16(d + ofsTri + (i + 2) * 2);
                if (a < nVerts && b < nVerts && c < nVerts)
                    tris.push_back({a, b, c});
            }
        }
    }

    auto model = std::make_shared<CollisionModel>(std::move(verts), std::move(tris));
    m_cache.emplace(key, model);
    return model;
}

}  // namespace world::terrain
