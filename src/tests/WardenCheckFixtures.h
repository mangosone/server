/**
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * MaNGOS is a full featured server for World of Warcraft, supporting
 * the following clients: 1.12.x, 2.4.3, 3.3.5a, 4.3.4a and 5.4.8
 *
 * Copyright (C) 2005-2026 MaNGOS <https://www.getmangos.eu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef MANGOS_TEST_WARDEN_CHECK_FIXTURES_H
#define MANGOS_TEST_WARDEN_CHECK_FIXTURES_H

#include "WardenCheckCatalog.h"

#include <string>
#include <vector>

namespace warden
{
namespace test
{
inline WardenCheckRowInput MakeRow(uint32 build,
    std::string const& localeHex, uint32 checkId, WardenCheckType type,
    uint32 sortOrder, WardenEvidenceClass evidenceClass)
{
    WardenCheckRowInput row;
    row.build = build;
    row.platformHex = "57696E";
    row.localeHex = localeHex;
    row.checkId = checkId;
    row.type = static_cast<uint32>(type);
    row.enabled = 1;
    row.sortOrder = sortOrder;
    row.evidenceClass = static_cast<uint32>(evidenceClass);
    return row;
}

inline void AppendInitialProfile(std::vector<WardenCheckRowInput>& rows,
    uint32 build, std::string const& localeHex,
    std::string const& mpqExpectedHex, std::string const& luaExpectedHex)
{
    rows.push_back(MakeRow(build, localeHex, 65536,
        WardenCheckType::Timing, 10, WardenEvidenceClass::ProtocolHealth));

    WardenCheckRowInput mpq = MakeRow(build, localeHex, 1,
        WardenCheckType::Mpq, 20, WardenEvidenceClass::Corroboration);
    mpq.requestHex =
        "444246696C6573436C69656E745C417265615461626C652E646263";
    mpq.expectedHex = mpqExpectedHex;
    rows.push_back(mpq);

    WardenCheckRowInput lua = MakeRow(build, localeHex, 2,
        WardenCheckType::Lua, 30, WardenEvidenceClass::Corroboration);
    lua.requestHex = "4F4B4159";
    lua.expectedHex = luaExpectedHex;
    rows.push_back(lua);

    WardenCheckRowInput bootstrap = MakeRow(build, localeHex, 3,
        WardenCheckType::Mem, 40, WardenEvidenceClass::IntegrityInvariant);
    bootstrap.moduleHex = "576F572E657865";
    bootstrap.address = 0x002D0C10;
    bootstrap.length = 40;
    bootstrap.expectedHex =
        "B9EC18E100E88687F7FFE821FBFFFF68CCDDBB00B9B3120000BA18CBBB00E8BD"
        "FCFFFFA3D018E100";
    rows.push_back(bootstrap);
}

/** Exact database rows intended for the eight evidenced TBC locale profiles. */
inline std::vector<WardenCheckRowInput> InitialWardenRows()
{
    std::vector<WardenCheckRowInput> rows;
    rows.reserve(32);
    AppendInitialProfile(rows, 8606, "656E5553",
        "D65D59D2E57792A13E8EDF78A574B8F81D0D3CF0", "4F6B6179");
    AppendInitialProfile(rows, 8606, "656E4742",
        "D65D59D2E57792A13E8EDF78A574B8F81D0D3CF0", "4F6B6179");
    AppendInitialProfile(rows, 8606, "64654445",
        "002ECF12A5B297DACE7955E5971E4F68BF8A8DAB", "4F4B");
    AppendInitialProfile(rows, 8606, "65734553",
        "DBE1DBADE50F3A616B48F8909D3C8EF75C0D9220", "41636570746172");
    AppendInitialProfile(rows, 8606, "66724652",
        "C7C50539F79BD77E28A6328CB13FCFFA596F1F7B", "4F4B");
    AppendInitialProfile(rows, 8606, "6B6F4B52",
        "7AD1756C8A4BD449698A98FDC58775EE2F0FBD6E", "ED9995EC9DB8");
    AppendInitialProfile(rows, 8606, "72755255",
        "FF75C74BDCF685D3ED6A7E00DCAF5DA54E75F436", "D09ED09A");
    AppendInitialProfile(rows, 8606, "7A68434E",
        "3D71F5D1E2BB4147FD6E2587C0D7E0167180DAC0", "E7A1AEE5AE9A");
    return rows;
}

inline WardenCheckCatalog BuildInitialWardenCatalog()
{
    WardenCheckCatalogBuilder builder;
    WardenCheckDiagnostic diagnostic;
    for (WardenCheckRowInput const& row : InitialWardenRows())
    {
        if (builder.Add(row, diagnostic) != CheckCatalogValidation::Valid)
            return WardenCheckCatalog();
    }

    WardenCheckCatalog catalog;
    if (builder.Build(catalog, diagnostic) != CheckCatalogValidation::Valid)
        return WardenCheckCatalog();
    return catalog;
}
}
}

#endif
