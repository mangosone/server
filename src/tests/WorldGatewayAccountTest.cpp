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
 *
 * World of Warcraft, and all World of Warcraft or Warcraft art, images,
 * and lore are copyrighted by Blizzard Entertainment, Inc.
 */

#include "TestHarness.h"
#include "Database/Field.h"
#include "WorldGatewayAccount.h"

namespace
{
void SetClearRow(Field (&fields)[12])
{
    for (Field& field : fields)
        field.SetValue("0");

    fields[3].SetValue("192.0.2.10");
}
}

TEST(WorldGatewayAccount_account_ban_is_post_strip_field_ten)
{
    Field fields[12];
    SetClearRow(fields);
    fields[10].SetValue("1");
    CHECK_EQ(int(EvaluateAccountRestriction(fields, "192.0.2.10")),
             int(AccountRestriction::Banned));
}

TEST(WorldGatewayAccount_ip_ban_is_post_strip_field_eleven)
{
    Field fields[12];
    SetClearRow(fields);
    fields[11].SetValue("1");
    CHECK_EQ(int(EvaluateAccountRestriction(fields, "192.0.2.10")),
             int(AccountRestriction::Banned));
}

TEST(WorldGatewayAccount_locked_account_rejects_a_different_address)
{
    Field fields[12];
    SetClearRow(fields);
    fields[4].SetValue("1");
    CHECK_EQ(int(EvaluateAccountRestriction(fields, "198.51.100.20")),
             int(AccountRestriction::LockedAddressMismatch));
    CHECK_EQ(int(EvaluateAccountRestriction(fields, "192.0.2.10")),
             int(AccountRestriction::None));
}
