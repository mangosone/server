#pragma once

// The one thing the terrain loaders need from the client data: "give me the raw
// bytes of this file by its in-MPQ path". Everything above this line (AdtParser,
// WmoParser, MpqTileSource) is pure byte-pushing and so is unit-testable against
// a MemoryArchive, with no StormLib and no real client data. The real client
// data path is StormLibArchive, compiled only when WITH_MPQ is enabled.
//
// Paths use the WoW convention: backslash-separated, case-insensitive, e.g.
//   World\Maps\Azeroth\Azeroth_32_48.adt

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace world::terrain {

class IMpqArchive {
public:
    virtual ~IMpqArchive() = default;

    // Read the whole file into `out`. Returns false (and leaves out untouched-ish)
    // if the file does not exist in any backing archive.
    virtual bool read(const std::string& path, std::vector<uint8_t>& out) = 0;

    virtual bool contains(const std::string& path) const = 0;
};

// In-memory archive for tests: a path -> bytes map. Lookups are case-insensitive
// and slash-insensitive ('/' and '\\' are treated the same), matching how a real
// MPQ resolves names, so tests can register "world\\maps\\x.adt" and the loader
// can ask for it with any casing/separator.
class MemoryArchive : public IMpqArchive {
public:
    void put(const std::string& path, std::vector<uint8_t> bytes) {
        m_files[normalize(path)] = std::move(bytes);
    }

    bool read(const std::string& path, std::vector<uint8_t>& out) override {
        auto it = m_files.find(normalize(path));
        if (it == m_files.end())
            return false;
        out = it->second;
        return true;
    }

    bool contains(const std::string& path) const override {
        return m_files.count(normalize(path)) != 0;
    }

private:
    static std::string normalize(const std::string& p) {
        std::string s;
        s.reserve(p.size());
        for (char c : p) {
            if (c == '/') c = '\\';
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            s.push_back(c);
        }
        return s;
    }

    std::unordered_map<std::string, std::vector<uint8_t>> m_files;
};

}  // namespace world::terrain
