/**
 * MaNGOS is a full featured server for World of Warcraft, supporting
 * the following clients: 1.12.x, 2.4.3, 3.3.5a, 4.3.4a and 5.4.8
 *
 * Copyright (C) 2005-2020 MaNGOS <https://getmangos.eu>
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * World of Warcraft, and all World of Warcraft or Warcraft art, images,
 * and lore are copyrighted by Blizzard Entertainment, Inc.
 */

#include "WorldSession.h"
#include "Log.h"
#include "Player.h"
#include "WorldPacket.h"
#include "ObjectMgr.h"
#include "World.h"

static void AttemptJoin(Player* _player)
{
    // skip not can autojoin cases and player group case
    if (!_player->m_lookingForGroup.canAutoJoin() || _player->GetGroup())
    {
        return;
    }

    // TODO: Guard Player Map
    sObjectAccessor.DoForAllPlayers([&](Player* pl)
    {
        // skip enemies and self
        if (!pl || pl == _player || pl->GetTeam() != _player->GetTeam())
        {
            return;
        }

        // skip players not in world
        if (!pl->IsInWorld())
        {
            return;
        }

        // skip not auto add, not group leader cases
        if (!pl->GetSession()->LookingForGroup_auto_add || (pl->GetGroup() && pl->GetGroup()->GetLeaderGuid() != pl->GetObjectGuid()))
        {
            return;
        }

        // skip non auto-join or empty slots, or non compatible slots
        if (!pl->m_lookingForGroup.more.canAutoJoin() || !_player->m_lookingForGroup.HaveInSlot(pl->m_lookingForGroup.more))
        {
            return;
        }

        // attempt create group, or skip
        if (!pl->GetGroup())
        {
            Group* group = new Group;
            if (!group->Create(pl->GetObjectGuid(), pl->GetName()))
            {
                delete group;
                return;
            }

            sObjectMgr.AddGroup(group);
        }

        // stop at success join
        if (pl->GetGroup()->AddMember(_player->GetObjectGuid(), _player->GetName()))
        {
            if (sWorld.getConfig(CONFIG_BOOL_RESTRICTED_LFG_CHANNEL) && _player->GetSession()->GetSecurity() == SEC_PLAYER)
            {
                _player->LeaveLFGChannel();
            }
        }
        // full
        else
        {
            if (sWorld.getConfig(CONFIG_BOOL_RESTRICTED_LFG_CHANNEL) && pl->GetSession()->GetSecurity() == SEC_PLAYER)
            {
                pl->LeaveLFGChannel();
            }
        }
    });
}

static void AttemptAddMore(Player* _player)
{
    // skip not group leader case
    if (_player->GetGroup() && _player->GetGroup()->GetLeaderGuid() != _player->GetObjectGuid())
    {
        return;
    }

    if (!_player->m_lookingForGroup.more.canAutoJoin())
    {
        return;
    }

    // TODO: Guard Player map
    sObjectAccessor.DoForAllPlayers([&](Player* pl)
    {

        // skip enemies and self
        if (!pl || pl == _player || pl->GetTeam() != _player->GetTeam())
        {
            return;
        }

        if (!pl->IsInWorld())
        {
            return;
        }

        // skip not auto join or in group
        if (!pl->GetSession()->LookingForGroup_auto_join || pl->GetGroup())
        {
            return;
        }

        if (!pl->m_lookingForGroup.HaveInSlot(_player->m_lookingForGroup.more))
        {
            return;
        }

        // attempt create group if need, or stop attempts
        if (!_player->GetGroup())
        {
            Group* group = new Group;
            if (!group->Create(_player->GetObjectGuid(), _player->GetName()))
            {
                delete group;
                return;                                     // can't create group (??)
            }

            sObjectMgr.AddGroup(group);
        }

        // stop at join fail (full)
        if (!_player->GetGroup()->AddMember(pl->GetObjectGuid(), pl->GetName()))
        {
            if (sWorld.getConfig(CONFIG_BOOL_RESTRICTED_LFG_CHANNEL) && _player->GetSession()->GetSecurity() == SEC_PLAYER)
            {
                _player->LeaveLFGChannel();
            }
        }

        // joined
        else if (sWorld.getConfig(CONFIG_BOOL_RESTRICTED_LFG_CHANNEL) && pl->GetSession()->GetSecurity() == SEC_PLAYER)
        {
            pl->LeaveLFGChannel();
        }

        // and group full
        else if (_player->GetGroup()->IsFull())
        {
            if (sWorld.getConfig(CONFIG_BOOL_RESTRICTED_LFG_CHANNEL) && _player->GetSession()->GetSecurity() == SEC_PLAYER)
            {
                _player->LeaveLFGChannel();
            }
        }
    });
}

void WorldSession::HandleLfgSetAutoJoinOpcode(WorldPacket & /*recv_data*/)
{
    DEBUG_LOG("CMSG_LFG_SET_AUTOJOIN");
    LookingForGroup_auto_join = true;

    if (!_player)                                           // needed because STATUS_AUTHED
    {
        return;
    }

    AttemptJoin(_player);
}

void WorldSession::HandleLfgClearAutoJoinOpcode(WorldPacket & /*recv_data*/)
{
    DEBUG_LOG("CMSG_LFG_CLEAR_AUTOJOIN");
    LookingForGroup_auto_join = false;
}

void WorldSession::HandleLfmSetAutoFillOpcode(WorldPacket & /*recv_data*/)
{
    DEBUG_LOG("CMSG_LFM_SET_AUTOFILL");
    LookingForGroup_auto_add = true;

    if (!_player)                                           // needed because STATUS_AUTHED
    {
        return;
    }

    AttemptAddMore(_player);
}

void WorldSession::HandleLfmClearAutoFillOpcode(WorldPacket & /*recv_data*/)
{
    DEBUG_LOG("CMSG_LFM_CLEAR_AUTOFILL");
    LookingForGroup_auto_add = false;
}

void WorldSession::HandleLfgClearOpcode(WorldPacket & /*recv_data */)
{
    // empty packet
    DEBUG_LOG("CMSG_CLEAR_LOOKING_FOR_GROUP");

    for (int i = 0; i < MAX_LOOKING_FOR_GROUP_SLOT; ++i)
    {
        _player->m_lookingForGroup.slots[i].Clear();
    }

    if (sWorld.getConfig(CONFIG_BOOL_RESTRICTED_LFG_CHANNEL) && _player->GetSession()->GetSecurity() == SEC_PLAYER)
    {
        _player->LeaveLFGChannel();
    }
}

void WorldSession::HandleLfmClearOpcode(WorldPacket & /*recv_data */)
{
    // empty packet
    DEBUG_LOG("CMSG_CLEAR_LOOKING_FOR_MORE");

    _player->m_lookingForGroup.more.Clear();
}

void WorldSession::HandleSetLfmOpcode(WorldPacket& recv_data)
{
    DEBUG_LOG("CMSG_SET_LOOKING_FOR_MORE");
    // recv_data.hexlike();
    uint32 temp;

    recv_data >> temp;

    uint32 entry = (temp & 0xFFFF);
    uint32 type = ((temp >> 24) & 0xFFFF);

    _player->m_lookingForGroup.more.Set(entry, type);
    DEBUG_LOG("LFM set: temp %u, zone %u, type %u", temp, entry, type);

    if (LookingForGroup_auto_add)
    {
        AttemptAddMore(_player);
    }

    SendLfgResult(LfgType(type), entry, LFM_MODE);
}

void WorldSession::HandleSetLfgCommentOpcode(WorldPacket& recv_data)
{
    DEBUG_LOG("CMSG_SET_LFG_COMMENT");
    // recv_data.hexlike();

    std::string comment;
    recv_data >> comment;
    DEBUG_LOG("LFG comment %s", comment.c_str());

    _player->m_lookingForGroup.comment = comment;
}

void WorldSession::HandleLookingForGroup(WorldPacket& recv_data)
{
    DEBUG_LOG("MSG_LOOKING_FOR_GROUP");
    // recv_data.hexlike();
    uint32 type, entry, unk;

    recv_data >> type >> entry >> unk;
    DEBUG_LOG("MSG_LOOKING_FOR_GROUP: type %u, entry %u, unk %u", type, entry, unk);

    if (LookingForGroup_auto_add)
    {
        AttemptAddMore(_player);
    }

    if (LookingForGroup_auto_join)
    {
        AttemptJoin(_player);
    }

    SendLfgResult(LfgType(type), entry, LFG_MODE);
}

void WorldSession::SendLfgResult(LfgType type, uint32 entry, LfgMode lfg_mode)
{
    uint32 number = 0;

    // start prepare packet;
    WorldPacket data(MSG_LOOKING_FOR_GROUP);
    data << uint32(type);                                   // type
    data << uint32(entry);                                  // entry from LFGDungeons.dbc
    data << uint32(0);                                      // count, placeholder
    data << uint32(0);                                      // count again, strange, placeholder

    // TODO: Guard Player map
    sObjectAccessor.DoForAllPlayers([&](Player* pl)
    {

        if (!pl || pl->GetTeam() != _player->GetTeam())
        {
            return;
        }

        if (!pl->IsInWorld())
        {
            return;
        }

        if (!pl->m_lookingForGroup.HaveInSlot(entry, type))
        {
            return;
        }

        ++number;

        data << pl->GetObjectGuid().WriteAsPacked();       // packed guid
        data << uint32(pl->getLevel());                    // level
        data << uint32(pl->GetZoneId());                   // current zone
        data << uint8(lfg_mode);                            // 0x00 - LFG, 0x01 - LFM

        for (uint8 j = 0; j < MAX_LOOKING_FOR_GROUP_SLOT; ++j)
        {
            data << uint32(pl->m_lookingForGroup.slots[j].entry | (pl->m_lookingForGroup.slots[j].type << 24));
        }

        data << pl->m_lookingForGroup.comment;

        Group* group = pl->GetGroup();
        if (group)
        {
            data << uint32(group->GetMembersCount() - 1);   // count of group members without group leader
            for (GroupReference* itr = group->GetFirstMember(); itr != NULL; itr = itr->next())
            {
                Player* member = itr->getSource();
                if (member && member->GetObjectGuid() != pl->GetObjectGuid())
                {
                    data << member->GetPackGUID();          // packed guid
                    data << uint32(member->getLevel());     // player level
                }
            }
        }
        else
        {
            data << uint32(0x00);
        }
    });

    // fill count placeholders
    data.put<uint32>(4 + 4,  number);
    data.put<uint32>(4 + 4 + 4, number);

    SendPacket(&data);
}

void WorldSession::HandleSetLfgOpcode(WorldPacket& recv_data)
{
    DEBUG_LOG("CMSG_SET_LOOKING_FOR_GROUP");

    uint32 slot, temp;

    recv_data >> slot >> temp;

    uint32 entry = (temp & 0xFFFF);
    uint32 type = ((temp >> 24) & 0xFFFF);

    if (slot >= MAX_LOOKING_FOR_GROUP_SLOT)
    {
        return;
    }

    _player->m_lookingForGroup.slots[slot].Set(entry, type);
    DEBUG_LOG("LFG set: looknumber %u, temp %X, type %u, entry %u", slot, temp, type, entry);

    if (LookingForGroup_auto_join)
    {
        AttemptJoin(_player);
    }

    SendLfgResult(LfgType(type), entry, LFG_MODE);
}
