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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include "TestHarness.h"

#include "WardenCheckCatalogLoader.h"
#include "WardenCheckFixtures.h"
#include "WardenModuleCatalog.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace
{
warden::WardenCheckCatalog BuildCheckCatalog(
    std::vector<warden::WardenCheckRowInput> const& rows)
{
    warden::WardenCheckCatalogBuilder builder;
    warden::WardenCheckDiagnostic diagnostic;
    for (warden::WardenCheckRowInput const& row : rows)
    {
        if (builder.Add(row, diagnostic) !=
            warden::CheckCatalogValidation::Valid)
            return {};
    }

    warden::WardenCheckCatalog catalog;
    if (builder.Build(catalog, diagnostic) !=
        warden::CheckCatalogValidation::Valid)
        return {};
    return catalog;
}
}

TEST(WardenCatalog_selects_only_the_exact_8606_windows_build)
{
    warden::WardenModuleCatalog catalog;

    CHECK(catalog.Find(8606, "Win") != nullptr);
    CHECK(catalog.Find(8606, "OSX") == nullptr);
    CHECK(catalog.Find(9999, "Win") == nullptr);
}

TEST(WardenCatalog_enumerates_one_validated_profile)
{
    warden::WardenModuleCatalog catalog;
    std::vector<warden::ModuleProfile const*> const profiles =
        catalog.Profiles();

    REQUIRE(profiles.size() == 1u);
    CHECK_EQ(profiles[0]->build, uint32(8606));
    CHECK_STR(profiles[0]->platform, "Win");
    CHECK(catalog.Validate(*profiles[0]) == warden::ModuleValidation::Valid);
}

TEST(WardenCatalog_coverage_requires_every_supported_locale_both_directions)
{
    warden::WardenModuleCatalog modules;
    std::vector<warden::WardenCheckRowInput> rows =
        warden::test::InitialWardenRows();
    warden::WardenCheckCatalog full = BuildCheckCatalog(rows);
    REQUIRE(full.TotalRows() == 32u);
    CHECK(warden::ValidateWardenCatalogCoverage(full, modules) ==
        warden::WardenCheckCatalogLoadFailure::None);

    rows.erase(std::remove_if(rows.begin(), rows.end(),
        [](warden::WardenCheckRowInput const& row)
        {
            return row.localeHex == "7A68434E";
        }), rows.end());
    warden::WardenCheckCatalog missingLocale = BuildCheckCatalog(rows);
    REQUIRE(missingLocale.TotalRows() == 28u);
    CHECK(warden::ValidateWardenCatalogCoverage(missingLocale, modules) ==
        warden::WardenCheckCatalogLoadFailure::ModuleWithoutProfile);

    rows = warden::test::InitialWardenRows();
    for (warden::WardenCheckRowInput& row : rows)
    {
        if (row.localeHex == "7A68434E")
            row.localeHex = "65734D58";
    }
    warden::WardenCheckCatalog unexpectedLocale = BuildCheckCatalog(rows);
    REQUIRE(unexpectedLocale.TotalRows() == 32u);
    CHECK(warden::ValidateWardenCatalogCoverage(unexpectedLocale, modules) ==
        warden::WardenCheckCatalogLoadFailure::ProfileWithoutModule);
}

TEST(WardenCatalog_exact_tbc_module_identity_and_keys_are_custody_pinned)
{
    warden::WardenModuleCatalog catalog;
    warden::ModuleProfile const* profile = catalog.Find(8606, "Win");

    REQUIRE(profile != nullptr);
    CHECK_EQ(profile->module.size, 18756u);
    CHECK_HEX(profile->moduleId.data(), profile->moduleId.size(),
        "79c0768d657977d697e10bad956cced1");
    CHECK_HEX(profile->moduleSha256.data(), profile->moduleSha256.size(),
        "6c68006a2f1fd31e7208204b3f7ceb94a6ce977876e13f2f703e9cd644482289");
    CHECK_HEX(profile->moduleKey.data(), profile->moduleKey.size(),
        "ae25bc51063b77bd363c3efe0fc173f9");
    CHECK_HEX(profile->hashSeed.data(), profile->hashSeed.size(),
        "4d808d2c77d905c41a6380ec08586afe");
    CHECK_HEX(profile->clientKeySeedHash.data(),
        profile->clientKeySeedHash.size(),
        "568c054c781a972a6037a2290c22b52571a06f4e");
    CHECK_HEX(profile->clientKeySeed.data(), profile->clientKeySeed.size(),
        "7f96eefda5b63d20a4df8e00cbf48304");
    CHECK_HEX(profile->serverKeySeed.data(), profile->serverKeySeed.size(),
        "c2b7adedfccca9c2bfb3f85602ba809b");
}

TEST(WardenCatalog_exact_8606_initialization_callbacks_are_custody_pinned)
{
    warden::WardenModuleCatalog catalog;
    warden::ModuleProfile const* profile = catalog.Find(8606, "Win");

    REQUIRE(profile != nullptr);
    CHECK_HEX(profile->initialization.archive.selectors.data(),
        profile->initialization.archive.selectors.size(), "01000200");
    CHECK_EQ(profile->initialization.archive.openRva, uint32(0x00257970));
    CHECK_EQ(profile->initialization.archive.sizeRva, uint32(0x00254080));
    CHECK_EQ(profile->initialization.archive.readRva, uint32(0x00254E00));
    CHECK_EQ(profile->initialization.archive.closeRva, uint32(0x00255290));
    CHECK_HEX(profile->initialization.lua.prefix.data(),
        profile->initialization.lua.prefix.size(), "040000");
    CHECK_EQ(profile->initialization.lua.callbackRva, uint32(0x00307200));
    CHECK_EQ(profile->initialization.lua.selector, uint8(1));
    CHECK_HEX(profile->initialization.timing.prefix.data(),
        profile->initialization.timing.prefix.size(), "010100");
    CHECK_EQ(profile->initialization.timing.callbackRva, uint32(0x00349850));
    CHECK_EQ(profile->initialization.timing.install, uint8(1));

    warden::ModuleProfile invalid = *profile;
    invalid.initialization.archive.closeRva = 0;
    CHECK(catalog.Validate(invalid) ==
        warden::ModuleValidation::InvalidInitialization);
}

TEST(WardenCatalog_rejects_a_corrupted_module_copy)
{
    warden::WardenModuleCatalog catalog;
    warden::ModuleProfile const* profile = catalog.Find(8606, "Win");

    REQUIRE(profile != nullptr);
    warden::ModuleProfile corrupted = *profile;
    std::vector<uint8> bytes(corrupted.module.data,
        corrupted.module.data + corrupted.module.size);
    bytes[801] ^= 0x80;
    corrupted.module = {bytes.data(), bytes.size()};

    CHECK(catalog.Validate(corrupted) ==
        warden::ModuleValidation::DigestMismatch);
}

TEST(WardenProtocol_admission_move_transfers_then_cleanses_the_source)
{
    warden::AdmissionData source;
    source.build = 8606;
    source.platform = "Win";
    source.clientLocale = "enGB";
    source.sessionKey.fill(0xA5);
    source.available = true;

    warden::AdmissionData moved(std::move(source));

    CHECK_EQ(moved.build, 8606u);
    CHECK_STR(moved.platform, "Win");
    CHECK_STR(moved.clientLocale, "enGB");
    CHECK(std::all_of(moved.sessionKey.begin(), moved.sessionKey.end(),
        [](uint8 value) { return value == 0xA5; }));
    CHECK(moved.available);
    CHECK_EQ(source.build, 0u);
    CHECK(source.platform.empty());
    CHECK(source.clientLocale.empty());
    CHECK(std::all_of(source.sessionKey.begin(), source.sessionKey.end(),
        [](uint8 value) { return value == 0; }));
    CHECK(!source.available);
}

TEST(WardenProtocol_clear_removes_pending_credentials)
{
    warden::AdmissionData admission;
    admission.build = 8606;
    admission.platform = "Win";
    admission.clientLocale = "enGB";
    admission.sessionKey.fill(0x5A);
    admission.available = true;

    admission.Clear();

    CHECK_EQ(admission.build, 0u);
    CHECK(admission.platform.empty());
    CHECK(admission.clientLocale.empty());
    CHECK(std::all_of(admission.sessionKey.begin(), admission.sessionKey.end(),
        [](uint8 value) { return value == 0; }));
    CHECK(!admission.available);
}

TEST(WardenProtocol_failure_names_are_secret_free_fixed_labels)
{
    CHECK_STR(warden::ToString(warden::WardenState::ModuleReady),
        "ModuleReady");
    CHECK_STR(warden::ToString(warden::WardenFailure::HashMismatch),
        "HashMismatch");
}
