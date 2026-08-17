/**
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (C) 2005-2026 MaNGOS <https://www.getmangos.eu>
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
