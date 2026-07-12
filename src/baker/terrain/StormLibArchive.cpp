// Empty translation unit unless WITH_MPQ wired StormLib in. Keeping the file in
// the glob (rather than conditionally adding it) means the source list is stable
// across configs; the gate is purely the WITH_MPQ define + the link.
#ifdef WITH_MPQ

#include "terrain/StormLibArchive.hpp"

#include <StormLib.h>
#include <iostream>
#include <unordered_set>

namespace world::terrain
{

    StormLibArchive::~StormLibArchive()
    {
        for (void *h : m_handles)
            if (h)
                SFileCloseArchive(h);
    }

    bool StormLibArchive::addArchive(const std::string &mpqPath)
    {
        HANDLE h = nullptr;
        if (!SFileOpenArchive(mpqPath.c_str(), 0, MPQ_OPEN_READ_ONLY, &h))
        {
            // GetLastError() is the portable spelling here: on Windows it is the Win32
            // one, and on other platforms StormLib.h declares its own shim (see the
            // "Non-Windows support for SetLastError/GetLastError" block in StormLib.h).
            std::cerr << "StormLib: failed to open archive " << mpqPath
                      << " (err " << GetLastError() << ")\n";
            return false;
        }
        m_handles.push_back(h);
        std::cout << "StormLib: opened " << mpqPath << "\n";
        return true;
    }

    int StormLibArchive::openClientData(const std::string &dataDir, const std::string &locale)
    {
        int opened = 0;
        auto tryOpen = [&](const std::string &rel)
        {
            if (addArchive(dataDir + "/" + rel))
                ++opened;
        };

        // Base chain (lowest priority first). Locale archives go on top so localized
        // overrides win, matching the client's load order.
        tryOpen("common.MPQ");
        tryOpen("expansion.MPQ");
        tryOpen("patch.MPQ");
        tryOpen("patch-2.MPQ");

        const std::string ld = dataDir + "/" + locale;
        auto tryOpenLocale = [&](const std::string &rel)
        {
            if (addArchive(ld + "/" + rel))
                ++opened;
        };
        tryOpenLocale("locale-" + locale + ".MPQ");
        tryOpenLocale("expansion-locale-" + locale + ".MPQ");
        tryOpenLocale("patch-" + locale + ".MPQ");
        tryOpenLocale("patch-" + locale + "-2.MPQ");

        return opened;
    }

    bool StormLibArchive::read(const std::string &path, std::vector<uint8_t> &out)
    {
        // Highest priority (last opened) first.
        for (auto it = m_handles.rbegin(); it != m_handles.rend(); ++it)
        {
            HANDLE hFile = nullptr;
            if (!SFileOpenFileEx(*it, path.c_str(), 0, &hFile))
                continue;

            DWORD high = 0;
            DWORD size = SFileGetFileSize(hFile, &high);
            if (size == SFILE_INVALID_SIZE)
            {
                SFileCloseFile(hFile);
                continue;
            }

            out.resize(size);
            DWORD got = 0;
            if (size > 0)
                SFileReadFile(hFile, out.data(), size, &got, nullptr);
            SFileCloseFile(hFile);
            if (got == size)
                return true;
            out.clear();
        }
        return false;
    }

    bool StormLibArchive::contains(const std::string &path) const
    {
        for (auto it = m_handles.rbegin(); it != m_handles.rend(); ++it)
        {
            // SFileHasFile takes a non-const handle; the call doesn't mutate the
            // archive in any observable way, so the const_cast is benign.
            if (SFileHasFile(*it, const_cast<char *>(path.c_str())))
                return true;
        }
        return false;
    }
    std::vector<std::string> StormLibArchive::findFiles(const std::string &pattern) const
    {
        std::vector<std::string> result;
        if (m_handles.empty() || pattern.empty())
            return result;

        std::unordered_set<std::string> seen;

        // Highest priority first (rbegin), so first match for a name is the effective one.
        for (auto it = m_handles.rbegin(); it != m_handles.rend(); ++it)
        {
            SFILE_FIND_DATA findData{};
            HANDLE hFind = SFileFindFirstFile(*it, pattern.c_str(), &findData, nullptr);
            if (hFind == nullptr)
                continue; // no matches or error in this archive

            do
            {
                std::string fname(findData.cFileName);
                if (seen.insert(fname).second) // inserted = not seen before
                {
                    result.push_back(std::move(fname));
                }
            } while (SFileFindNextFile(hFind, &findData));

            SFileFindClose(hFind);
        }
        return result;
    }

    std::vector<std::string> StormLibArchive::findFilesByExtension(const std::string &extension) const
    {
        if (extension.empty())
            return {};

        std::string ext = extension;
        if (ext[0] != '.')
            ext = "." + ext;

        std::string pattern = "*" + ext;
        return findFiles(pattern);
    }

} // namespace world::terrain

#endif // WITH_MPQ
