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
 * @file PlayerLoadFromDB.cpp
 * @brief Cohesion split of PlayerLoad.cpp.
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

/**
 * @brief Loads the player's persisted state from the database and initializes runtime data.
 *
 * @param guid The player GUID to load.
 * @param holder The query holder containing login query results.
 * @return True if the player was loaded successfully; otherwise, false.
 */
bool Player::LoadFromDB(ObjectGuid guid, SqlQueryHolder* holder)
{
    //       0     1        2     3     4      5       6      7   8      9            10            11
    // SELECT guid, account, name, race, class, gender, level, xp, money, playerBytes, playerBytes2, playerFlags,"
    // 12          13          14          15   16           17        18         19         20         21          22           23                 24
    //"position_x, position_y, position_z, map, orientation, taximask, cinematic, totaltime, leveltime, rest_bonus, logout_time, is_logout_resting, resettalents_cost,"
    // 25                 26       27       28       29       30         31           32            33        34    35      36                 37         38
    //"resettalents_time, trans_x, trans_y, trans_z, trans_o, transguid, extra_flags, stable_slots, at_login, zone, online, death_expire_time, taxi_path, dungeon_difficulty,"
    // 39           40                41                42                    43          44          45              46           47              48
    //"arenaPoints, totalHonorPoints, todayHonorPoints, yesterdayHonorPoints, totalKills, todayKills, yesterdayKills, chosenTitle, watchedFaction, drunk,"
    // 49      50      51      52      53      54      55             56              57      58              59         60
    //"health, power1, power2, power3, power4, power5, exploredZones, equipmentCache, ammoId, knownTitles, actionBars, createdDate FROM characters WHERE guid = '%u'", GUID_LOPART(m_guid));
    QueryResult* result = holder->GetResult(PLAYER_LOGIN_QUERY_LOADFROM);

    if (!result)
    {
        sLog.outError("%s not found in table `characters`, can't load. ", guid.GetString().c_str());
        return false;
    }

    Field* fields = result->Fetch();

    uint32 dbAccountId = fields[1].GetUInt32();

    // check if the character's account in the db and the logged in account match.
    // player should be able to load/delete character only with correct account!
    if (dbAccountId != GetSession()->GetAccountId())
    {
        sLog.outError("%s loading from wrong account (is: %u, should be: %u)",
                      guid.GetString().c_str(), GetSession()->GetAccountId(), dbAccountId);
        delete result;
        return false;
    }

    Object::_Create(guid.GetCounter(), 0, HIGHGUID_PLAYER);

    m_name = fields[2].GetCppString();

    // check name limitations
    if (ObjectMgr::CheckPlayerName(m_name) != CHAR_NAME_SUCCESS ||
        (GetSession()->GetSecurity() == SEC_PLAYER && sObjectMgr.IsReservedName(m_name)))
    {
        delete result;
        CharacterDatabase.PExecute("UPDATE `characters` SET `at_login` = `at_login` | '%u' WHERE `guid` ='%u'",
                                   uint32(AT_LOGIN_RENAME), guid.GetCounter());
        return false;
    }

    // overwrite possible wrong/corrupted guid
    SetGuidValue(OBJECT_FIELD_GUID, guid);

    // overwrite some data fields
    SetByteValue(UNIT_FIELD_BYTES_0, 0, fields[3].GetUInt8()); // race
    SetByteValue(UNIT_FIELD_BYTES_0, 1, fields[4].GetUInt8()); // class

    uint8 gender = fields[5].GetUInt8() & 0x01;             // allowed only 1 bit values male/female cases (for fit drunk gender part)
    SetByteValue(UNIT_FIELD_BYTES_0, 2, gender);            // gender

    SetByteValue(UNIT_FIELD_BYTES_2, 1, UNIT_BYTE2_FLAG_UNK3 | UNIT_BYTE2_FLAG_UNK5);

    // check if race/class combination is valid
    PlayerInfo const* info = sObjectMgr.GetPlayerInfo(getRace(), getClass());
    if (!info)
    {
        DEBUG_FILTER_LOG(LOG_FILTER_PLAYER_STATS, "Player (GUID: %u) has wrong race/class (%u/%u), can't be loaded.",
                         guid.GetCounter(), getRace(), getClass());
        return false;
    }

    SetUInt32Value(UNIT_FIELD_LEVEL, fields[6].GetUInt8());
    SetUInt32Value(PLAYER_XP, fields[7].GetUInt32());

    _LoadIntoDataField(fields[55].GetString(), PLAYER_EXPLORED_ZONES_1, PLAYER_EXPLORED_ZONES_SIZE);
    _LoadIntoDataField(fields[58].GetString(), PLAYER__FIELD_KNOWN_TITLES, 2);

    InitDisplayIds();                                       // model, scale and model data

    uint32 money = fields[8].GetUInt32();
    if (money > MAX_MONEY_AMOUNT)
    {
        money = MAX_MONEY_AMOUNT;
    }
    SetMoney(money);

    SetUInt32Value(PLAYER_BYTES, fields[9].GetUInt32());
    SetUInt32Value(PLAYER_BYTES_2, fields[10].GetUInt32());

    m_drunk = fields[48].GetUInt16();

    SetUInt16Value(PLAYER_BYTES_3, 0, (m_drunk & 0xFFFE) | gender);

    SetUInt32Value(PLAYER_FLAGS, fields[11].GetUInt32());
    SetInt32Value(PLAYER_FIELD_WATCHED_FACTION_INDEX, fields[47].GetInt32());

    SetUInt32Value(PLAYER_AMMO_ID, fields[57].GetUInt32());

    // Action bars state
    SetByteValue(PLAYER_FIELD_BYTES, 2, fields[59].GetUInt8());

    // cleanup inventory related item value fields (its will be filled correctly in _LoadInventory)
    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
    {
        SetGuidValue(PLAYER_FIELD_INV_SLOT_HEAD + (slot * 2), ObjectGuid());
        SetVisibleItemSlot(slot, NULL);

        delete m_items[slot];
        m_items[slot] = NULL;
    }

    DEBUG_FILTER_LOG(LOG_FILTER_PLAYER_STATS, "Load Basic value of player %s is: ", m_name.c_str());
    outDebugStatsValues();

    // Need to call it to initialize m_team (m_team can be calculated from race)
    // Other way is to saves m_team into characters table.
    setFactionForRace(getRace());
    SetCharm(NULL);

    // load home bind and check in same time class/race pair, it used later for restore broken positions
    if (!_LoadHomeBind(holder->GetResult(PLAYER_LOGIN_QUERY_LOADHOMEBIND)))
    {
        return false;
    }

    InitPrimaryProfessions();                               // to max set before any spell loaded

    // init saved position, and fix it later if problematic
    uint32 transGUID = fields[30].GetUInt32();
    Relocate(fields[12].GetFloat(), fields[13].GetFloat(), fields[14].GetFloat(), fields[16].GetFloat());
    SetLocationMapId(fields[15].GetUInt32());

    uint32 difficulty = fields[38].GetUInt32();
    if (difficulty >= MAX_DIFFICULTY || getLevel() < LEVELREQUIREMENT_HEROIC)
    {
        difficulty = DUNGEON_DIFFICULTY_NORMAL;
    }
    SetDifficulty(Difficulty(difficulty));                  // may be changed in _LoadGroup

    _LoadGroup(holder->GetResult(PLAYER_LOGIN_QUERY_LOADGROUP));

    _LoadArenaTeamInfo(holder->GetResult(PLAYER_LOGIN_QUERY_LOADARENAINFO));

    SetArenaPoints(fields[39].GetUInt32());

    // check arena teams integrity
    for (uint32 arena_slot = 0; arena_slot < MAX_ARENA_SLOT; ++arena_slot)
    {
        uint32 arena_team_id = GetArenaTeamId(arena_slot);
        if (!arena_team_id)
        {
            continue;
        }

        if (ArenaTeam* at = sObjectMgr.GetArenaTeamById(arena_team_id))
            if (at->HaveMember(GetObjectGuid()))
            {
                continue;
            }

        // arena team not exist or not member, cleanup fields
        for (int j = 0; j < ARENA_TEAM_END; ++j)
        {
            SetArenaTeamInfoField(arena_slot, ArenaTeamInfoType(j), 0);
        }
    }

    SetHonorPoints(fields[40].GetUInt32());

    SetUInt32Value(PLAYER_FIELD_TODAY_CONTRIBUTION, fields[41].GetUInt32());
    SetUInt32Value(PLAYER_FIELD_YESTERDAY_CONTRIBUTION, fields[42].GetUInt32());
    SetUInt32Value(PLAYER_FIELD_LIFETIME_HONORABLE_KILLS, fields[43].GetUInt32());
    SetUInt16Value(PLAYER_FIELD_KILLS, 0, fields[44].GetUInt16());
    SetUInt16Value(PLAYER_FIELD_KILLS, 1, fields[45].GetUInt16());

    _LoadBoundInstances(holder->GetResult(PLAYER_LOGIN_QUERY_LOADBOUNDINSTANCES));

    if (!IsPositionValid())
    {
        sLog.outError("%s have invalid coordinates (X: %f Y: %f Z: %f O: %f). Teleport to default race/class locations.",
                      guid.GetString().c_str(), GetPositionX(), GetPositionY(), GetPositionZ(), GetOrientation());
        RelocateToHomebind();

        transGUID = 0;

        m_movementInfo.ClearTransportData();
    }

    _LoadBGData(holder->GetResult(PLAYER_LOGIN_QUERY_LOADBGDATA));

    if (m_bgData.bgInstanceID)                              // saved in BattleGround
    {
        BattleGround* currentBg = sBattleGroundMgr.GetBattleGround(m_bgData.bgInstanceID, BATTLEGROUND_TYPE_NONE);

        bool player_at_bg = currentBg && currentBg->IsPlayerInBattleGround(GetObjectGuid());

        if (player_at_bg && currentBg->GetStatus() != STATUS_WAIT_LEAVE)
        {
            BattleGroundQueueTypeId bgQueueTypeId = sBattleGroundMgr.BGQueueTypeId(currentBg->GetTypeID(), currentBg->GetArenaType());
            AddBattleGroundQueueId(bgQueueTypeId);

            m_bgData.bgTypeID = currentBg->GetTypeID();     // bg data not marked as modified

            // join player to battleground group
            currentBg->EventPlayerLoggedIn(this);
            currentBg->AddOrSetPlayerToCorrectBgGroup(this, GetObjectGuid(), m_bgData.bgTeam);

            SetInviteForBattleGroundQueueType(bgQueueTypeId, currentBg->GetInstanceID());
        }
        else
        {
            // leave bg
            if (player_at_bg)
            {
                currentBg->RemovePlayerAtLeave(GetObjectGuid(), false, true);
            }

            // move to bg enter point
            const WorldLocation& _loc = GetBattleGroundEntryPoint();
            SetLocationMapId(_loc.mapid);
            Relocate(_loc.coord_x, _loc.coord_y, _loc.coord_z, _loc.orientation);

            // We are not in BG anymore
            SetBattleGroundId(0, BATTLEGROUND_TYPE_NONE);
            // remove outdated DB data in DB
            _SaveBGData();
        }
    }
    else
    {
        MapEntry const* mapEntry = sMapStore.LookupEntry(GetMapId());
        // if server restart after player save in BG or area
        // player can have current coordinates in to BG/Arena map, fix this
        if (!mapEntry || mapEntry->IsBattleGroundOrArena())
        {
            const WorldLocation& _loc = GetBattleGroundEntryPoint();
            SetLocationMapId(_loc.mapid);
            Relocate(_loc.coord_x, _loc.coord_y, _loc.coord_z, _loc.orientation);

            // We are not in BG anymore
            SetBattleGroundId(0, BATTLEGROUND_TYPE_NONE);
            // remove outdated DB data in DB
            _SaveBGData();
        }
    }

    if (transGUID != 0)
    {
        m_movementInfo.SetTransportData(ObjectGuid(HIGHGUID_MO_TRANSPORT, transGUID), fields[26].GetFloat(), fields[27].GetFloat(), fields[28].GetFloat(), fields[29].GetFloat(), 0);

        Position const* transportPosition = m_movementInfo.GetTransportPos();

        if (!MaNGOS::IsValidMapCoord(
                    GetPositionX() + transportPosition->x, GetPositionY() + transportPosition->y,
                    GetPositionZ() + transportPosition->z, GetOrientation() + transportPosition->o) ||
            // transport size limited
                transportPosition->x > 50 || transportPosition->y > 50 || transportPosition->z > 50)
        {
            sLog.outError("%s have invalid transport coordinates (X: %f Y: %f Z: %f O: %f). Teleport to default race/class locations.",
                          guid.GetString().c_str(), GetPositionX() + transportPosition->x, GetPositionY() + transportPosition->y,
                          GetPositionZ() + transportPosition->z, GetOrientation() + transportPosition->o);

            RelocateToHomebind();

            m_movementInfo.ClearTransportData();

            transGUID = 0;
        }
    }

    if (transGUID != 0)
    {
        for (MapManager::TransportSet::const_iterator iter = sMapMgr.m_Transports.begin(); iter != sMapMgr.m_Transports.end(); ++iter)
        {
            if ((*iter)->GetGUIDLow() == transGUID)
            {
                MapEntry const* transMapEntry = sMapStore.LookupEntry((*iter)->GetMapId());
                // client without expansion support
                if (GetSession()->Expansion() < transMapEntry->Expansion())
                {
                    DEBUG_LOG("Player %s using client without required expansion tried login at transport at non accessible map %u", GetName(), (*iter)->GetMapId());
                    break;
                }

                m_transport = *iter;
                m_transport->AddPassenger(this);
                SetLocationMapId(m_transport->GetMapId());
                break;
            }
        }

        if (!m_transport)
        {
            sLog.outError("%s have problems with transport guid (%u). Teleport to default race/class locations.",
                          guid.GetString().c_str(), transGUID);

            RelocateToHomebind();

            m_movementInfo.ClearTransportData();

            transGUID = 0;
        }
    }
    else                                                    // not transport case
    {
        MapEntry const* mapEntry = sMapStore.LookupEntry(GetMapId());
        // client without expansion support
        if (GetSession()->Expansion() < mapEntry->Expansion())
        {
            DEBUG_LOG("Player %s using client without required expansion tried login at non accessible map %u", GetName(), GetMapId());
            RelocateToHomebind();
        }
    }

    // player bounded instance saves loaded in _LoadBoundInstances, group versions at group loading
    DungeonPersistentState* state = GetBoundInstanceSaveForSelfOrGroup(GetMapId());

    // load the player's map here if it's not already loaded
    SetMap(sMapMgr.CreateMap(GetMapId(), this));

    // if the player is in an instance and it has been reset in the meantime teleport him to the entrance
    if (GetInstanceId() && !state)
    {
        AreaTrigger const* at = sObjectMgr.GetMapEntranceTrigger(GetMapId());
        if (at)
        {
            Relocate(at->target_X, at->target_Y, at->target_Z, at->target_Orientation);
        }
        else
        {
            sLog.outError("Player %s(GUID: %u) logged in to a reset instance (map: %u) and there is no area-trigger leading to this map. Thus he can't be ported back to the entrance. This _might_ be an exploit attempt.", GetName(), GetGUIDLow(), GetMapId());
        }
    }

    SaveRecallPosition();

    time_t now = time(NULL);
    time_t logoutTime = time_t(fields[22].GetUInt64());

    // since last logout (in seconds)
    uint32 time_diff = uint32(now - logoutTime);

    // set value, including drunk invisibility detection
    // calculate sobering. after 15 minutes logged out, the player will be sober again
    float soberFactor;
    if (time_diff > 15 * MINUTE)
    {
        soberFactor = 0;
    }
    else
    {
        soberFactor = 1 - time_diff / (15.0f * MINUTE);
    }
    uint16 newDrunkenValue = uint16(soberFactor * m_drunk);
    SetDrunkValue(newDrunkenValue);

    m_cinematic = fields[18].GetUInt32();
    m_Played_time[PLAYED_TIME_TOTAL] = fields[19].GetUInt32();
    m_Played_time[PLAYED_TIME_LEVEL] = fields[20].GetUInt32();

    m_resetTalentsCost = fields[24].GetUInt32();
    m_resetTalentsTime = time_t(fields[25].GetUInt64());

    // reserve some flags
    uint32 old_safe_flags = GetUInt32Value(PLAYER_FLAGS) & (PLAYER_FLAGS_HIDE_CLOAK | PLAYER_FLAGS_HIDE_HELM);

    if (HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GM))
    {
        SetUInt32Value(PLAYER_FLAGS, 0 | old_safe_flags);
    }

    m_taxi.LoadTaxiMask(fields[17].GetString());            // must be before InitTaxiNodesForLevel

    uint32 extraflags = fields[31].GetUInt32();

    m_stableSlots = fields[32].GetUInt32();
    if (m_stableSlots > MAX_PET_STABLES)
    {
        sLog.outError("Player can have not more %u stable slots, but have in DB %u", MAX_PET_STABLES, uint32(m_stableSlots));
        m_stableSlots = MAX_PET_STABLES;
    }

    m_atLoginFlags = fields[33].GetUInt32();

    // Honor system
    // Update Honor kills data
    m_lastHonorUpdateTime = logoutTime;
    UpdateHonorFields();

    m_deathExpireTime = (time_t)fields[36].GetUInt64();
    if (m_deathExpireTime > now + MAX_DEATH_COUNT * DEATH_EXPIRE_STEP)
    {
        m_deathExpireTime = now + MAX_DEATH_COUNT * DEATH_EXPIRE_STEP - 1;
    }

    std::string taxi_nodes = fields[37].GetCppString();

    // clear channel spell data (if saved at channel spell casting)
    SetChannelObjectGuid(ObjectGuid());
    SetUInt32Value(UNIT_CHANNEL_SPELL, 0);

    // clear charm/summon related fields
    SetCharm(NULL);
    SetPet(NULL);
    SetTargetGuid(ObjectGuid());
    SetCharmerGuid(ObjectGuid());
    SetOwnerGuid(ObjectGuid());
    SetCreatorGuid(ObjectGuid());

    // reset some aura modifiers before aura apply

    SetGuidValue(PLAYER_FARSIGHT, ObjectGuid());
    SetUInt32Value(PLAYER_TRACK_CREATURES, 0);
    SetUInt32Value(PLAYER_TRACK_RESOURCES, 0);

    // cleanup aura list explicitly before skill load where some spells can be applied
    RemoveAllAuras();

    // make sure the unit is considered out of combat for proper loading
    ClearInCombat();

    // make sure the unit is considered not in duel for proper loading
    SetGuidValue(PLAYER_DUEL_ARBITER, ObjectGuid());
    SetUInt32Value(PLAYER_DUEL_TEAM, 0);

    // reset stats before loading any modifiers
    InitStatsForLevel();
    InitTaxiNodesForLevel();

    // rest bonus can only be calculated after InitStatsForLevel()
    m_rest_bonus = fields[21].GetFloat();

    if (time_diff > 0)
    {
        SetRestBonus(GetRestBonus() + ComputeRest(time_diff, true, (fields[23].GetInt32() > 0)));
    }

    // load skills after InitStatsForLevel because it triggering aura apply also
    _LoadSkills(holder->GetResult(PLAYER_LOGIN_QUERY_LOADSKILLS));

    // apply original stats mods before spell loading or item equipment that call before equip _RemoveStatsMods()

    // Mail
    _LoadMails(holder->GetResult(PLAYER_LOGIN_QUERY_LOADMAILS));
    _LoadMailedItems(holder->GetResult(PLAYER_LOGIN_QUERY_LOADMAILEDITEMS));
    UpdateNextMailTimeAndUnreads();

    _LoadAuras(holder->GetResult(PLAYER_LOGIN_QUERY_LOADAURAS), time_diff);

    // add ghost flag (must be after aura load: PLAYER_FLAGS_GHOST set in aura)
    if (HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST))
    {
        m_deathState = DEAD;
    }

    _LoadSpells(holder->GetResult(PLAYER_LOGIN_QUERY_LOADSPELLS));

    // after spell load
    InitTalentForLevel();
    learnDefaultSpells();

    // after spell load, learn rewarded spell if need also
    _LoadQuestStatus(holder->GetResult(PLAYER_LOGIN_QUERY_LOADQUESTSTATUS));
    _LoadDailyQuestStatus(holder->GetResult(PLAYER_LOGIN_QUERY_LOADDAILYQUESTSTATUS));

    // must be before inventory (some items required reputation check)
    m_reputationMgr.LoadFromDB(holder->GetResult(PLAYER_LOGIN_QUERY_LOADREPUTATION));

    _LoadInventory(holder->GetResult(PLAYER_LOGIN_QUERY_LOADINVENTORY), time_diff);
    _LoadItemLoot(holder->GetResult(PLAYER_LOGIN_QUERY_LOADITEMLOOT));

    // update items with duration and realtime
    UpdateItemDuration(time_diff, true);

    _LoadActions(holder->GetResult(PLAYER_LOGIN_QUERY_LOADACTIONS));

    m_social = sSocialMgr.LoadFromDB(holder->GetResult(PLAYER_LOGIN_QUERY_LOADSOCIALLIST), GetObjectGuid());

    // check PLAYER_CHOSEN_TITLE compatibility with PLAYER__FIELD_KNOWN_TITLES
    // note: PLAYER__FIELD_KNOWN_TITLES updated at quest status loaded
    uint32 curTitle = fields[46].GetUInt32();
    if (curTitle && !HasTitle(curTitle))
    {
        curTitle = 0;
    }

    SetUInt32Value(PLAYER_CHOSEN_TITLE, curTitle);

    if (!m_taxi.LoadTaxiDestinationsFromString(taxi_nodes, GetTeam()))
    {
        // problems with taxi path loading
        TaxiNodesEntry const* nodeEntry = NULL;
        if (uint32 node_id = m_taxi.GetTaxiSource())
        {
            nodeEntry = sTaxiNodesStore.LookupEntry(node_id);
        }

        if (!nodeEntry)                                     // don't know taxi start node, to homebind
        {
            sLog.outError("Character %u have wrong data in taxi destination list, teleport to homebind.", GetGUIDLow());
            RelocateToHomebind();
        }
        else                                                // have start node, to it
        {
            sLog.outError("Character %u have too short taxi destination list, teleport to original node.", GetGUIDLow());
            SetLocationMapId(nodeEntry->map_id);
            Relocate(nodeEntry->x, nodeEntry->y, nodeEntry->z, 0.0f);
        }

        // we can be relocated from taxi and still have an outdated Map pointer!
        // so we need to get a new Map pointer!
        SetMap(sMapMgr.CreateMap(GetMapId(), this));
        SaveRecallPosition();                           // save as recall also to prevent recall and fall from sky

        m_taxi.ClearTaxiDestinations();
    }

    if (uint32 node_id = m_taxi.GetTaxiSource())
    {
        // save source node as recall coord to prevent recall and fall from sky
        TaxiNodesEntry const* nodeEntry = sTaxiNodesStore.LookupEntry(node_id);
        MANGOS_ASSERT(nodeEntry);                           // checked in m_taxi.LoadTaxiDestinationsFromString
        m_recallMap = nodeEntry->map_id;
        m_recallX = nodeEntry->x;
        m_recallY = nodeEntry->y;
        m_recallZ = nodeEntry->z;

        // flight will started later
    }

    // has to be called after last Relocate() in Player::LoadFromDB
    SetFallInformation(0, GetPositionZ());

    _LoadSpellCooldowns(holder->GetResult(PLAYER_LOGIN_QUERY_LOADSPELLCOOLDOWNS));

    // Spell code allow apply any auras to dead character in load time in aura/spell/item loading
    // Do now before stats re-calculation cleanup for ghost state unexpected auras
    if (!IsAlive())
    {
        RemoveAllAurasOnDeath();
    }

    // apply all stat bonuses from items and auras
    SetCanModifyStats(true);
    UpdateAllStats();

    // restore remembered power/health values (but not more max values)
    uint32 savedhealth = fields[49].GetUInt32();
    SetHealth(savedhealth > GetMaxHealth() ? GetMaxHealth() : savedhealth);
    for (uint32 i = 0; i < MAX_POWERS; ++i)
    {
        uint32 savedpower = fields[50+i].GetUInt32();
        SetPower(Powers(i), savedpower > GetMaxPower(Powers(i)) ? GetMaxPower(Powers(i)) : savedpower);
    }

    uint32 createdDate = fields[60].GetUInt32();
    SetCreatedDate(createdDate);

    DEBUG_FILTER_LOG(LOG_FILTER_PLAYER_STATS, "The value of player %s after load item and aura is: ", m_name.c_str());
    outDebugStatsValues();

    // all fields read
    delete result;

    // GM state
    if (GetSession()->GetSecurity() > SEC_PLAYER)
    {
        switch (sWorld.getConfig(CONFIG_UINT32_GM_LOGIN_STATE))
        {
            default:
            case 0:                      break;             // disable
            case 1: SetGameMaster(true); break;             // enable
            case 2:                                         // save state
                if (extraflags & PLAYER_EXTRA_GM_ON)
                {
                    SetGameMaster(true);
                }
                break;
        }

        switch (sWorld.getConfig(CONFIG_UINT32_GM_VISIBLE_STATE))
        {
            default:
            case 0: SetGMVisible(false); break;             // invisible
            case 1:                      break;             // visible
            case 2:                                         // save state
                if (extraflags & PLAYER_EXTRA_GM_INVISIBLE)
                {
                    SetGMVisible(false);
                }
                break;
        }

        switch (sWorld.getConfig(CONFIG_UINT32_GM_ACCEPT_TICKETS))
        {
            default:
            case 0:                        break;           // disable
            case 1: SetAcceptTicket(true); break;           // enable
            case 2:                                         // save state
                if (extraflags & PLAYER_EXTRA_GM_ACCEPT_TICKETS)
                {
                    SetAcceptTicket(true);
                }
                break;
        }

        switch (sWorld.getConfig(CONFIG_UINT32_GM_CHAT))
        {
            default:
            case 0:                  break;                 // disable
            case 1: SetGMChat(true); break;                 // enable
            case 2:                                         // save state
                if (extraflags & PLAYER_EXTRA_GM_CHAT)
                {
                    SetGMChat(true);
                }
                break;
        }

        switch (sWorld.getConfig(CONFIG_UINT32_GM_WISPERING_TO))
        {
            default:
            case 0:                          break;         // disable
            case 1: SetAcceptWhispers(true); break;         // enable
            case 2:                                         // save state
                if (extraflags & PLAYER_EXTRA_ACCEPT_WHISPERS)
                {
                    SetAcceptWhispers(true);
                }
                break;
        }
    }

    _LoadDeclinedNames(holder->GetResult(PLAYER_LOGIN_QUERY_LOADDECLINEDNAMES));

    return true;
}
