#pragma once

// Real client-data archive, backed by StormLib. Compiled only when WITH_MPQ is
// enabled (otherwise the .cpp is an empty translation unit and this header is
// simply not included by anyone). Opens a set of .MPQ files in priority order;
// a file read searches the highest-priority archive that has it first, so the
// patch MPQs transparently override the base ones — exactly like the client.

#include "terrain/IMpqArchive.hpp"

#include <string>
#include <vector>

namespace world::terrain
{

    class StormLibArchive : public IMpqArchive
    {
    public:
        StormLibArchive() = default;
        ~StormLibArchive() override;

        StormLibArchive(const StormLibArchive &) = delete;
        StormLibArchive &operator=(const StormLibArchive &) = delete;

        // Open one MPQ and push it on the priority stack (last opened = highest
        // priority, searched first). Returns false if the archive can't be opened.
        bool addArchive(const std::string &mpqPath);

        // Convenience: open the standard TBC 2.4.3 archive chain found under a
        // client Data/ directory (common, expansion, patch, patch-2..4, plus the
        // given locale's locale-/patch- MPQs). Returns how many opened.
        int openClientData(const std::string &dataDir, const std::string &locale = "enGB");

        bool read(const std::string &path, std::vector<uint8_t> &out) override;
        bool contains(const std::string &path) const override;
        // Find files matching a StormLib wildcard pattern (e.g. "*.dbc", "*.adt",
        // "DBFilesClient\\*.dbc"). Searches highest-priority archive first; returns
        // unique file paths (deduplicated, highest-priority version wins).
        // The pattern supports * and ? wildcards; subdir paths in names (e.g.
        // "foo\\bar.dbc") are matched correctly by StormLib even with "*.dbc".
        std::vector<std::string> findFiles(const std::string &pattern) const;

        // Convenience wrapper: find all files with the given extension (e.g. ".dbc"
        // or "dbc" or ".adt"). Internally builds pattern "*.<ext>" (case as provided).
        std::vector<std::string> findFilesByExtension(const std::string &extension) const;
        size_t archiveCount() const { return m_handles.size(); }

    private:
        std::vector<void *> m_handles; // HANDLE, lowest priority first
    };

} // namespace world::terrain
