#include <string>
#include <vector>
#include "StormLibArchive.hpp"

#include <StormLib.h>

#include <cstdio>
#include <unordered_set>

namespace world::terrain
{
    namespace
    {
        std::string ExpandLocale(std::string s, const std::string& locale)
        {
            const std::string token = "{locale}";
            for (size_t at = s.find(token); at != std::string::npos;
                 at = s.find(token, at + locale.size()))
            {
                s.replace(at, token.size(), locale);
            }
            return s;
        }
    }

    // Lowest priority first: Read walks the handles in reverse, so the last archive
    // opened is the first searched.
    //
    // Checked against a real 2.4.3 install. 2.4.3 has no common-2.MPQ and no
    // lichking.MPQ -- those arrive with 3.3.5a -- and its patch chain stops at
    // patch-2.MPQ.
    //
    // These archives hold COMPLETE files, so opening them as independent handles and
    // taking the highest-priority hit is equivalent to StormLib's own patch chain. That
    // stops being true from 4.3.4 on, where wow-update-*.MPQ carry PTCH-headed deltas
    // and must go through SFileOpenPatchArchive instead.
    const std::vector<std::string>& ClientArchives243()
    {
        static const std::vector<std::string> archives = {
            "common.MPQ",   "expansion.MPQ",
            "patch.MPQ",    "patch-2.MPQ",
        };
        return archives;
    }

    const std::vector<std::string>& ClientLocaleArchives243()
    {
        // Lowest priority first, so the NUMBERED locale patch comes last and wins. Get
        // that backwards and Map.dbc is read from patch-{locale}.MPQ instead of
        // patch-{locale}-2.MPQ -- an older file that parses perfectly and is simply
        // missing the maps added since.
        //
        // 2.4.3 spells them "patch-<locale>-2.MPQ", not "patch-2-<locale>.MPQ", and has
        // no lichking-locale archive. The speech and backup archives are deliberately
        // absent: neither holds a DBC, WDT, ADT, WMO or M2.
        static const std::vector<std::string> archives = {
            "base-{locale}.MPQ",
            "locale-{locale}.MPQ",
            "expansion-locale-{locale}.MPQ",
            "patch-{locale}.MPQ",
            "patch-{locale}-2.MPQ",
        };
        return archives;
    }

    StormLibArchive::~StormLibArchive()
    {
        for (void* h : m_handles)
        {
            if (h)
            {
                SFileCloseArchive(h);
            }
        }
    }

    bool StormLibArchive::AddArchive(const std::string& mpqPath)
    {
        HANDLE h = nullptr;
        if (!SFileOpenArchive(mpqPath.c_str(), 0, MPQ_OPEN_READ_ONLY, &h))
        {
            return false;
        }
        m_handles.push_back(h);
        return true;
    }

    int StormLibArchive::OpenClientData(const std::string& dataDir,
                                        const std::vector<std::string>& archives,
                                        const std::vector<std::string>& localeArchives,
                                        const std::string& locale)
    {
        int opened = 0;
        for (const std::string& rel : archives)
        {
            if (AddArchive(dataDir + "/" + ExpandLocale(rel, locale)))
            {
                ++opened;
            }
        }

        const std::string localeDir = dataDir + "/" + locale;
        for (const std::string& rel : localeArchives)
        {
            if (AddArchive(localeDir + "/" + ExpandLocale(rel, locale)))
            {
                ++opened;
            }
        }
        return opened;
    }

    bool StormLibArchive::Read(const std::string& path, std::vector<uint8_t>& out)
    {
        for (auto it = m_handles.rbegin(); it != m_handles.rend(); ++it)
        {
            HANDLE hFile = nullptr;
            if (!SFileOpenFileEx(*it, path.c_str(), 0, &hFile))
            {
                continue;
            }

            DWORD high = 0;
            const DWORD size = SFileGetFileSize(hFile, &high);
            if (size == SFILE_INVALID_SIZE)
            {
                SFileCloseFile(hFile);
                continue;
            }

            out.resize(size);
            DWORD got = 0;
            if (size > 0)
            {
                SFileReadFile(hFile, out.data(), size, &got, nullptr);
            }
            SFileCloseFile(hFile);
            if (got == size)
            {
                return true;
            }
            out.clear();
        }
        return false;
    }

    bool StormLibArchive::Contains(const std::string& path) const
    {
        for (auto it = m_handles.rbegin(); it != m_handles.rend(); ++it)
        {
            if (SFileHasFile(*it, const_cast<char*>(path.c_str())))
            {
                return true;
            }
        }
        return false;
    }

    std::vector<std::string> StormLibArchive::FindFiles(const std::string& pattern) const
    {
        std::vector<std::string> result;
        if (m_handles.empty() || pattern.empty())
        {
            return result;
        }

        std::unordered_set<std::string> seen;
        for (auto it = m_handles.rbegin(); it != m_handles.rend(); ++it)
        {
            SFILE_FIND_DATA findData{};
            HANDLE hFind = SFileFindFirstFile(*it, pattern.c_str(), &findData, nullptr);
            if (!hFind)
            {
                continue;
            }
            do
            {
                std::string name(findData.cFileName);
                if (seen.insert(name).second)
                {
                    result.push_back(std::move(name));
                }
            } while (SFileFindNextFile(hFind, &findData));
            SFileFindClose(hFind);
        }
        return result;
    }
}
