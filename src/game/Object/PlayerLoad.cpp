/**
 * MaNGOS is a full featured server for World of Warcraft, supporting
 * the following clients: 1.12.x, 2.4.3, 3.3.5a, 4.3.4a and 5.4.8
 *
 * Copyright (C) 2005-2025 MaNGOS <https://www.getmangos.eu>
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

/**
 * @file PlayerLoad.cpp
 * @brief Cohesion split of Player.cpp.
 *        Re-applied onto MangosOne TBC 2.4.3; same class, pure code move,
 *        no behaviour change. CMake file(GLOB) picks this TU up automatically.
 */

#include "Player.h"
#include "Language.h"
#include "Database/DatabaseEnv.h"
#include "Log.h"
#include "Opcodes.h"
#include "SpellMgr.h"
#include "World.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include "UpdateMask.h"
#include "CinematicFlyover.h"
#include "SkillDiscovery.h"
#include "QuestDef.h"
#include "GossipDef.h"
#include "UpdateData.h"
#include "Channel.h"
#include "ChannelMgr.h"
#include "MapManager.h"
#include "MapPersistentStateMgr.h"
#include "InstanceData.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "CellImpl.h"
#include "ObjectMgr.h"
#include "ObjectAccessor.h"
#include "Formulas.h"
#include "Group.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "Pet.h"
#include "Util.h"
#include "Transports.h"
#include "Weather.h"
#include "BattleGround/BattleGround.h"
#include "BattleGround/BattleGroundMgr.h"
#include "BattleGround/BattleGroundAV.h"
#include "OutdoorPvP/OutdoorPvP.h"
#include "ArenaTeam.h"
#include "Chat.h"
#include "revision_data.h"
#include "Database/DatabaseImpl.h"
#include "Spell.h"
#include "ScriptMgr.h"
#include "SocialMgr.h"
#include "Mail.h"
#include "DBCStores.h"
#include "SQLStorages.h"
#include "DisableMgr.h"
#ifdef ENABLE_ELUNA
#include "LuaEngine.h"
#endif /* ENABLE_ELUNA */

#ifdef ENABLE_PLAYERBOTS
#include "playerbot.h"
#endif

#include <cmath>

#define ZONE_UPDATE_INTERVAL (1*IN_MILLISECONDS)

#define PLAYER_SKILL_INDEX(x)       (PLAYER_SKILL_INFO_1_1 + ((x)*3))
#define PLAYER_SKILL_VALUE_INDEX(x) (PLAYER_SKILL_INDEX(x)+1)
#define PLAYER_SKILL_BONUS_INDEX(x) (PLAYER_SKILL_INDEX(x)+2)

#define SKILL_VALUE(x)         PAIR32_LOPART(x)
#define SKILL_MAX(x)           PAIR32_HIPART(x)
#define MAKE_SKILL_VALUE(v, m) MAKE_PAIR32(v,m)

#define SKILL_TEMP_BONUS(x)    int16(PAIR32_LOPART(x))
#define SKILL_PERM_BONUS(x)    int16(PAIR32_HIPART(x))
#define MAKE_SKILL_BONUS(t, p) MAKE_PAIR32(t,p)


// Constants shared with the parent TU (duplicated, guarded).
#ifndef DEATH_EXPIRE_STEP
#define DEATH_EXPIRE_STEP (5*MINUTE)
#endif
#ifndef MAX_DEATH_COUNT
#define MAX_DEATH_COUNT 3
#endif

/*********************************************************/
/***                   LOAD SYSTEM                     ***/
/*********************************************************/

void Player::_LoadDeclinedNames(QueryResult* result)
{
    if (!result)
    {
        return;
    }

    delete m_declinedname;
    m_declinedname = new DeclinedName;

    Field* fields = result->Fetch();
    for (int i = 0; i < MAX_DECLINED_NAME_CASES; ++i)
    {
        m_declinedname->name[i] = fields[i].GetCppString();
    }

    delete result;
}

void Player::_LoadArenaTeamInfo(QueryResult* result)
{
    // arenateamid, played_week, played_season, personal_rating
    memset((void*)&m_uint32Values[PLAYER_FIELD_ARENA_TEAM_INFO_1_1], 0, sizeof(uint32) * MAX_ARENA_SLOT * ARENA_TEAM_END);
    if (!result)
    {
        return;
    }

    do
    {
        Field* fields = result->Fetch();

        uint32 arenateamid     = fields[0].GetUInt32();
        uint32 played_week     = fields[1].GetUInt32();
        uint32 played_season   = fields[2].GetUInt32();
        uint32 personal_rating = fields[3].GetUInt32();

        ArenaTeam* aTeam = sObjectMgr.GetArenaTeamById(arenateamid);
        if (!aTeam)
        {
            sLog.outError("Player::_LoadArenaTeamInfo: couldn't load arenateam %u, week %u, season %u, rating %u", arenateamid, played_week, played_season, personal_rating);
            continue;
        }
        uint8  arenaSlot = aTeam->GetSlot();

        SetArenaTeamInfoField(arenaSlot, ARENA_TEAM_ID, arenateamid);
        SetArenaTeamInfoField(arenaSlot, ARENA_TEAM_MEMBER, (aTeam->GetCaptainGuid() == GetObjectGuid()) ? 0 : 1);
        SetArenaTeamInfoField(arenaSlot, ARENA_TEAM_GAMES_WEEK, played_week);
        SetArenaTeamInfoField(arenaSlot, ARENA_TEAM_GAMES_SEASON, played_season);
        SetArenaTeamInfoField(arenaSlot, ARENA_TEAM_WINS_SEASON, 0);
        SetArenaTeamInfoField(arenaSlot, ARENA_TEAM_PERSONAL_RATING, personal_rating);
    }
    while (result->NextRow());
    delete result;
}

/**
 * @brief Loads battleground return and participation data from the database.
 *
 * @param result The query result containing battleground data.
 */
void Player::_LoadBGData(QueryResult* result)
{
    if (!result)
    {
        return;
    }

    // Expecting only one row
    Field* fields = result->Fetch();
    /* bgInstanceID, bgTeam, x, y, z, o, map */
    m_bgData.bgInstanceID = fields[0].GetUInt32();
    m_bgData.bgTeam       = Team(fields[1].GetUInt32());
    m_bgData.joinPos      = WorldLocation(fields[6].GetUInt32(),    // Map
                                          fields[2].GetFloat(),     // X
                                          fields[3].GetFloat(),     // Y
                                          fields[4].GetFloat(),     // Z
                                          fields[5].GetFloat());    // Orientation

    delete result;
}

/**
 * @brief Loads a character position directly from the database.
 *
 * @param guid The player GUID to query.
 * @param mapid Output map identifier.
 * @param x Output X coordinate.
 * @param y Output Y coordinate.
 * @param z Output Z coordinate.
 * @param o Output orientation.
 * @param in_flight Output flag indicating whether the character was in flight.
 * @return True if the position was loaded successfully; otherwise, false.
 */
bool Player::LoadPositionFromDB(ObjectGuid guid, uint32& mapid, float& x, float& y, float& z, float& o, bool& in_flight)
{
    QueryResult* result = CharacterDatabase.PQuery("SELECT `position_x`,`position_y`,`position_z`,`orientation`,`map`,`taxi_path` FROM `characters` WHERE `guid` = '%u'", guid.GetCounter());
    if (!result)
    {
        return false;
    }

    Field* fields = result->Fetch();

    x = fields[0].GetFloat();
    y = fields[1].GetFloat();
    z = fields[2].GetFloat();
    o = fields[3].GetFloat();
    mapid = fields[4].GetUInt32();
    in_flight = !fields[5].GetCppString().empty();

    delete result;
    return true;
}

/**
 * @brief Loads serialized uint32 values into the player's update field array.
 *
 * @param data The serialized space-separated field data.
 * @param startOffset The first update-field offset to populate.
 * @param count The number of uint32 values expected.
 */
void Player::_LoadIntoDataField(const char* data, uint32 startOffset, uint32 count)
{
    if (!data)
    {
        return;
    }

    Tokens tokens = StrSplit(data, " ");

    if (tokens.size() != count)
    {
        return;
    }

    Tokens::iterator iter;
    uint32 index;
    for (iter = tokens.begin(), index = 0; index < count; ++iter, ++index)
    {
        m_uint32Values[startOffset + index] = atol((*iter).c_str());
    }
}

/**
 * @brief Loads action bar bindings from the database.
 *
 * @param result The query result containing action bindings.
 */
void Player::_LoadActions(QueryResult* result)
{
    m_actionButtons.clear();

    // QueryResult *result = CharacterDatabase.PQuery("SELECT `button`,`action`,`type` FROM `character_action` WHERE `guid` = '%u' ORDER BY `button`",GetGUIDLow());

    if (result)
    {
        do
        {
            Field* fields = result->Fetch();

            uint8 button = fields[0].GetUInt8();
            uint32 action = fields[1].GetUInt32();
            uint8 type = fields[2].GetUInt8();

            if (ActionButton* ab = addActionButton(button, action, type))
            {
                ab->uState = ACTIONBUTTON_UNCHANGED;
            }
            else
            {
                sLog.outError("  ...at loading, and will deleted in DB also");

                // Will deleted in DB at next save (it can create data until save but marked as deleted)
                m_actionButtons[button].uState = ACTIONBUTTON_DELETED;
            }
        }
        while (result->NextRow());

        delete result;
    }
}

/**
 * @brief Loads saved auras from the database and restores their remaining durations.
 *
 * @param result The query result containing aura data.
 * @param timediff The elapsed offline time used to age aura durations.
 */
void Player::_LoadAuras(QueryResult* result, uint32 timediff)
{
    // RemoveAllAuras(); -- some spells casted before aura load, for example in LoadSkills, aura list explicitly cleaned early

    // all aura related fields
    for (int i = UNIT_FIELD_AURA; i <= UNIT_FIELD_AURASTATE; ++i)
    {
        SetUInt32Value(i, 0);
    }

    // QueryResult *result = CharacterDatabase.PQuery("SELECT `caster_guid`,`item_guid`,`spell`,`stackcount`,`remaincharges`,`basepoints0`,`basepoints1`,`basepoints2`,`periodictime0`,`periodictime1`,`periodictime2`,`maxduration`,`remaintime`,`effIndexMask` FROM `character_aura` WHERE `guid` = '%u'",GetGUIDLow());

    if (result)
    {
        do
        {
            Field* fields = result->Fetch();
            ObjectGuid caster_guid = ObjectGuid(fields[0].GetUInt64());
            uint32 item_lowguid = fields[1].GetUInt32();
            uint32 spellid = fields[2].GetUInt32();
            uint32 stackcount = fields[3].GetUInt32();
            uint32 remaincharges = fields[4].GetUInt32();
            int32  damage[MAX_EFFECT_INDEX];
            uint32 periodicTime[MAX_EFFECT_INDEX];

            for (int32 i = 0; i < MAX_EFFECT_INDEX; ++i)
            {
                damage[i] = fields[i + 5].GetInt32();
                periodicTime[i] = fields[i + 8].GetUInt32();
            }

            int32 maxduration = fields[11].GetInt32();
            int32 remaintime = fields[12].GetInt32();
            uint32 effIndexMask = fields[13].GetUInt32();

            SpellEntry const* spellproto = sSpellStore.LookupEntry(spellid);
            if (!spellproto)
            {
                sLog.outError("Unknown spell (spellid %u), ignore.", spellid);
                continue;
            }

            if (remaintime != -1 && !IsPositiveSpell(spellproto))
            {
                if (remaintime / IN_MILLISECONDS <= int32(timediff))
                {
                    continue;
                }

                remaintime -= timediff * IN_MILLISECONDS;
            }

            // prevent wrong values of remaincharges
            if (spellproto->procCharges == 0)
            {
                remaincharges = 0;
            }

            if (!spellproto->StackAmount)
            {
                stackcount = 1;
            }
            else if (spellproto->StackAmount < stackcount)
            {
                stackcount = spellproto->StackAmount;
            }
            else if (!stackcount)
            {
                stackcount = 1;
            }

            SpellAuraHolder* holder = CreateSpellAuraHolder(spellproto, this, NULL);
            holder->SetLoadedState(caster_guid, ObjectGuid(HIGHGUID_ITEM, item_lowguid), stackcount, remaincharges, maxduration, remaintime);

            for (int32 i = 0; i < MAX_EFFECT_INDEX; ++i)
            {
                if ((effIndexMask & (1 << i)) == 0)
                {
                    continue;
                }

                Aura* aura = CreateAura(spellproto, SpellEffectIndex(i), NULL, holder, this);
                if (!damage[i])
                {
                    damage[i] = aura->GetModifier()->m_amount;
                }

                aura->SetLoadedState(damage[i], periodicTime[i]);
                holder->AddAura(aura, SpellEffectIndex(i));
            }

            if (!holder->IsEmptyHolder())
            {
                // reset stolen single target auras
                if (caster_guid != GetObjectGuid() && holder->GetTrackedAuraType() == TRACK_AURA_TYPE_SINGLE_TARGET)
                {
                    holder->SetTrackedAuraType(TRACK_AURA_TYPE_NOT_TRACKED);
                }

                AddSpellAuraHolder(holder);
                DETAIL_LOG("Added auras from spellid %u", spellproto->Id);
            }
            else
            {
                delete holder;
            }
        }
        while (result->NextRow());
        delete result;
    }

    if (getClass() == CLASS_WARRIOR && !HasAuraType(SPELL_AURA_MOD_SHAPESHIFT))
    {
        CastSpell(this, SPELL_ID_PASSIVE_BATTLE_STANCE, true);
    }
}

/**
 * @brief Restores corpse state for a dead player or cleans it up for a living one.
 */
void Player::LoadCorpse()
{
    if (IsAlive())
    {
        sObjectAccessor.ConvertCorpseForPlayer(GetObjectGuid());
    }
    else
    {
        if (Corpse* corpse = GetCorpse())
        {
            ApplyModByteFlag(PLAYER_FIELD_BYTES, 0, PLAYER_FIELD_BYTE_RELEASE_TIMER, corpse && !sMapStore.LookupEntry(corpse->GetMapId())->Instanceable());
        }
        else
        {
            // Prevent Dead Player login without corpse
            ResurrectPlayer(0.5f);
        }
    }
}

/**
 * @brief Loads inventory, bank, and equipped items from the database.
 *
 * @param result The query result containing item inventory rows.
 * @param timediff The elapsed offline time used for time-sensitive item checks.
 */
void Player::_LoadInventory(QueryResult* result, uint32 timediff)
{
    // QueryResult *result = CharacterDatabase.PQuery("SELECT `data`,`bag`,`slot`,`item`,`item_template` FROM `character_inventory` JOIN `item_instance` ON `character_inventory`.`item` = `item_instance`.`guid` WHERE `character_inventory`.`guid` = '%u' ORDER BY `bag`,`slot`", GetGUIDLow());
    std::map<uint32, Bag*> bagMap;                          // fast guid lookup for bags
    // NOTE: the "order by `bag`" is important because it makes sure
    // the bagMap is filled before items in the bags are loaded
    // NOTE2: the "order by `slot`" is needed because mainhand weapons are (wrongly?)
    // expected to be equipped before offhand items (TODO: fixme)

    uint32 zone = GetZoneId();

    if (result)
    {
        std::list<Item*> problematicItems;

        // prevent items from being added to the queue when stored
        m_itemUpdateQueueBlocked = true;
        do
        {
            Field* fields = result->Fetch();
            uint32 bag_guid  = fields[1].GetUInt32();
            uint8  slot      = fields[2].GetUInt8();
            uint32 item_lowguid = fields[3].GetUInt32();
            uint32 item_id   = fields[4].GetUInt32();

            ItemPrototype const* proto = ObjectMgr::GetItemPrototype(item_id);

            if (!proto)
            {
                CharacterDatabase.PExecute("DELETE FROM `character_inventory` WHERE `item` = '%u'", item_lowguid);
                CharacterDatabase.PExecute("DELETE FROM `item_instance` WHERE `guid` = '%u'", item_lowguid);
                sLog.outError("Player::_LoadInventory: Player %s has an unknown item (id: #%u) in inventory, deleted.", GetName(), item_id);
                continue;
            }

            Item* item = NewItemOrBag(proto);

            if (!item->LoadFromDB(item_lowguid, fields, GetObjectGuid()))
            {
                sLog.outError("Player::_LoadInventory: Player %s has broken item (id: #%u) in inventory, deleted.", GetName(), item_id);
                CharacterDatabase.PExecute("DELETE FROM `character_inventory` WHERE `item` = '%u'", item_lowguid);
                item->FSetState(ITEM_REMOVED);
                item->SaveToDB();                           // it also deletes item object !
                continue;
            }

            // not allow have in alive state item limited to another map/zone
            if (IsAlive() && item->IsLimitedToAnotherMapOrZone(GetMapId(), zone))
            {
                CharacterDatabase.PExecute("DELETE FROM `character_inventory` WHERE `item` = '%u'", item_lowguid);
                item->FSetState(ITEM_REMOVED);
                item->SaveToDB();                           // it also deletes item object !
                continue;
            }

            // "Conjured items disappear if you are logged out for more than 15 minutes"
            if (timediff > 15 * MINUTE && (item->GetProto()->Flags & ITEM_FLAG_CONJURED))
            {
                CharacterDatabase.PExecute("DELETE FROM `character_inventory` WHERE `item` = '%u'", item_lowguid);
                item->FSetState(ITEM_REMOVED);
                item->SaveToDB();                           // it also deletes item object !
                continue;
            }

            bool success = true;

            // the item/bag is not in a bag
            if (!bag_guid)
            {
                item->SetContainer(NULL);
                item->SetSlot(slot);

                if (IsInventoryPos(INVENTORY_SLOT_BAG_0, slot))
                {
                    ItemPosCountVec dest;
                    if (CanStoreItem(INVENTORY_SLOT_BAG_0, slot, dest, item, false) == EQUIP_ERR_OK)
                    {
                        item = StoreItem(dest, item, true);
                    }
                    else
                    {
                        success = false;
                    }
                }
                else if (IsEquipmentPos(INVENTORY_SLOT_BAG_0, slot))
                {
                    uint16 dest;
                    if (CanEquipItem(slot, dest, item, false, false) == EQUIP_ERR_OK)
                    {
                        QuickEquipItem(dest, item);
                    }
                    else
                    {
                        success = false;
                    }
                }
                else if (IsBankPos(INVENTORY_SLOT_BAG_0, slot))
                {
                    ItemPosCountVec dest;
                    if (CanBankItem(INVENTORY_SLOT_BAG_0, slot, dest, item, false, false) == EQUIP_ERR_OK)
                    {
                        item = BankItem(dest, item, true);
                    }
                    else
                    {
                        success = false;
                    }
                }

                if (success)
                {
                    // store bags that may contain items in them
                    if (item->IsBag() && IsBagPos(item->GetPos()))
                    {
                        bagMap[item_lowguid] = (Bag*)item;
                    }
                }
            }
            // the item/bag in a bag
            else
            {
                item->SetSlot(NULL_SLOT);
                // the item is in a bag, find the bag
                std::map<uint32, Bag*>::const_iterator itr = bagMap.find(bag_guid);
                if (itr != bagMap.end() && slot < itr->second->GetBagSize())
                {
                    ItemPosCountVec dest;
                    if (CanStoreItem(itr->second->GetSlot(), slot, dest, item, false) == EQUIP_ERR_OK)
                    {
                        item = StoreItem(dest, item, true);
                    }
                    else
                    {
                        success = false;
                    }
                }
                else
                {
                    success = false;
                }
            }

            // item's state may have changed after stored
            if (success)
            {
                item->SetState(ITEM_UNCHANGED, this);

                // restore container unchanged state also
                if (item->GetContainer())
                {
                    item->GetContainer()->SetState(ITEM_UNCHANGED, this);
                }
            }
            else
            {
                sLog.outError("Player::_LoadInventory: Player %s has item (GUID: %u Entry: %u) can't be loaded to inventory (Bag GUID: %u Slot: %u) by some reason, will send by mail.", GetName(), item_lowguid, item_id, bag_guid, slot);
                CharacterDatabase.PExecute("DELETE FROM `character_inventory` WHERE `item` = '%u'", item_lowguid);
                problematicItems.push_back(item);
            }
        }
        while (result->NextRow());

        delete result;
        m_itemUpdateQueueBlocked = false;

        // send by mail problematic items
        while (!problematicItems.empty())
        {
            std::string subject = "Item could not be loaded to inventory.";
            std::string content = GetSession()->GetMangosString(LANG_NOT_EQUIPPED_ITEM);
            // fill mail
            MailDraft draft(subject,"");
            draft.SetSubjectAndBody(subject,content);
            for (int i = 0; !problematicItems.empty() && i < MAX_MAIL_ITEMS; ++i)
            {
                Item* item = problematicItems.front();
                problematicItems.pop_front();

                draft.AddItem(item);
            }

            draft.SendMailTo(this, MailSender(this, MAIL_STATIONERY_GM), MAIL_CHECK_MASK_COPIED);
        }
    }

    // if (IsAlive())
    _ApplyAllItemMods();
}

/**
 * @brief Loads persisted item loot contents for the player's items.
 *
 * @param result The query result containing saved item loot rows.
 */
void Player::_LoadItemLoot(QueryResult* result)
{
    // QueryResult *result = CharacterDatabase.PQuery("SELECT `guid`,`itemid`,`amount`,`suffix`,`property` FROM `item_loot` WHERE `guid` = '%u'", GetGUIDLow());

    if (result)
    {
        do
        {
            Field* fields = result->Fetch();
            uint32 item_guid   = fields[0].GetUInt32();

            Item* item = GetItemByGuid(ObjectGuid(HIGHGUID_ITEM, item_guid));

            if (!item)
            {
                CharacterDatabase.PExecute("DELETE FROM `item_loot` WHERE `guid` = '%u'", item_guid);
                sLog.outError("Player::_LoadItemLoot: Player %s has loot for nonexistent item (GUID: %u) in `item_loot`, deleted.", GetName(), item_guid);
                continue;
            }

            item->LoadLootFromDB(fields);
        }
        while (result->NextRow());

        delete result;
    }
}

// load mailed item which should receive current player
void Player::_LoadMailedItems(QueryResult* result)
{
    // data needs to be at first place for Item::LoadFromDB
    //         0     1     2        3          4
    // "SELECT data, text, mail_id, item_guid, item_template FROM mail_items JOIN item_instance ON item_guid = guid WHERE receiver = '%u'", GUID_LOPART(m_guid)
    if (!result)
    {
        return;
    }

    do
    {
        Field* fields = result->Fetch();
        uint32 mail_id       = fields[1].GetUInt32();
        uint32 item_guid_low = fields[2].GetUInt32();
        uint32 item_template = fields[3].GetUInt32();

        Mail* mail = GetMail(mail_id);
        if (!mail)
        {
            continue;
        }
        mail->AddItem(item_guid_low, item_template);

        ItemPrototype const* proto = ObjectMgr::GetItemPrototype(item_template);

        if (!proto)
        {
            sLog.outError("Player %u has unknown item_template (ProtoType) in mailed items(GUID: %u template: %u) in mail (%u), deleted.", GetGUIDLow(), item_guid_low, item_template, mail->messageID);
            CharacterDatabase.PExecute("DELETE FROM `mail_items` WHERE `item_guid` = '%u'", item_guid_low);
            CharacterDatabase.PExecute("DELETE FROM `item_instance` WHERE `guid` = '%u'", item_guid_low);
            continue;
        }

        Item* item = NewItemOrBag(proto);

        if (!item->LoadFromDB(item_guid_low, fields, GetObjectGuid()))
        {
            sLog.outError("Player::_LoadMailedItems - Item in mail (%u) doesn't exist !!!! - item guid: %u, deleted from mail", mail->messageID, item_guid_low);
            CharacterDatabase.PExecute("DELETE FROM `mail_items` WHERE `item_guid` = '%u'", item_guid_low);
            item->FSetState(ITEM_REMOVED);
            item->SaveToDB();                               // it also deletes item object !
            continue;
        }

        AddMItem(item);
    }
    while (result->NextRow());

    delete result;
}

/**
 * @brief Loads the player's mailbox headers from the database.
 *
 * @param result The query result containing mail records.
 */
void Player::_LoadMails(QueryResult* result)
{
    m_mail.clear();
    //        0  1           2      3        4       5    6           7            8     9   10      11         12             13
    //"SELECT id,messageType,sender,receiver,subject,body,expire_time,deliver_time,money,cod,checked,stationery,mailTemplateId,has_items FROM mail WHERE receiver = '%u' ORDER BY id DESC", GetGUIDLow()
    if (!result)
    {
        return;
    }

    do
    {
        Field* fields = result->Fetch();
        Mail* m = new Mail;
        m->messageID = fields[0].GetUInt32();
        m->messageType = fields[1].GetUInt8();
        m->sender = fields[2].GetUInt32();
        m->receiverGuid = ObjectGuid(HIGHGUID_PLAYER, fields[3].GetUInt32());
        m->subject = fields[4].GetCppString();
        m->body = fields[5].GetCppString();
        m->expire_time = (time_t)fields[6].GetUInt64();
        m->deliver_time = (time_t)fields[7].GetUInt64();
        m->money = fields[8].GetUInt32();
        m->COD = fields[9].GetUInt32();
        m->checked = fields[10].GetUInt32();
        m->stationery = fields[11].GetUInt8();
        m->mailTemplateId = fields[12].GetInt16();
        m->has_items = fields[13].GetBool();                // true, if mail have items or mail have template and items generated (maybe none)

        if (m->mailTemplateId && !sMailTemplateStore.LookupEntry(m->mailTemplateId))
        {
            sLog.outError("Player::_LoadMail - Mail (%u) have nonexistent MailTemplateId (%u), remove at load", m->messageID, m->mailTemplateId);
            m->mailTemplateId = 0;
        }

        m->state = MAIL_STATE_UNCHANGED;

        m_mail.push_back(m);

        if (m->mailTemplateId && !m->has_items)
        {
            m->prepareTemplateItems(this);
        }
    }
    while (result->NextRow());
    delete result;
}

/**
 * @brief Loads the player's currently active pet from the database when possible.
 */
void Player::LoadPet()
{
    // fixme: the pet should still be loaded if the player is not in world
    // just not added to the map
    if (IsInWorld())
    {
        Pet* pet = new Pet;
        if (!pet->LoadPetFromDB(this, 0, 0, true))
        {
            delete pet;
        }
    }
}

/**
 * @brief Loads quest status records and rebuilds the in-memory quest log state.
 *
 * @param result The query result containing quest status rows.
 */
void Player::_LoadQuestStatus(QueryResult* result)
{
    mQuestStatus.clear();

    uint32 slot = 0;

    ////                                                       0        1         2           3           4        5            6            7            8            9             10            11            12
    // QueryResult *result = CharacterDatabase.PQuery("SELECT `quest`, `status`, `rewarded`, `explored`, `timer`, `mobcount1`, `mobcount2`, `mobcount3`, `mobcount4`, `itemcount1`, `itemcount2`, `itemcount3`, `itemcount4` FROM `character_queststatus` WHERE `guid` = '%u'", GetGUIDLow());

    if (result)
    {
        do
        {
            Field* fields = result->Fetch();

            uint32 quest_id = fields[0].GetUInt32();
            // used to be new, no delete?
            Quest const* pQuest = sObjectMgr.GetQuestTemplate(quest_id);
            if (pQuest)
            {
                // find or create
                QuestStatusData& questStatusData = mQuestStatus[quest_id];

                uint32 qstatus = fields[1].GetUInt32();
                if (qstatus < MAX_QUEST_STATUS)
                {
                    questStatusData.m_status = QuestStatus(qstatus);
                }
                else
                {
                    questStatusData.m_status = QUEST_STATUS_NONE;
                    sLog.outError("Player %s have invalid quest %d status (%d), replaced by QUEST_STATUS_NONE(0).", GetName(), quest_id, qstatus);
                }

                questStatusData.m_rewarded = (fields[2].GetUInt8() > 0);
                questStatusData.m_explored = (fields[3].GetUInt8() > 0);

                time_t quest_time = time_t(fields[4].GetUInt64());

                if (pQuest->HasSpecialFlag(QUEST_SPECIAL_FLAG_TIMED) && !GetQuestRewardStatus(quest_id) && questStatusData.m_status != QUEST_STATUS_NONE)
                {
                    AddTimedQuest(quest_id);

                    if (quest_time <= sWorld.GetGameTime())
                    {
                        questStatusData.m_timer = 1;
                    }
                    else
                    {
                        questStatusData.m_timer = uint32(quest_time - sWorld.GetGameTime()) * IN_MILLISECONDS;
                    }
                }
                else
                {
                    quest_time = 0;
                }

                questStatusData.m_creatureOrGOcount[0] = fields[5].GetUInt32();
                questStatusData.m_creatureOrGOcount[1] = fields[6].GetUInt32();
                questStatusData.m_creatureOrGOcount[2] = fields[7].GetUInt32();
                questStatusData.m_creatureOrGOcount[3] = fields[8].GetUInt32();
                questStatusData.m_itemcount[0] = fields[9].GetUInt32();
                questStatusData.m_itemcount[1] = fields[10].GetUInt32();
                questStatusData.m_itemcount[2] = fields[11].GetUInt32();
                questStatusData.m_itemcount[3] = fields[12].GetUInt32();

                questStatusData.uState = QUEST_UNCHANGED;

                // add to quest log
                if (slot < MAX_QUEST_LOG_SIZE &&
                    ((questStatusData.m_status == QUEST_STATUS_INCOMPLETE ||
                      questStatusData.m_status == QUEST_STATUS_COMPLETE ||
                      questStatusData.m_status == QUEST_STATUS_FAILED) &&
                     (!questStatusData.m_rewarded || pQuest->IsRepeatable())))
                {
                    SetQuestSlot(slot, quest_id, uint32(quest_time));

                    if (questStatusData.m_explored)
                    {
                        SetQuestSlotState(slot, QUEST_STATE_COMPLETE);
                    }

                    if (questStatusData.m_status == QUEST_STATUS_COMPLETE)
                    {
                        SetQuestSlotState(slot, QUEST_STATE_COMPLETE);
                    }

                    if (questStatusData.m_status == QUEST_STATUS_FAILED)
                    {
                        SetQuestSlotState(slot, QUEST_STATE_FAIL);
                    }

                    for (uint8 idx = 0; idx < QUEST_OBJECTIVES_COUNT; ++idx)
                    {
                        if (questStatusData.m_creatureOrGOcount[idx])
                        {
                            SetQuestSlotCounter(slot, idx, questStatusData.m_creatureOrGOcount[idx]);
                        }
                    }

                    ++slot;
                }

                if (questStatusData.m_rewarded)
                {
                    // learn rewarded spell if unknown
                    learnQuestRewardedSpells(pQuest);

                    // set rewarded title if any
                    if (pQuest->GetCharTitleId())
                    {
                        if (CharTitlesEntry const* titleEntry = sCharTitlesStore.LookupEntry(pQuest->GetCharTitleId()))
                        {
                            SetTitle(titleEntry);
                        }
                    }
                }

                DEBUG_LOG("Quest status is {%u} for quest {%u} for player (GUID: %u)", questStatusData.m_status, quest_id, GetGUIDLow());
            }
        }
        while (result->NextRow());

        delete result;
    }

    // clear quest log tail
    for (uint16 i = slot; i < MAX_QUEST_LOG_SIZE; ++i)
    {
        SetQuestSlot(i, 0);
    }
}

void Player::_LoadDailyQuestStatus(QueryResult* result)
{
    for (uint32 quest_daily_idx = 0; quest_daily_idx < PLAYER_MAX_DAILY_QUESTS; ++quest_daily_idx)
    {
        SetUInt32Value(PLAYER_FIELD_DAILY_QUESTS_1 + quest_daily_idx, 0);
    }

    // QueryResult *result = CharacterDatabase.PQuery("SELECT `quest` FROM `character_queststatus_daily` WHERE `guid` = '%u'", GetGUIDLow());

    if (result)
    {
        uint32 quest_daily_idx = 0;

        do
        {
            if (quest_daily_idx >= PLAYER_MAX_DAILY_QUESTS) // max amount with exist data in query
            {
                sLog.outError("Player (GUID: %u) have more 25 daily quest records in `charcter_queststatus_daily`", GetGUIDLow());
                break;
            }

            Field* fields = result->Fetch();

            uint32 quest_id = fields[0].GetUInt32();

            Quest const* pQuest = sObjectMgr.GetQuestTemplate(quest_id);
            if (!pQuest)
            {
                continue;
            }

            SetUInt32Value(PLAYER_FIELD_DAILY_QUESTS_1 + quest_daily_idx, quest_id);
            ++quest_daily_idx;

            DEBUG_LOG("Daily quest {%u} cooldown for player (GUID: %u)", quest_id, GetGUIDLow());
        }
        while (result->NextRow());

        delete result;
    }

    m_DailyQuestChanged = false;
}

/**
 * @brief Loads known spells from the database.
 *
 * @param result The query result containing learned spell rows.
 */
void Player::_LoadSpells(QueryResult* result)
{
    // QueryResult *result = CharacterDatabase.PQuery("SELECT `spell`,`active`,`disabled` FROM `character_spell` WHERE `guid` = '%u'",GetGUIDLow());

    if (result)
    {
        do
        {
            Field* fields = result->Fetch();

            uint32 spell_id = fields[0].GetUInt32();

            addSpell(spell_id, fields[1].GetBool(), false, false, fields[2].GetBool());
        }
        while (result->NextRow());

        delete result;
    }
}

/**
 * @brief Loads the player's current group membership from the database.
 *
 * @param result The query result containing the group identifier.
 */
void Player::_LoadGroup(QueryResult* result)
{
    // QueryResult *result = CharacterDatabase.PQuery("SELECT `groupId` FROM `group_member` WHERE `memberGuid`='%u'", GetGUIDLow());
    if (result)
    {
        uint32 groupId = (*result)[0].GetUInt32();
        delete result;

        if (Group* group = sObjectMgr.GetGroupById(groupId))
        {
            uint8 subgroup = group->GetMemberGroup(GetObjectGuid());
            SetGroup(group, subgroup);
            if (getLevel() >= LEVELREQUIREMENT_HEROIC)
            {
                // the group leader may change the instance difficulty while the player is offline
                SetDifficulty(group->GetDifficulty());
            }
        }
    }
}

/**
 * @brief Loads the player's saved dungeon and raid instance bindings.
 *
 * @param result The query result containing character instance bind rows.
 */
void Player::_LoadBoundInstances(QueryResult* result)
{
    for (uint8 i = 0; i < MAX_DIFFICULTY; ++i)
    {
        m_boundInstances[i].clear();
    }

    Group* group = GetGroup();

    // QueryResult *result = CharacterDatabase.PQuery("SELECT `id`, `permanent`, `map`, `difficulty`, `resettime` FROM `character_instance` LEFT JOIN `instance` ON `instance` = `id` WHERE `guid` = '%u'", GUID_LOPART(m_guid));
    if (result)
    {
        do
        {
            Field* fields = result->Fetch();
            bool perm = fields[1].GetBool();
            uint32 mapId = fields[2].GetUInt32();
            uint32 instanceId = fields[0].GetUInt32();
            uint8 difficulty = fields[3].GetUInt8();

            time_t resetTime = (time_t)fields[4].GetUInt64();
            // the resettime for normal instances is only saved when the InstanceSave is unloaded
            // so the value read from the DB may be wrong here but only if the InstanceSave is loaded
            // and in that case it is not used

            MapEntry const* mapEntry = sMapStore.LookupEntry(mapId);
            if (!mapEntry || !mapEntry->IsDungeon())
            {
                sLog.outError("_LoadBoundInstances: player %s(%d) has bind to nonexistent or not dungeon map %d", GetName(), GetGUIDLow(), mapId);
                CharacterDatabase.PExecute("DELETE FROM `character_instance` WHERE `guid` = '%u' AND `instance` = '%u'", GetGUIDLow(), instanceId);
                continue;
            }

            if (difficulty >= MAX_DIFFICULTY)
            {
                sLog.outError("_LoadBoundInstances: player %s(%d) has bind to nonexistent difficulty %d instance for map %u", GetName(), GetGUIDLow(), difficulty, mapId);
                CharacterDatabase.PExecute("DELETE FROM `character_instance` WHERE `guid` = '%u' AND `instance` = '%u'", GetGUIDLow(), instanceId);
                continue;
            }

            if (!perm && group)
            {
                sLog.outError("_LoadBoundInstances: %s is in group (Id: %d) but has a non-permanent character bind to map %d,%d,%d",
                              GetGuidStr().c_str(), group->GetId(), mapId, instanceId, difficulty);
                CharacterDatabase.PExecute("DELETE FROM `character_instance` WHERE `guid` = '%u' AND `instance` = '%u'",
                                           GetGUIDLow(), instanceId);
                continue;
            }

            // since non permanent binds are always solo bind, they can always be reset
            DungeonPersistentState* state = (DungeonPersistentState*)sMapPersistentStateMgr.AddPersistentState(mapEntry, instanceId, Difficulty(difficulty), resetTime, !perm, true);
            if (state)
            {
                BindToInstance(state, perm, true);
            }
        }
        while (result->NextRow());
        delete result;
    }
}

/**
 * @brief Finds the player's instance bind for a specific map.
 *
 * @param mapid The map identifier to look up.
 * @return The matching bind entry, or NULL if none exists.
 */
InstancePlayerBind* Player::GetBoundInstance(uint32 mapid, Difficulty difficulty)
{
    // some instances only have one difficulty
    const MapEntry* entry = sMapStore.LookupEntry(mapid);
    if (!entry || !entry->SupportsHeroicMode())
    {
        difficulty = DUNGEON_DIFFICULTY_NORMAL;
    }

    BoundInstancesMap::iterator itr = m_boundInstances[difficulty].find(mapid);
    if (itr != m_boundInstances[difficulty].end())
    {
        return &itr->second;
    }
    else
    {
        return NULL;
    }
}

/**
 * @brief Removes the player's instance bind for a map.
 *
 * @param mapid The bound map identifier.
 * @param unload True when called during load or unload paths that should skip DB deletion.
 */
void Player::UnbindInstance(uint32 mapid, Difficulty difficulty, bool unload)
{
    BoundInstancesMap::iterator itr = m_boundInstances[difficulty].find(mapid);
    UnbindInstance(itr, difficulty, unload);
}

/**
 * @brief Removes the instance bind referenced by an iterator.
 *
 * @param itr Iterator pointing at the bind to remove.
 * @param unload True when called during load or unload paths that should skip DB deletion.
 */
void Player::UnbindInstance(BoundInstancesMap::iterator& itr, Difficulty difficulty, bool unload)
{
    if (itr != m_boundInstances[difficulty].end())
    {
        if (!unload)
            CharacterDatabase.PExecute("DELETE FROM `character_instance` WHERE `guid` = '%u' AND `instance` = '%u'",
                                       GetGUIDLow(), itr->second.state->GetInstanceId());
        itr->second.state->RemovePlayer(this);              // state can become invalid
        m_boundInstances[difficulty].erase(itr++);
    }
}

/**
 * @brief Binds the player to a dungeon persistent state.
 *
 * @param state The persistent instance state to bind.
 * @param permanent True for a permanent lock, false for a temporary one.
 * @param load True when rebuilding the bind from saved data.
 * @return The resulting bind entry, or NULL if the state was invalid.
 */
InstancePlayerBind* Player::BindToInstance(DungeonPersistentState* state, bool permanent, bool load)
{
    if (state)
    {
        InstancePlayerBind& bind = m_boundInstances[state->GetDifficulty()][state->GetMapId()];
        if (bind.state)
        {
            // update the state when the group kills a boss
            if (permanent != bind.perm || state != bind.state)
                if (!load)
                    CharacterDatabase.PExecute("UPDATE `character_instance` SET `instance` = '%u', `permanent` = '%u' WHERE `guid` = '%u' AND `instance` = '%u'",
                                               state->GetInstanceId(), permanent, GetGUIDLow(), bind.state->GetInstanceId());
        }
        else
        {
            if (!load)
                CharacterDatabase.PExecute("INSERT INTO `character_instance` (`guid`, `instance`, `permanent`) VALUES ('%u', '%u', '%u')",
                                           GetGUIDLow(), state->GetInstanceId(), permanent);
        }

        if (bind.state != state)
        {
            if (bind.state)
            {
                bind.state->RemovePlayer(this);
            }
            state->AddPlayer(this);
        }

        if (permanent)
        {
            state->SetCanReset(false);
        }

        bind.state = state;
        bind.perm = permanent;
        if (!load)
            DEBUG_LOG("Player::BindToInstance: %s(%d) is now bound to map %d, instance %d, difficulty %d",
                      GetName(), GetGUIDLow(), state->GetMapId(), state->GetInstanceId(), state->GetDifficulty());
        // Used by Eluna
#ifdef ENABLE_ELUNA
        if (Eluna* e = GetEluna())
        {
            e->OnBindToInstance(this, (Difficulty)0, state->GetMapId(), permanent);
        }
#endif /* ENABLE_ELUNA */

        return &bind;
    }
    else
    {
        return NULL;
    }
}

/**
 * @brief Resolves the instance save that applies to the player or the player's group.
 *
 * @param mapid The map identifier to inspect.
 * @return The applicable persistent state, or NULL if none exists.
 */
DungeonPersistentState* Player::GetBoundInstanceSaveForSelfOrGroup(uint32 mapid)
{
    MapEntry const* mapEntry = sMapStore.LookupEntry(mapid);
    if (!mapEntry)
    {
        return NULL;
    }

    InstancePlayerBind* pBind = GetBoundInstance(mapid, GetDifficulty());
    DungeonPersistentState* state = pBind ? pBind->state : NULL;

    // the player's permanent player bind is taken into consideration first
    // then the player's group bind and finally the solo bind.
    if (!pBind || !pBind->perm)
    {
        InstanceGroupBind* groupBind = NULL;
        // use the player's difficulty setting (it may not be the same as the group's)
        if (Group* group = GetGroup())
            if ((groupBind = group->GetBoundInstance(mapid, this)))
            {
                state = groupBind->state;
            }
    }

    return state;
}

/**
 * @brief Sends the player's permanent raid instance lock information to the client.
 */
void Player::SendRaidInfo()
{
    uint32 counter = 0;

    WorldPacket data(SMSG_RAID_INSTANCE_INFO, 4);

    size_t p_counter = data.wpos();
    data << uint32(counter);                                // placeholder

    time_t now = time(NULL);

    for (int i = 0; i < MAX_DIFFICULTY; ++i)
    {
        for (BoundInstancesMap::const_iterator itr = m_boundInstances[i].begin(); itr != m_boundInstances[i].end(); ++itr)
        {
            if (itr->second.perm)
            {
                DungeonPersistentState* state = itr->second.state;
                data << uint32(state->GetMapId());          // map id
                data << uint32(state->GetResetTime() - time(NULL));
                data << uint32(state->GetInstanceId());     // instance id
                data << uint32(counter);
                counter++;
            }
        }
    }
    data.put<uint32>(p_counter, counter);
    GetSession()->SendPacket(&data);
}

/*
- called on every successful teleportation to a map
*/
void Player::SendSavedInstances()
{
    bool hasBeenSaved = false;
    WorldPacket data;

    for (uint8 i = 0; i < MAX_DIFFICULTY; ++i)
    {
        for (BoundInstancesMap::const_iterator itr = m_boundInstances[i].begin(); itr != m_boundInstances[i].end(); ++itr)
        {
            if (itr->second.perm)                           // only permanent binds are sent
            {
                hasBeenSaved = true;
                break;
            }
        }
    }

    // Send opcode 811. true or false means, whether you have current raid/heroic instances
    data.Initialize(SMSG_UPDATE_INSTANCE_OWNERSHIP);
    data << uint32(hasBeenSaved);
    GetSession()->SendPacket(&data);

    if (!hasBeenSaved)
    {
        return;
    }

    for (uint8 i = 0; i < MAX_DIFFICULTY; ++i)
    {
        for (BoundInstancesMap::const_iterator itr = m_boundInstances[i].begin(); itr != m_boundInstances[i].end(); ++itr)
        {
            if (itr->second.perm)
            {
                data.Initialize(SMSG_UPDATE_LAST_INSTANCE);
                data << uint32(itr->second.state->GetMapId());
                GetSession()->SendPacket(&data);
            }
        }
    }
}

/// convert the player's binds to the group
void Player::ConvertInstancesToGroup(Player* player, Group* group, ObjectGuid player_guid)
{
    bool has_binds = false;
    bool has_solo = false;

    if (player)
    {
        player_guid = player->GetObjectGuid();
        if (!group)
        {
            group = player->GetGroup();
        }
    }

    MANGOS_ASSERT(player_guid);

    // copy all binds to the group, when changing leader it's assumed the character
    // will not have any solo binds

    if (player)
    {
        for (uint8 i = 0; i < MAX_DIFFICULTY; ++i)
        {
            for (BoundInstancesMap::iterator itr = player->m_boundInstances[i].begin(); itr != player->m_boundInstances[i].end();)
            {
                has_binds = true;

                if (group)
                {
                    group->BindToInstance(itr->second.state, itr->second.perm, true);
                }

                // permanent binds are not removed
                if (!itr->second.perm)
                {
                    // increments itr in call
                    player->UnbindInstance(itr, Difficulty(i), true);
                    has_solo = true;
                }
                else
                {
                    ++itr;
                }
            }
        }
    }

    uint32 player_lowguid = player_guid.GetCounter();

    // if the player's not online we don't know what binds it has
    if (!player || !group || has_binds)
    {
        CharacterDatabase.PExecute("INSERT INTO `group_instance` SELECT `guid`, `instance`, `permanent` FROM `character_instance` WHERE `guid` = '%u'", player_lowguid);
    }

    // the following should not get executed when changing leaders
    if (!player || has_solo)
    {
        CharacterDatabase.PExecute("DELETE FROM `character_instance` WHERE `guid` = '%u' AND `permanent` = 0", player_lowguid);
    }
}

/**
 * @brief Loads and validates the player's home bind location.
 *
 * @param result The query result containing home bind data.
 * @return True if a valid home bind was loaded or defaulted successfully; otherwise, false.
 */
bool Player::_LoadHomeBind(QueryResult* result)
{
    PlayerInfo const* info = sObjectMgr.GetPlayerInfo(getRace(), getClass());
    if (!info)
    {
        sLog.outError("Player have incorrect race/class pair. Can't be loaded.");
        return false;
    }

    bool ok = false;
    // QueryResult *result = CharacterDatabase.PQuery("SELECT `map`,`zone`,`position_x`,`position_y`,`position_z` FROM `character_homebind` WHERE `guid` = '%u'", GUID_LOPART(playerGuid));
    if (result)
    {
        Field* fields = result->Fetch();
        m_homebindMapId = fields[0].GetUInt32();
        m_homebindAreaId = fields[1].GetUInt16();
        m_homebindX = fields[2].GetFloat();
        m_homebindY = fields[3].GetFloat();
        m_homebindZ = fields[4].GetFloat();
        delete result;

        MapEntry const* bindMapEntry = sMapStore.LookupEntry(m_homebindMapId);

        // accept saved data only for valid position (and non instanceable), and accessable
        if (MapManager::IsValidMapCoord(m_homebindMapId, m_homebindX, m_homebindY, m_homebindZ) &&
                !bindMapEntry->Instanceable() && GetSession()->Expansion() >= bindMapEntry->Expansion())
        {
            ok = true;
        }
        else
        {
            CharacterDatabase.PExecute("DELETE FROM `character_homebind` WHERE `guid` = '%u'", GetGUIDLow());
        }
    }

    if (!ok)
    {
        m_homebindMapId = info->mapId;
        m_homebindAreaId = info->areaId;
        m_homebindX = info->positionX;
        m_homebindY = info->positionY;
        m_homebindZ = info->positionZ;

        CharacterDatabase.PExecute("INSERT INTO `character_homebind` (`guid`,`map`,`zone`,`position_x`,`position_y`,`position_z`) VALUES ('%u', '%u', '%u', '%f', '%f', '%f')", GetGUIDLow(), m_homebindMapId, (uint32)m_homebindAreaId, m_homebindX, m_homebindY, m_homebindZ);
    }

    DEBUG_LOG("Setting player home position: mapid is: %u, zoneid is %u, X is %f, Y is %f, Z is %f",
              m_homebindMapId, m_homebindAreaId, m_homebindX, m_homebindY, m_homebindZ);

    return true;
}
