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

#include "Common.h"
#include "Database/DatabaseEnv.h"

#include "DBCStores.h"

#include "ObjectMgr.h"
#include "ObjectGuid.h"
#include "Item.h"
#include "Player.h"
#include "TemporarySummon.h"
#include "Totem.h"
#include "Pet.h"
#include "CreatureAI.h"
#include "GameObject.h"
#include "Opcodes.h"
#include "Chat.h"
#include "ObjectAccessor.h"
#include "MapManager.h"
#include "Language.h"
#include "World.h"
#include "GameEventMgr.h"
#include "SpellMgr.h"
#include "MapPersistentStateMgr.h"
#include "AccountMgr.h"
#include "GMTicketMgr.h"
#include "WaypointManager.h"
#include "DBCStores.h"
#include "Util.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "CellImpl.h"
#include "WaypointMovementGenerator.h"
#include "Formulas.h"
#include "G3D/Quat.h"                                       // for turning GO's
#include "TargetedMovementGenerator.h"                      // for HandleNpcUnFollowCommand
#include "MoveMap.h"                                        // for mmap manager
#include "PathFinder.h"                                     // for mmap commands
#include "movement/MoveSplineInit.h"

#include <fstream>
#include <map>
#include <typeinfo>

static uint32 ReputationRankStrIndex[MAX_REPUTATION_RANK] =
{
    LANG_REP_HATED,    LANG_REP_HOSTILE, LANG_REP_UNFRIENDLY, LANG_REP_NEUTRAL,
    LANG_REP_FRIENDLY, LANG_REP_HONORED, LANG_REP_REVERED,    LANG_REP_EXALTED
};

// mute player for some times

void ChatHandler::ShowTriggerTargetListHelper(uint32 id, AreaTrigger const* at, bool subpart /*= false*/)
{
    if (m_session)
    {
        char dist_buf[50];
        if (!subpart)
        {
            float dist = m_session->GetPlayer()->GetDistance2d(at->target_X, at->target_Y);
            snprintf(dist_buf, 50, GetMangosString(LANG_TRIGGER_DIST), dist);
        }
        else
        {
            dist_buf[0] = '\0';
        }

        PSendSysMessage(LANG_TRIGGER_TARGET_LIST_CHAT,
                        subpart ? " -> " : "", id, id, at->target_mapId, at->target_X, at->target_Y, at->target_Z, dist_buf);
    }
    else
        PSendSysMessage(LANG_TRIGGER_TARGET_LIST_CONSOLE,
                        subpart ? " -> " : "", id, at->target_mapId, at->target_X, at->target_Y, at->target_Z);
}

void ChatHandler::ShowTriggerListHelper(AreaTriggerEntry const* atEntry)
{

    char const* tavern = sObjectMgr.IsTavernAreaTrigger(atEntry->id) ? GetMangosString(LANG_TRIGGER_TAVERN) : "";
    char const* quest = sObjectMgr.GetQuestForAreaTrigger(atEntry->id) ? GetMangosString(LANG_TRIGGER_QUEST) : "";

    if (m_session)
    {
        float dist = m_session->GetPlayer()->GetDistance2d(atEntry->x, atEntry->y);
        char dist_buf[50];
        snprintf(dist_buf, 50, GetMangosString(LANG_TRIGGER_DIST), dist);

        PSendSysMessage(LANG_TRIGGER_LIST_CHAT,
                        atEntry->id, atEntry->id, atEntry->mapid, atEntry->x, atEntry->y, atEntry->z, dist_buf, tavern, quest);
    }
    else
        PSendSysMessage(LANG_TRIGGER_LIST_CONSOLE,
                        atEntry->id, atEntry->mapid, atEntry->x, atEntry->y, atEntry->z, tavern, quest);

    if (AreaTrigger const* at = sObjectMgr.GetAreaTrigger(atEntry->id))
    {
        ShowTriggerTargetListHelper(atEntry->id, at, true);
    }
}

bool ChatHandler::HandleTriggerCommand(char* args)
{
    AreaTriggerEntry const* atEntry = NULL;

    Player* pl = m_session ? m_session->GetPlayer() : NULL;

    // select by args
    if (*args)
    {
        uint32 atId;
        if (!ExtractUint32KeyFromLink(&args, "Hareatrigger", atId))
        {
            return false;
        }

        if (!atId)
        {
            return false;
        }

        atEntry = sAreaTriggerStore.LookupEntry(atId);

        if (!atEntry)
        {
            PSendSysMessage(LANG_COMMAND_GOAREATRNOTFOUND, atId);
            SetSentErrorMessage(true);
            return false;
        }
    }
    // find nearest
    else
    {
        if (!m_session)
        {
            return false;
        }

        float dist2 = MAP_SIZE * MAP_SIZE;

        // Search triggers
        for (uint32 id = 0; id < sAreaTriggerStore.GetNumRows(); ++id)
        {
            AreaTriggerEntry const* atTestEntry = sAreaTriggerStore.LookupEntry(id);
            if (!atTestEntry)
            {
                continue;
            }

            if (atTestEntry->mapid != m_session->GetPlayer()->GetMapId())
            {
                continue;
            }

            float dx = atTestEntry->x - pl->GetPositionX();
            float dy = atTestEntry->y - pl->GetPositionY();

            float test_dist2 = dx * dx + dy * dy;

            if (test_dist2 >= dist2)
            {
                continue;
            }

            dist2 = test_dist2;
            atEntry = atTestEntry;
        }

        if (!atEntry)
        {
            SendSysMessage(LANG_COMMAND_NOTRIGGERFOUND);
            SetSentErrorMessage(true);
            return false;
        }
    }

    ShowTriggerListHelper(atEntry);

    int loc_idx = GetSessionDbLocaleIndex();

    AreaTrigger const* at = sObjectMgr.GetAreaTrigger(atEntry->id);
    if (at)
    {
        PSendSysMessage(LANG_TRIGGER_REQ_LEVEL, at->requiredLevel);
    }

    if (uint32 quest_id = sObjectMgr.GetQuestForAreaTrigger(atEntry->id))
    {
        SendSysMessage(LANG_TRIGGER_EXPLORE_QUEST);
        ShowQuestListHelper(quest_id, loc_idx, pl);
    }

    if (at)
    {
        if (at->requiredItem || at->requiredItem2)
        {
            SendSysMessage(LANG_TRIGGER_REQ_ITEMS);

            if (at->requiredItem)
            {
                ShowItemListHelper(at->requiredItem, loc_idx, pl);
            }
            if (at->requiredItem2)
            {
                ShowItemListHelper(at->requiredItem2, loc_idx, pl);
            }
        }

        if (at->requiredQuest)
        {
            SendSysMessage(LANG_TRIGGER_REQ_QUEST);
            ShowQuestListHelper(at->requiredQuest, loc_idx, pl);
        }

        if (at->heroicKey || at->heroicKey2)
        {
            SendSysMessage(LANG_TRIGGER_REQ_KEYS_HEROIC);

            if (at->heroicKey)
            {
                ShowItemListHelper(at->heroicKey, loc_idx, pl);
            }
            if (at->heroicKey2)
            {
                ShowItemListHelper(at->heroicKey2, loc_idx, pl);
            }
        }
    }

    return true;
}

bool ChatHandler::HandleTriggerActiveCommand(char* /*args*/)
{
    uint32 counter = 0;                                     // Counter for figure out that we found smth.

    Player* pl = m_session->GetPlayer();

    // Search in AreaTable.dbc
    for (uint32 id = 0; id < sAreaTriggerStore.GetNumRows(); ++id)
    {
        AreaTriggerEntry const* atEntry = sAreaTriggerStore.LookupEntry(id);
        if (!atEntry)
        {
            continue;
        }

        if (!IsPointInAreaTriggerZone(atEntry, pl->GetMapId(), pl->GetPositionX(), pl->GetPositionY(), pl->GetPositionZ()))
        {
            continue;
        }

        ShowTriggerListHelper(atEntry);

        ++counter;
    }

    if (counter == 0)                                      // if counter == 0 then we found nth
    {
        SendSysMessage(LANG_COMMAND_NOTRIGGERFOUND);
    }

    return true;
}

bool ChatHandler::HandleTriggerNearCommand(char* args)
{
    float distance = (!*args) ? 10.0f : (float)atof(args);
    float dist2 =  distance * distance;
    uint32 counter = 0;                                     // Counter for figure out that we found smth.

    Player* pl = m_session->GetPlayer();

    // Search triggers
    for (uint32 id = 0; id < sAreaTriggerStore.GetNumRows(); ++id)
    {
        AreaTriggerEntry const* atEntry = sAreaTriggerStore.LookupEntry(id);
        if (!atEntry)
        {
            continue;
        }

        if (atEntry->mapid != m_session->GetPlayer()->GetMapId())
        {
            continue;
        }

        float dx = atEntry->x - pl->GetPositionX();
        float dy = atEntry->y - pl->GetPositionY();

        if (dx * dx + dy * dy > dist2)
        {
            continue;
        }

        ShowTriggerListHelper(atEntry);

        ++counter;
    }

    // Search trigger targets
    for (uint32 id = 0; id < sAreaTriggerStore.GetNumRows(); ++id)
    {
        AreaTriggerEntry const* atEntry = sAreaTriggerStore.LookupEntry(id);
        if (!atEntry)
        {
            continue;
        }

        AreaTrigger const* at = sObjectMgr.GetAreaTrigger(atEntry->id);
        if (!at)
        {
            continue;
        }

        if (at->target_mapId != m_session->GetPlayer()->GetMapId())
        {
            continue;
        }

        float dx = at->target_X - pl->GetPositionX();
        float dy = at->target_Y - pl->GetPositionY();

        if (dx * dx + dy * dy > dist2)
        {
            continue;
        }

        ShowTriggerTargetListHelper(atEntry->id, at);

        ++counter;
    }

    if (counter == 0)                                      // if counter == 0 then we found nth
    {
        SendSysMessage(LANG_COMMAND_NOTRIGGERFOUND);
    }

    return true;
}

static char const* const areatriggerKeys[] =
{
    "Hareatrigger",
    "Hareatrigger_target",
    NULL
};

bool ChatHandler::HandleGoTriggerCommand(char* args)
{
    Player* _player = m_session->GetPlayer();

    if (!*args)
    {
        return false;
    }

    char* atIdStr = ExtractKeyFromLink(&args, areatriggerKeys);
    if (!atIdStr)
    {
        return false;
    }

    uint32 atId;
    if (!ExtractUInt32(&atIdStr, atId))
    {
        return false;
    }

    if (!atId)
    {
        return false;
    }

    AreaTriggerEntry const* atEntry = sAreaTriggerStore.LookupEntry(atId);
    if (!atEntry)
    {
        PSendSysMessage(LANG_COMMAND_GOAREATRNOTFOUND, atId);
        SetSentErrorMessage(true);
        return false;
    }

    bool to_target = ExtractLiteralArg(&args, "target");
    if (!to_target && *args)                                // can be fail also at syntax error
    {
        return false;
    }

    if (to_target)
    {
        AreaTrigger const* at = sObjectMgr.GetAreaTrigger(atId);
        if (!at)
        {
            PSendSysMessage(LANG_AREATRIGER_NOT_HAS_TARGET, atId);
            SetSentErrorMessage(true);
            return false;
        }

        return HandleGoHelper(_player, at->target_mapId, at->target_X, at->target_Y, &at->target_Z);
    }
    else
    {
        return HandleGoHelper(_player, atEntry->mapid, atEntry->x, atEntry->y, &atEntry->z);
    }
}

bool ChatHandler::HandleGoGraveyardCommand(char* args)
{
    Player* _player = m_session->GetPlayer();

    uint32 gyId;
    if (!ExtractUInt32(&args, gyId))
    {
        return false;
    }

    WorldSafeLocsEntry const* gy = sWorldSafeLocsStore.LookupEntry(gyId);
    if (!gy)
    {
        PSendSysMessage(LANG_COMMAND_GRAVEYARDNOEXIST, gyId);
        SetSentErrorMessage(true);
        return false;
    }

    return HandleGoHelper(_player, gy->map_id, gy->x, gy->y, &gy->z);
}

enum CreatureLinkType
{
    CREATURE_LINK_RAW               = -1,                   // non-link case
    CREATURE_LINK_GUID              = 0,
    CREATURE_LINK_ENTRY             = 1,
};

static char const* const creatureKeys[] =
{
    "Hcreature",
    "Hcreature_entry",
    NULL
};

/** \brief Teleport the GM to the specified creature
*
* .go creature <GUID>     --> TP using creature.guid
* .go creature azuregos   --> TP player to the mob with this name
*                             Warning: If there is more than one mob with this name
*                                      you will be teleported to the first one that is found.
* .go creature id 6109    --> TP player to the mob, that has this creature_template.entry
*                             Warning: If there is more than one mob with this "id"
*                                      you will be teleported to the first one that is found.
*/
// teleport to creature
bool ChatHandler::HandleGoCreatureCommand(char* args)
{
    if (!*args)
    {
        return false;
    }

    Player* _player = m_session->GetPlayer();

    // "id" or number or [name] Shift-click form |color|Hcreature:creature_id|h[name]|h|r
    int crType;
    char* pParam1 = ExtractKeyFromLink(&args, creatureKeys, &crType);
    if (!pParam1)
    {
        return false;
    }

    // User wants to teleport to the NPC's template entry
    if (crType == CREATURE_LINK_RAW && strcmp(pParam1, "id") == 0)
    {
        // number or [name] Shift-click form |color|Hcreature_entry:creature_id|h[name]|h|r
        pParam1 = ExtractKeyFromLink(&args, "Hcreature_entry");
        if (!pParam1)
        {
            return false;
        }

        crType = CREATURE_LINK_ENTRY;
    }

    CreatureData const* data = NULL;

    switch (crType)
    {
        case CREATURE_LINK_ENTRY:
        {
            uint32 tEntry;
            if (!ExtractUInt32(&pParam1, tEntry))
            {
                return false;
            }

            if (!tEntry)
            {
                return false;
            }

            if (!ObjectMgr::GetCreatureTemplate(tEntry))
            {
                SendSysMessage(LANG_COMMAND_GOCREATNOTFOUND);
                SetSentErrorMessage(true);
                return false;
            }

            FindCreatureData worker(tEntry, m_session ? m_session->GetPlayer() : NULL);

            sObjectMgr.DoCreatureData(worker);

            CreatureDataPair const* dataPair = worker.GetResult();
            if (!dataPair)
            {
                SendSysMessage(LANG_COMMAND_GOCREATNOTFOUND);
                SetSentErrorMessage(true);
                return false;
            }

            data = &dataPair->second;
            break;
        }
        case CREATURE_LINK_GUID:
        {
            uint32 lowguid;
            if (!ExtractUInt32(&pParam1, lowguid))
            {
                return false;
            }

            data = sObjectMgr.GetCreatureData(lowguid);
            if (!data)
            {
                SendSysMessage(LANG_COMMAND_GOCREATNOTFOUND);
                SetSentErrorMessage(true);
                return false;
            }
            break;
        }
        case CREATURE_LINK_RAW:
        {
            uint32 lowguid;
            if (ExtractUInt32(&pParam1, lowguid))
            {
                data = sObjectMgr.GetCreatureData(lowguid);
                if (!data)
                {
                    SendSysMessage(LANG_COMMAND_GOCREATNOTFOUND);
                    SetSentErrorMessage(true);
                    return false;
                }
            }
            // Number is invalid - maybe the user specified the mob's name
            else
            {
                std::string name = pParam1;
                WorldDatabase.escape_string(name);
                QueryResult* result = WorldDatabase.PQuery("SELECT `guid` FROM `creature`, `creature_template` WHERE `creature`.`id` = `creature_template`.`entry` AND `creature_template`.`name` " _LIKE_ " " _CONCAT3_("'%%'", "'%s'", "'%%'"), name.c_str());
                if (!result)
                {
                    SendSysMessage(LANG_COMMAND_GOCREATNOTFOUND);
                    SetSentErrorMessage(true);
                    return false;
                }

                FindCreatureData worker(0, m_session ? m_session->GetPlayer() : NULL);

                do
                {
                    Field* fields = result->Fetch();
                    uint32 guid = fields[0].GetUInt32();

                    CreatureDataPair const* cr_data = sObjectMgr.GetCreatureDataPair(guid);
                    if (!cr_data)
                    {
                        continue;
                    }

                    worker(*cr_data);
                }
                while (result->NextRow());

                delete result;

                CreatureDataPair const* dataPair = worker.GetResult();
                if (!dataPair)
                {
                    SendSysMessage(LANG_COMMAND_GOCREATNOTFOUND);
                    SetSentErrorMessage(true);
                    return false;
                }

                data = &dataPair->second;
            }
            break;
        }
    }

    return HandleGoHelper(_player, data->mapid, data->posX, data->posY, &data->posZ);
}

enum GameobjectLinkType
{
    GAMEOBJECT_LINK_RAW             = -1,                   // non-link case
    GAMEOBJECT_LINK_GUID            = 0,
    GAMEOBJECT_LINK_ENTRY           = 1,
};

static char const* const gameobjectKeys[] =
{
    "Hgameobject",
    "Hgameobject_entry",
    NULL
};

// teleport to gameobject
bool ChatHandler::HandleGoObjectCommand(char* args)
{
    Player* _player = m_session->GetPlayer();

    // number or [name] Shift-click form |color|Hgameobject:go_guid|h[name]|h|r
    int goType;
    char* pParam1 = ExtractKeyFromLink(&args, gameobjectKeys, &goType);
    if (!pParam1)
    {
        return false;
    }

    // User wants to teleport to the GO's template entry
    if (goType == GAMEOBJECT_LINK_RAW && strcmp(pParam1, "id") == 0)
    {
        // number or [name] Shift-click form |color|Hgameobject_entry:creature_id|h[name]|h|r
        pParam1 = ExtractKeyFromLink(&args, "Hgameobject_entry");
        if (!pParam1)
        {
            return false;
        }

        goType = GAMEOBJECT_LINK_ENTRY;
    }

    GameObjectData const* data = NULL;

    switch (goType)
    {
        case CREATURE_LINK_ENTRY:
        {
            uint32 tEntry;
            if (!ExtractUInt32(&pParam1, tEntry))
            {
                return false;
            }

            if (!tEntry)
            {
                return false;
            }

            if (!ObjectMgr::GetGameObjectInfo(tEntry))
            {
                SendSysMessage(LANG_COMMAND_GOOBJNOTFOUND);
                SetSentErrorMessage(true);
                return false;
            }

            FindGOData worker(tEntry, m_session ? m_session->GetPlayer() : NULL);

            sObjectMgr.DoGOData(worker);

            GameObjectDataPair const* dataPair = worker.GetResult();

            if (!dataPair)
            {
                SendSysMessage(LANG_COMMAND_GOOBJNOTFOUND);
                SetSentErrorMessage(true);
                return false;
            }

            data = &dataPair->second;
            break;
        }
        case GAMEOBJECT_LINK_GUID:
        {
            uint32 lowguid;
            if (!ExtractUInt32(&pParam1, lowguid))
            {
                return false;
            }

            // by DB guid
            data = sObjectMgr.GetGOData(lowguid);
            if (!data)
            {
                SendSysMessage(LANG_COMMAND_GOOBJNOTFOUND);
                SetSentErrorMessage(true);
                return false;
            }
            break;
        }
        case GAMEOBJECT_LINK_RAW:
        {
            uint32 lowguid;
            if (ExtractUInt32(&pParam1, lowguid))
            {
                // by DB guid
                data = sObjectMgr.GetGOData(lowguid);
                if (!data)
                {
                    SendSysMessage(LANG_COMMAND_GOOBJNOTFOUND);
                    SetSentErrorMessage(true);
                    return false;
                }
            }
            else
            {
                std::string name = pParam1;
                WorldDatabase.escape_string(name);
                QueryResult* result = WorldDatabase.PQuery("SELECT `guid` FROM `gameobject`, `gameobject_template` WHERE `gameobject`.`id` = `gameobject_template`.`entry` AND `gameobject_template`.`name `" _LIKE_ " " _CONCAT3_("'%%'", "'%s'", "'%%'"), name.c_str());
                if (!result)
                {
                    SendSysMessage(LANG_COMMAND_GOOBJNOTFOUND);
                    SetSentErrorMessage(true);
                    return false;
                }

                FindGOData worker(0, m_session ? m_session->GetPlayer() : NULL);

                do
                {
                    Field* fields = result->Fetch();
                    uint32 guid = fields[0].GetUInt32();

                    GameObjectDataPair const* go_data = sObjectMgr.GetGODataPair(guid);
                    if (!go_data)
                    {
                        continue;
                    }

                    worker(*go_data);
                }
                while (result->NextRow());

                delete result;

                GameObjectDataPair const* dataPair = worker.GetResult();
                if (!dataPair)
                {
                    SendSysMessage(LANG_COMMAND_GOOBJNOTFOUND);
                    SetSentErrorMessage(true);
                    return false;
                }

                data = &dataPair->second;
            }
            break;
        }
    }

    return HandleGoHelper(_player, data->mapid, data->posX, data->posY, &data->posZ);
}


bool ChatHandler::HandleGUIDCommand(char* /*args*/)
{
    ObjectGuid guid = m_session->GetPlayer()->GetSelectionGuid();

    if (!guid)
    {
        SendSysMessage(LANG_NO_SELECTION);
        SetSentErrorMessage(true);
        return false;
    }

    PSendSysMessage(LANG_OBJECT_GUID, guid.GetString().c_str());
    return true;
}

void ChatHandler::ShowFactionListHelper(FactionEntry const* factionEntry, LocaleConstant loc, FactionState const* repState /*= NULL*/, Player* target /*= NULL */)
{
    std::string name = factionEntry->name[loc];
    // send faction in "id - [faction] rank reputation [visible] [at war] [own team] [unknown] [invisible] [inactive]" format
    // or              "id - [faction] [no reputation]" format
    std::ostringstream ss;
    if (m_session)
    {
        ss << factionEntry->ID << " - |cffffffff|Hfaction:" << factionEntry->ID << "|h[" << name << " " << localeNames[loc] << "]|h|r";
    }
    else
    {
        ss << factionEntry->ID << " - " << name << " " << localeNames[loc];
    }

    if (repState)                               // and then target!=NULL also
    {
        ReputationRank rank = target->GetReputationMgr().GetRank(factionEntry);
        std::string rankName = GetMangosString(ReputationRankStrIndex[rank]);

        ss << " " << rankName << "|h|r (" << target->GetReputationMgr().GetReputation(factionEntry) << ")";

        if (repState->Flags & FACTION_FLAG_VISIBLE)
        {
            ss << GetMangosString(LANG_FACTION_VISIBLE);
        }
        if (repState->Flags & FACTION_FLAG_AT_WAR)
        {
            ss << GetMangosString(LANG_FACTION_ATWAR);
        }
        if (repState->Flags & FACTION_FLAG_PEACE_FORCED)
        {
            ss << GetMangosString(LANG_FACTION_PEACE_FORCED);
        }
        if (repState->Flags & FACTION_FLAG_HIDDEN)
        {
            ss << GetMangosString(LANG_FACTION_HIDDEN);
        }
        if (repState->Flags & FACTION_FLAG_INVISIBLE_FORCED)
        {
            ss << GetMangosString(LANG_FACTION_INVISIBLE_FORCED);
        }
        if (repState->Flags & FACTION_FLAG_INACTIVE)
        {
            ss << GetMangosString(LANG_FACTION_INACTIVE);
        }
    }
    else if (target)
    {
        ss << GetMangosString(LANG_FACTION_NOREPUTATION);
    }

    SendSysMessage(ss.str().c_str());
}


bool ChatHandler::HandleModifyRepCommand(char* args)
{
    if (!*args)
    {
        return false;
    }

    Player* target = getSelectedPlayer();

    if (!target)
    {
        SendSysMessage(LANG_PLAYER_NOT_FOUND);
        SetSentErrorMessage(true);
        return false;
    }

    // check online security
    if (HasLowerSecurity(target))
    {
        return false;
    }

    uint32 factionId;
    if (!ExtractUint32KeyFromLink(&args, "Hfaction", factionId))
    {
        return false;
    }

    if (!factionId)
    {
        return false;
    }

    int32 amount = 0;
    if (!ExtractInt32(&args, amount))
    {
        char* rankTxt = ExtractLiteralArg(&args);
        if (!rankTxt)
        {
            return false;
        }

        std::string rankStr = rankTxt;
        std::wstring wrankStr;
        if (!Utf8toWStr(rankStr, wrankStr))
        {
            return false;
        }
        wstrToLower(wrankStr);

        int r = 0;
        amount = -42000;
        for (; r < MAX_REPUTATION_RANK; ++r)
        {
            std::string rank = GetMangosString(ReputationRankStrIndex[r]);
            if (rank.empty())
            {
                continue;
            }

            std::wstring wrank;
            if (!Utf8toWStr(rank, wrank))
            {
                continue;
            }

            wstrToLower(wrank);

            if (wrank.substr(0, wrankStr.size()) == wrankStr)
            {
                int32 delta;
                if (!ExtractOptInt32(&args, delta, 0) || (delta < 0) || (delta > ReputationMgr::PointsInRank[r] - 1))
                {
                    PSendSysMessage(LANG_COMMAND_FACTION_DELTA, (ReputationMgr::PointsInRank[r] - 1));
                    SetSentErrorMessage(true);
                    return false;
                }
                amount += delta;
                break;
            }
            amount += ReputationMgr::PointsInRank[r];
        }
        if (r >= MAX_REPUTATION_RANK)
        {
            PSendSysMessage(LANG_COMMAND_FACTION_INVPARAM, rankTxt);
            SetSentErrorMessage(true);
            return false;
        }
    }

    FactionEntry const* factionEntry = sFactionStore.LookupEntry(factionId);

    if (!factionEntry)
    {
        PSendSysMessage(LANG_COMMAND_FACTION_UNKNOWN, factionId);
        SetSentErrorMessage(true);
        return false;
    }

    if (factionEntry->reputationListID < 0)
    {
        PSendSysMessage(LANG_COMMAND_FACTION_NOREP_ERROR, factionEntry->name[GetSessionDbcLocale()], factionId);
        SetSentErrorMessage(true);
        return false;
    }

    target->GetReputationMgr().SetReputation(factionEntry, amount);
    PSendSysMessage(LANG_COMMAND_MODIFY_REP, factionEntry->name[GetSessionDbcLocale()], factionId,
                    GetNameLink(target).c_str(), target->GetReputationMgr().GetReputation(factionEntry));
    return true;
}



// move item to other slot
bool ChatHandler::HandleItemMoveCommand(char* args)
{
    if (!*args)
    {
        return false;
    }
    uint8 srcslot, dstslot;

    char* pParam1 = strtok(args, " ");
    if (!pParam1)
    {
        return false;
    }

    char* pParam2 = strtok(NULL, " ");
    if (!pParam2)
    {
        return false;
    }

    srcslot = (uint8)atoi(pParam1);
    dstslot = (uint8)atoi(pParam2);

    if (srcslot == dstslot)
    {
        return true;
    }

    Player* player = m_session->GetPlayer();
    if (!player->IsValidPos(INVENTORY_SLOT_BAG_0, srcslot, true))
    {
        return false;
    }

    // can be autostore pos
    if (!player->IsValidPos(INVENTORY_SLOT_BAG_0, dstslot, false))
    {
        return false;
    }

    uint16 src = ((INVENTORY_SLOT_BAG_0 << 8) | srcslot);
    uint16 dst = ((INVENTORY_SLOT_BAG_0 << 8) | dstslot);

    player->SwapItem(src, dst);

    return true;
}

// demorph player or unit
bool ChatHandler::HandleDeMorphCommand(char* /*args*/)
{
    Unit* target = getSelectedUnit();
    if (!target)
    {
        target = m_session->GetPlayer();
    }


    // check online security
    else if (target->GetTypeId() == TYPEID_PLAYER && HasLowerSecurity((Player*)target))
    {
        return false;
    }

    target->DeMorph();

    return true;
}

// morph creature or player
bool ChatHandler::HandleModifyMorphCommand(char* args)
{
    if (!*args)
    {
        return false;
    }

    uint32 display_id = (uint32)atoi(args);

    CreatureDisplayInfoEntry const* displayEntry = sCreatureDisplayInfoStore.LookupEntry(display_id);
    if (!displayEntry)
    {
        SendSysMessage(LANG_BAD_VALUE);
        SetSentErrorMessage(true);
        return false;
    }

    Unit* target = getSelectedUnit();
    if (!target)
    {
        target = m_session->GetPlayer();
    }

    // check online security
    else if (target->GetTypeId() == TYPEID_PLAYER && HasLowerSecurity((Player*)target))
    {
        return false;
    }

    target->SetDisplayId(display_id);

    return true;
}


// show ticket (helper)
void ChatHandler::ShowTicket(GMTicket const* ticket)
{
    std::string lastupdated = TimeToTimestampStr(ticket->GetLastUpdate());

    std::string name;
    if (!sObjectMgr.GetPlayerNameByGUID(ticket->GetPlayerGuid(), name))
    {
        name = GetMangosString(LANG_UNKNOWN);
    }

    std::string nameLink = playerLink(name);

    char const* response = ticket->GetResponse();

    PSendSysMessage(LANG_COMMAND_TICKETVIEW, nameLink.c_str(), lastupdated.c_str(), ticket->GetText());
    if (strlen(response))
    {
        PSendSysMessage(LANG_COMMAND_TICKETRESPONSE, ticket->GetResponse());
    }
}

// ticket commands
bool ChatHandler::HandleTicketAcceptCommand(char* args)
{
    char* px = ExtractLiteralArg(&args);

    // ticket<end>
    if (!px)
    {
        return false;
    }

    // ticket accept on
    if (strncmp(px, "on", 3) == 0)
    {
        sTicketMgr.SetAcceptTickets(true);
        SendSysMessage(LANG_COMMAND_TICKETS_SYSTEM_ON);
    }
    // ticket accept off
    else if (strncmp(px, "off", 4) == 0)
    {
        sTicketMgr.SetAcceptTickets(false);
        SendSysMessage(LANG_COMMAND_TICKETS_SYSTEM_OFF);
    }
    else
    {
        return false;
    }

    return true;
}

bool ChatHandler::HandleTicketCloseCommand(char* args)
{
    GMTicket* ticket = NULL;

    uint32 num;
    if (ExtractUInt32(&args, num))
    {
        if (num == 0)
        {
            return false;
        }

        ticket = sTicketMgr.GetGMTicket(num);

        if (!ticket)
        {
            PSendSysMessage(LANG_COMMAND_TICKETNOTEXIST, num);
            SetSentErrorMessage(true);
            return false;
        }
    }
    else
    {
        ObjectGuid target_guid;
        std::string target_name;
        if (!ExtractPlayerTarget(&args, NULL, &target_guid, &target_name))
        {
            return false;
        }

        // ticket respond $char_name
        ticket = sTicketMgr.GetGMTicket(target_guid);

        if (!ticket)
        {
            PSendSysMessage(LANG_COMMAND_TICKETNOTEXIST_NAME, target_name.c_str());
            SetSentErrorMessage(true);
            return false;
        }
    }

    Player* pPlayer = sObjectMgr.GetPlayer(ticket->GetPlayerGuid());

    if (!pPlayer && !sWorld.getConfig(CONFIG_BOOL_GM_TICKET_OFFLINE_CLOSING))
    {
        SendSysMessage(LANG_COMMAND_TICKET_CANT_CLOSE);
        return false;
    }

    ticket->Close();

    //This logic feels misplaced, but you can't have it in GMTicket?
    sTicketMgr.Delete(ticket->GetPlayerGuid()); // here, ticket become invalidated and should not be used below

    PSendSysMessage(LANG_COMMAND_TICKETCLOSED_NAME, pPlayer ? pPlayer->GetName() : "an offline player");

    return true;
}

// del tickets
bool ChatHandler::HandleTicketDeleteCommand(char* args)
{
    char* px = ExtractLiteralArg(&args);
    if (!px)
    {
        return false;
    }

    // ticket delete all
    if (strncmp(px, "all", 4) == 0)
    {
        sTicketMgr.DeleteAll();
        SendSysMessage(LANG_COMMAND_ALLTICKETDELETED);
        return true;
    }

    uint32 num;

    // ticket delete #num
    if (ExtractUInt32(&px, num))
    {
        if (num == 0)
        {
            return false;
        }

        // mgr numbering tickets start from 0
        GMTicket* ticket = sTicketMgr.GetGMTicket(num);

        if (!ticket)
        {
            PSendSysMessage(LANG_COMMAND_TICKETNOTEXIST, num);
            SetSentErrorMessage(true);
            return false;
        }

        ObjectGuid guid = ticket->GetPlayerGuid();

        sTicketMgr.Delete(guid);

        // notify player
        if (Player* pl = sObjectMgr.GetPlayer(guid))
        {
            pl->GetSession()->SendGMTicketGetTicket(0x0A);
            PSendSysMessage(LANG_COMMAND_TICKETPLAYERDEL, GetNameLink(pl).c_str());
        }
        else
        {
            PSendSysMessage(LANG_COMMAND_TICKETDEL);
        }

        return true;
    }

    // ticket delete $charName
    Player* target;
    ObjectGuid target_guid;
    std::string target_name;
    if (!ExtractPlayerTarget(&px, &target, &target_guid, &target_name))
    {
        return false;
    }

    // ticket delete $charName
    sTicketMgr.Delete(target_guid);

    // notify players about ticket deleting
    if (target)
    {
        target->GetSession()->SendGMTicketGetTicket(0x0A);
    }

    std::string nameLink = playerLink(target_name);

    PSendSysMessage(LANG_COMMAND_TICKETPLAYERDEL, nameLink.c_str());
    return true;
}

bool ChatHandler::HandleTicketInfoCommand(char *args)
{
    size_t count = sTicketMgr.GetTicketCount();

    if (m_session)
    {
        PSendSysMessage(LANG_COMMAND_TICKETCOUNT, count, GetOnOffStr(m_session->GetPlayer()->isAcceptTickets()));
    }
    else
    {
        PSendSysMessage(LANG_COMMAND_TICKETCOUNT_CONSOLE, count);
    }

    return true;
}

bool ChatHandler::HandleTicketListCommand(char* args)
{
    uint16 numToShow = std::min(uint16(sTicketMgr.GetTicketCount()), uint16(sWorld.getConfig(CONFIG_UINT32_GM_TICKET_LIST_SIZE)));
    for (uint16 i = 0; i < numToShow; ++i)
    {
        GMTicket* ticket = sTicketMgr.GetGMTicketByOrderPos(i);
        time_t lastChanged = time_t(ticket->GetLastUpdate());
        PSendSysMessage(LANG_COMMAND_TICKET_OFFLINE_INFO, ticket->GetId(), ticket->GetPlayerGuid().GetCounter(), ticket->HasResponse() ? "+" : "-", ctime(&lastChanged));
    }

    PSendSysMessage(LANG_COMMAND_TICKET_COUNT_ALL, numToShow, sTicketMgr.GetTicketCount());
    return true;
}

bool ChatHandler::HandleTicketOnlineListCommand(char* args)
{
    uint16 count = 0;
    for (uint16 i = 0; i < sTicketMgr.GetTicketCount(); ++i)
    {
        GMTicket* ticket = sTicketMgr.GetGMTicketByOrderPos(i);
        if (Player* player = sObjectMgr.GetPlayer(ticket->GetPlayerGuid(), true))
        {
            ++count;
            if (i < sWorld.getConfig(CONFIG_UINT32_GM_TICKET_LIST_SIZE))
            {
                time_t lastChanged = time_t(ticket->GetLastUpdate());
                PSendSysMessage(LANG_COMMAND_TICKET_BRIEF_INFO, ticket->GetId(), player->GetName(), ticket->HasResponse() ? "+" : "-", ctime(&lastChanged));
            }
        }
    }

    PSendSysMessage(LANG_COMMAND_TICKET_COUNT_ONLINE, std::min(count, uint16(sWorld.getConfig(CONFIG_UINT32_GM_TICKET_LIST_SIZE))), count);
    return true;
}

bool ChatHandler::HandleTicketMeAcceptCommand(char* args)
{
    char* px = ExtractLiteralArg(&args);
    if (!px)
    {
        PSendSysMessage(LANG_COMMAND_TICKET_ACCEPT_STATE, m_session->GetPlayer()->isAcceptTickets() ? "on" : "off");
        return true;
    }

    if (!m_session)
    {
        SendSysMessage(LANG_PLAYER_NOT_FOUND);
        SetSentErrorMessage(true);
        return false;
    }

    // ticket on
    if (strncmp(px, "on", 3) == 0)
    {
        m_session->GetPlayer()->SetAcceptTicket(true);
        SendSysMessage(LANG_COMMAND_TICKETON);
    }
    // ticket off
    else if (strncmp(px, "off", 4) == 0)
    {
        m_session->GetPlayer()->SetAcceptTicket(false);
        SendSysMessage(LANG_COMMAND_TICKETOFF);
    }
    else
    {
        return false;
    }

    return true;
}

bool ChatHandler::HandleTicketRespondCommand(char* args)
{
    GMTicket* ticket = NULL;

    // ticket respond #num
    uint32 num;
    if (ExtractUInt32(&args, num))
    {
        if (num == 0)
        {
            return false;
        }

        // mgr numbering tickets start from 0
        ticket = sTicketMgr.GetGMTicket(num);

        if (!ticket)
        {
            PSendSysMessage(LANG_COMMAND_TICKETNOTEXIST, num);
            SetSentErrorMessage(true);
            return false;
        }
    }
    else
    {
        ObjectGuid target_guid;
        std::string target_name;
        if (!ExtractPlayerTarget(&args, NULL, &target_guid, &target_name))
        {
            return false;
        }

        // ticket respond $char_name
        ticket = sTicketMgr.GetGMTicket(target_guid);

        if (!ticket)
        {
            PSendSysMessage(LANG_COMMAND_TICKETNOTEXIST_NAME, target_name.c_str());
            SetSentErrorMessage(true);
            return false;
        }
    }

    // no response text?
    if (!*args)
    {
        return false;
    }

    ticket->SetResponseText(args);

    if (Player* pl = sObjectMgr.GetPlayer(ticket->GetPlayerGuid()))
    {
        pl->GetSession()->SendGMTicketGetTicket(0x06, ticket);
        //How should we error here?
        if (m_session)
        {
            m_session->GetPlayer()->Whisper(args, LANG_UNIVERSAL, pl->GetObjectGuid());
        }
    }

    return true;
}

bool ChatHandler::HandleTicketShowCommand(char *args)
{
    // ticket #num
    char* px = ExtractLiteralArg(&args);
    if (!px)
    {
        return false;
    }

    uint32 num;
    if (ExtractUInt32(&px, num))
    {
        if (num == 0)
        {
            return false;
        }

        // mgr numbering tickets start from 0
        GMTicket* ticket = sTicketMgr.GetGMTicket(num);
        if (!ticket)
        {
            PSendSysMessage(LANG_COMMAND_TICKETNOTEXIST, num);
            SetSentErrorMessage(true);
            return false;
        }

        ShowTicket(ticket);
        return true;
    }

    ObjectGuid target_guid;
    std::string target_name;
    if (!ExtractPlayerTarget(&px, NULL, &target_guid, &target_name))
    {
        return false;
    }

    // ticket $char_name
    GMTicket* ticket = sTicketMgr.GetGMTicket(target_guid);
    if (!ticket)
    {
        PSendSysMessage(LANG_COMMAND_TICKETNOTEXIST_NAME, target_name.c_str());
        SetSentErrorMessage(true);
        return false;
    }

    ShowTicket(ticket);

    return true;
}

bool ChatHandler::HandleTickerSurveyClose(char *args)
{
    GMTicket* ticket = NULL;

    uint32 num;
    if (ExtractUInt32(&args, num))
    {
        if (num == 0)
        {
            return false;
        }

        ticket = sTicketMgr.GetGMTicket(num);

        if (!ticket)
        {
            PSendSysMessage(LANG_COMMAND_TICKETNOTEXIST, num);
            SetSentErrorMessage(true);
            return false;
        }
    }
    else
    {
        ObjectGuid target_guid;
        std::string target_name;
        if (!ExtractPlayerTarget(&args, NULL, &target_guid, &target_name))
        {
            return false;
        }

        // ticket respond $char_name
        ticket = sTicketMgr.GetGMTicket(target_guid);

        if (!ticket)
        {
            PSendSysMessage(LANG_COMMAND_TICKETNOTEXIST_NAME, target_name.c_str());
            SetSentErrorMessage(true);
            return false;
        }
    }

    ticket->CloseWithSurvey();

    //This needs to be before we delete the ticket
    Player* pPlayer = sObjectMgr.GetPlayer(ticket->GetPlayerGuid());

    //For now we can't close tickets for offline players, TODO
    if (!pPlayer)
    {
        SendSysMessage(LANG_COMMAND_TICKET_CANT_CLOSE);
        return false;
    }

    //This logic feels misplaced, but you can't have it in GMTicket?
    sTicketMgr.Delete(ticket->GetPlayerGuid());
    ticket = NULL;

    PSendSysMessage(LANG_COMMAND_TICKETCLOSED_NAME, pPlayer->GetName());

    return true;
}

/// Helper function
inline Creature* Helper_CreateWaypointFor(Creature* wpOwner, WaypointPathOrigin wpOrigin, int32 pathId, uint32 wpId, WaypointNode const* wpNode, CreatureInfo const* waypointInfo)
{
    TemporarySummonWaypoint* wpCreature = new TemporarySummonWaypoint(wpOwner->GetObjectGuid(), wpId, pathId, (uint32)wpOrigin);

    CreatureCreatePos pos(wpOwner->GetMap(), wpNode->x, wpNode->y, wpNode->z, wpNode->orientation);

    if (!wpCreature->Create(wpOwner->GetMap()->GenerateLocalLowGuid(HIGHGUID_UNIT), pos, waypointInfo))
    {
        delete wpCreature;
        return NULL;
    }

    wpCreature->SetVisibility(VISIBILITY_OFF);
    wpCreature->SetRespawnCoord(pos);

    wpCreature->SetActiveObjectState(true);

    wpCreature->Summon(TEMPSUMMON_TIMED_DESPAWN, 5 * MINUTE * IN_MILLISECONDS); // Also initializes the AI and MMGen
    return wpCreature;
}
inline void UnsummonVisualWaypoints(Player const* player, ObjectGuid ownerGuid)
{
    std::list<Creature*> waypoints;
    MaNGOS::AllCreaturesOfEntryInRangeCheck checkerForWaypoint(player, VISUAL_WAYPOINT, SIZE_OF_GRIDS);
    MaNGOS::CreatureListSearcher<MaNGOS::AllCreaturesOfEntryInRangeCheck> searcher(waypoints, checkerForWaypoint);
    Cell::VisitGridObjects(player, searcher, SIZE_OF_GRIDS);

    for (std::list<Creature*>::iterator itr = waypoints.begin(); itr != waypoints.end(); ++itr)
    {
        if ((*itr)->GetSubtype() != CREATURE_SUBTYPE_TEMPORARY_SUMMON)
        {
            continue;
        }

        TemporarySummonWaypoint* wpTarget = dynamic_cast<TemporarySummonWaypoint*>(*itr);
        if (!wpTarget)
        {
            continue;
        }

        if (wpTarget->GetSummonerGuid() == ownerGuid)
        {
            wpTarget->UnSummon();
        }
    }
}

/** Add a waypoint to a creature
 * .wp add [dbGuid] [pathId] [source]
 *
 * The user can either select an npc or provide its dbGuid.
 * Also the user can specify pathId and source if wanted.
 *
 * The user can even select a visual waypoint - then the new waypoint
 * is placed *after* the selected one - this makes insertion of new
 * waypoints possible.
 *
 * .wp add [pathId] [source]
 * -> adds a waypoint to the currently selected creature, to path pathId in source-storage
 *
 * .wp add guid [pathId] [source]
 * -> if no npc is selected, expect the creature provided with guid argument
 *
 * @return true - command did succeed, false - something went wrong
 */
bool ChatHandler::HandleWpAddCommand(char* args)
{
    DEBUG_LOG("DEBUG: HandleWpAddCommand");

    CreatureInfo const* waypointInfo = ObjectMgr::GetCreatureTemplate(VISUAL_WAYPOINT);
    if (!waypointInfo || waypointInfo->GetHighGuid() != HIGHGUID_UNIT)
    {
        return false;                                       // must exist as normal creature in mangos.sql 'creature_template'
    }

    Creature* targetCreature = getSelectedCreature();
    WaypointPathOrigin wpDestination = PATH_NO_PATH;        ///< into which storage
    int32 wpPathId = 0;                                     ///< along which path
    uint32 wpPointId = 0;                                   ///< pointId if a waypoint was selected, in this case insert after
    Creature* wpOwner = NULL;

    if (targetCreature)
    {
        // Check if the user did specify a visual waypoint
        if (targetCreature->GetEntry() == VISUAL_WAYPOINT && targetCreature->GetSubtype() == CREATURE_SUBTYPE_TEMPORARY_SUMMON)
        {
            TemporarySummonWaypoint* wpTarget = dynamic_cast<TemporarySummonWaypoint*>(targetCreature);
            if (!wpTarget)
            {
                PSendSysMessage(LANG_WAYPOINT_VP_SELECT);
                SetSentErrorMessage(true);
                return false;
            }

            // Who moves along this waypoint?
            wpOwner = targetCreature->GetMap()->GetAnyTypeCreature(wpTarget->GetSummonerGuid());
            if (!wpOwner)
            {
                PSendSysMessage(LANG_WAYPOINT_NOTFOUND_NPC, wpTarget->GetSummonerGuid().GetString().c_str());
                SetSentErrorMessage(true);
                return false;
            }
            wpDestination = (WaypointPathOrigin)wpTarget->GetPathOrigin();
            wpPathId = wpTarget->GetPathId();
            wpPointId = wpTarget->GetWaypointId() + 1;      // Insert as next waypoint
        }
        else // normal creature selected
        {
            wpOwner = targetCreature;
        }
    }
    else //!targetCreature - first argument must be dbGuid
    {
        uint32 dbGuid;
        if (!ExtractUInt32(&args, dbGuid))
        {
            PSendSysMessage(LANG_WAYPOINT_NOGUID);
            SetSentErrorMessage(true);
            return false;
        }

        CreatureData const* data = sObjectMgr.GetCreatureData(dbGuid);
        if (!data)
        {
            PSendSysMessage(LANG_WAYPOINT_CREATNOTFOUND, dbGuid);
            SetSentErrorMessage(true);
            return false;
        }

        if (m_session->GetPlayer()->GetMapId() != data->mapid)
        {
            PSendSysMessage(LANG_COMMAND_CREATUREATSAMEMAP, dbGuid);
            SetSentErrorMessage(true);
            return false;
        }

        wpOwner = m_session->GetPlayer()->GetMap()->GetAnyTypeCreature(data->GetObjectGuid(dbGuid));
        if (!wpOwner)
        {
            PSendSysMessage(LANG_WAYPOINT_CREATNOTFOUND, dbGuid);
            SetSentErrorMessage(true);
            return false;
        }
    }

    if (wpDestination == PATH_NO_PATH)                      // No Waypoint selected, parse additional params
    {
        if (ExtractOptInt32(&args, wpPathId, 0))            // Fill path-id and source
        {
            uint32 src = (uint32)PATH_NO_PATH;
            if (ExtractOptUInt32(&args, src, src))
            {
                wpDestination = (WaypointPathOrigin)src;
            }
            else // pathId provided but no destination
            {
                if (wpPathId != 0)
                {
                    wpDestination = PATH_FROM_ENTRY;        // Multiple Paths must only be assigned by entry
                }
            }
        }

        if (wpDestination == PATH_NO_PATH)                  // No overwrite params. Do best estimate
        {
            if (wpOwner->GetMotionMaster()->GetCurrentMovementGeneratorType() == WAYPOINT_MOTION_TYPE)
            {
                if (WaypointMovementGenerator<Creature> const* wpMMGen = dynamic_cast<WaypointMovementGenerator<Creature> const*>(wpOwner->GetMotionMaster()->GetCurrent()))
                {
                    wpMMGen->GetPathInformation(wpPathId, wpDestination);
                }
            }
            // Get information about default path if no current path. If no default path, prepare data dependendy on uniqueness
            if (wpDestination == PATH_NO_PATH && !sWaypointMgr.GetDefaultPath(wpOwner->GetEntry(), wpOwner->GetGUIDLow(), &wpDestination))
            {
                wpDestination = PATH_FROM_ENTRY;                // Default place to store paths
                if (wpOwner->HasStaticDBSpawnData())
                {
                    QueryResult* result = WorldDatabase.PQuery("SELECT COUNT(`id`) FROM `creature` WHERE `id` = %u", wpOwner->GetEntry());
                    if (result && result->Fetch()[0].GetUInt32() != 1)
                    {
                        wpDestination = PATH_FROM_GUID;
                    }
                    delete result;
                }
            }
        }
    }

    // All arguments parsed
    // wpOwner will get a new waypoint inserted into wpPath = GetPathFromOrigin(wpOwner, wpDestination, wpPathId) at wpPointId

    float x, y, z;
    m_session->GetPlayer()->GetPosition(x, y, z);
    if (!sWaypointMgr.AddNode(wpOwner->GetEntry(), wpOwner->GetGUIDLow(), wpPointId, wpDestination, x, y, z))
    {
        PSendSysMessage(LANG_WAYPOINT_NOTCREATED, wpPointId, wpOwner->GetGuidStr().c_str(), wpPathId, WaypointManager::GetOriginString(wpDestination).c_str());
        SetSentErrorMessage(true);
        return false;
    }

    // Unsummon old visuals, summon new ones
    UnsummonVisualWaypoints(m_session->GetPlayer(), wpOwner->GetObjectGuid());
    WaypointPath const* wpPath = sWaypointMgr.GetPathFromOrigin(wpOwner->GetEntry(), wpOwner->GetGUIDLow(), wpPathId, wpDestination);
    for (WaypointPath::const_iterator itr = wpPath->begin(); itr != wpPath->end(); ++itr)
    {
        if (!Helper_CreateWaypointFor(wpOwner, wpDestination, wpPathId, itr->first, &itr->second, waypointInfo))
        {
            PSendSysMessage(LANG_WAYPOINT_VP_NOTCREATED, VISUAL_WAYPOINT);
            SetSentErrorMessage(true);
            return false;
        }
    }

    PSendSysMessage(LANG_WAYPOINT_ADDED, wpPointId, wpOwner->GetGuidStr().c_str(), wpPathId, WaypointManager::GetOriginString(wpDestination).c_str());

    return true;
}                                                           // HandleWpAddCommand

/**
 * .wp modify waittime | scriptid | orientation | del | move [dbGuid, id] [value]
 *
 * waittime <Delay>
 *   User has selected a visual waypoint before.
 *   Delay <Delay> is added to this waypoint. Everytime the
 *   NPC comes to this waypoint, it will wait Delay millieseconds.
 *
 * waittime <DBGuid> <WPNUM> <Delay>
 *   User has not selected visual waypoint before.
 *   For the waypoint <WPNUM> for the NPC with <DBGuid>
 *   an delay Delay is added to this waypoint
 *   Everytime the NPC comes to this waypoint, it will wait Delay millieseconds.
 *
 * scriptid <scriptId>
 *   User has selected a visual waypoint before.
 *   <scriptId> is added to this waypoint. Everytime the
 *   NPC comes to this waypoint, the DBScript scriptId is executed.
 *
 * scriptid <DBGuid> <WPNUM> <scriptId>
 *   User has not selected visual waypoint before.
 *   For the waypoint <WPNUM> for the NPC with <DBGuid>
 *   an emote <scriptId> is added.
 *   Everytime the NPC comes to this waypoint, the DBScript scriptId is executed.
 *
 * orientation [DBGuid, WpNum] <Orientation>
 *   Set the orientation of the selected waypoint or waypoint given with DbGuid/ WpId
 *   to the value of <Orientation>.
 *
 * del [DBGuid, WpId]
 *   Remove the selected waypoint or waypoint given with DbGuid/ WpId.
 *
 * move [DBGuid, WpId]
 *   Move the selected waypoint or waypoint given with DbGuid/ WpId to player's current positiion.
 */
bool ChatHandler::HandleWpModifyCommand(char* args)
{
    DEBUG_LOG("DEBUG: HandleWpModifyCommand");

    if (!*args)
    {
        return false;
    }

    CreatureInfo const* waypointInfo = ObjectMgr::GetCreatureTemplate(VISUAL_WAYPOINT);
    if (!waypointInfo || waypointInfo->GetHighGuid() != HIGHGUID_UNIT)
    {
        return false; // must exist as normal creature in mangos.sql 'creature_template'
    }

    // first arg: add del text emote spell waittime move
    char* subCmd_str = ExtractLiteralArg(&args);
    if (!subCmd_str)
    {
        return false;
    }

    std::string subCmd = subCmd_str;
    // Check
    // Remember: "show" must also be the name of a column!
    if ((subCmd != "waittime") && (subCmd != "scriptid") && (subCmd != "orientation") && (subCmd != "del") && (subCmd != "move"))
    {
        return false;
    }

    // Next arg is: <GUID> <WPNUM> <ARGUMENT>

    // Did user provide a GUID or did the user select a creature?
    Creature* targetCreature = getSelectedCreature();       // Expect a visual waypoint to be selected
    Creature* wpOwner = NULL;                               // Who moves along the waypoint
    uint32 wpId = 0;
    WaypointPathOrigin wpSource = PATH_NO_PATH;
    int32 wpPathId = 0;

    if (targetCreature)
    {
        DEBUG_LOG("DEBUG: HandleWpModifyCommand - User did select an NPC");

        // Check if the user did specify a visual waypoint
        if (targetCreature->GetEntry() != VISUAL_WAYPOINT || targetCreature->GetSubtype() != CREATURE_SUBTYPE_TEMPORARY_SUMMON)
        {
            PSendSysMessage(LANG_WAYPOINT_VP_SELECT);
            SetSentErrorMessage(true);
            return false;
        }
        TemporarySummonWaypoint* wpTarget = dynamic_cast<TemporarySummonWaypoint*>(targetCreature);
        if (!wpTarget)
        {
            PSendSysMessage(LANG_WAYPOINT_VP_SELECT);
            SetSentErrorMessage(true);
            return false;
        }

        // Who moves along this waypoint?
        wpOwner = targetCreature->GetMap()->GetAnyTypeCreature(wpTarget->GetSummonerGuid());
        if (!wpOwner)
        {
            PSendSysMessage(LANG_WAYPOINT_NOTFOUND_NPC, wpTarget->GetSummonerGuid().GetString().c_str());
            SetSentErrorMessage(true);
            return false;
        }
        wpId = wpTarget->GetWaypointId();

        wpPathId = wpTarget->GetPathId();
        wpSource = (WaypointPathOrigin)wpTarget->GetPathOrigin();
    }
    else
    {
        uint32 dbGuid = 0;
        // User did provide <GUID> <WPNUM>
        if (!ExtractUInt32(&args, dbGuid))
        {
            SendSysMessage(LANG_WAYPOINT_NOGUID);
            SetSentErrorMessage(true);
            return false;
        }

        if (!ExtractUInt32(&args, wpId))
        {
            SendSysMessage(LANG_WAYPOINT_NOWAYPOINTGIVEN);
            SetSentErrorMessage(true);
            return false;
        }

        CreatureData const* data = sObjectMgr.GetCreatureData(dbGuid);
        if (!data)
        {
            PSendSysMessage(LANG_WAYPOINT_CREATNOTFOUND, dbGuid);
            SetSentErrorMessage(true);
            return false;
        }

        wpOwner = m_session->GetPlayer()->GetMap()->GetAnyTypeCreature(data->GetObjectGuid(dbGuid));
        if (!wpOwner)
        {
            PSendSysMessage(LANG_WAYPOINT_CREATNOTFOUND, dbGuid);
            SetSentErrorMessage(true);
            return false;
        }
    }

    if (wpSource == PATH_NO_PATH)                           // No waypoint selected
    {
        if (wpOwner->GetMotionMaster()->GetCurrentMovementGeneratorType() == WAYPOINT_MOTION_TYPE)
        {
            if (WaypointMovementGenerator<Creature> const* wpMMGen = dynamic_cast<WaypointMovementGenerator<Creature> const*>(wpOwner->GetMotionMaster()->GetCurrent()))
            {
                wpMMGen->GetPathInformation(wpPathId, wpSource);
            }
        }
        if (wpSource == PATH_NO_PATH)
        {
            sWaypointMgr.GetDefaultPath(wpOwner->GetEntry(), wpOwner->GetGUIDLow(), &wpSource);
        }
    }

    WaypointPath const* wpPath = sWaypointMgr.GetPathFromOrigin(wpOwner->GetEntry(), wpOwner->GetGUIDLow(), wpPathId, wpSource);
    if (!wpPath)
    {
        PSendSysMessage(LANG_WAYPOINT_NOTFOUNDPATH, wpOwner->GetGuidStr().c_str(), wpPathId, WaypointManager::GetOriginString(wpSource).c_str());
        SetSentErrorMessage(true);
        return false;
    }

    WaypointPath::const_iterator point = wpPath->find(wpId);
    if (point == wpPath->end())
    {
        PSendSysMessage(LANG_WAYPOINT_NOTFOUND, wpId, wpOwner->GetGuidStr().c_str(), wpPathId, WaypointManager::GetOriginString(wpSource).c_str());
        SetSentErrorMessage(true);
        return false;
    }

    // If no visual WP was selected, but we are not going to remove it
    if (!targetCreature && subCmd != "del")
    {
        targetCreature = Helper_CreateWaypointFor(wpOwner, wpSource, wpPathId, wpId, &(point->second), waypointInfo);
        if (!targetCreature)
        {
            PSendSysMessage(LANG_WAYPOINT_VP_NOTCREATED, VISUAL_WAYPOINT);
            SetSentErrorMessage(true);
            return false;
        }
    }

    if (subCmd == "del")                                    // Remove WP, no additional command required
    {
        sWaypointMgr.DeleteNode(wpOwner->GetEntry(), wpOwner->GetGUIDLow(), wpId, wpPathId, wpSource);

        if (TemporarySummonWaypoint* wpCreature = dynamic_cast<TemporarySummonWaypoint*>(targetCreature))
        {
            wpCreature->UnSummon();
        }

        if (wpPath->empty())
        {
            wpOwner->SetDefaultMovementType(RANDOM_MOTION_TYPE);
            wpOwner->GetMotionMaster()->Initialize();
            if (wpOwner->IsAlive())                         // Dead creature will reset movement generator at respawn
            {
                wpOwner->SetDeathState(JUST_DIED);
                wpOwner->Respawn();
            }
            wpOwner->SaveToDB();
        }

        PSendSysMessage(LANG_WAYPOINT_REMOVED);
        return true;
    }
    else if (subCmd == "move")                              // Move to player position, no additional command required
    {
        float x, y, z;
        m_session->GetPlayer()->GetPosition(x, y, z);

        // Move visual waypoint
        targetCreature->NearTeleportTo(x, y, z, targetCreature->GetOrientation());

        sWaypointMgr.SetNodePosition(wpOwner->GetEntry(), wpOwner->GetGUIDLow(), wpId, wpPathId, wpSource, x, y, z);

        PSendSysMessage(LANG_WAYPOINT_CHANGED);
        return true;
    }
    else if (subCmd == "waittime")
    {
        uint32 waittime;
        if (!ExtractUInt32(&args, waittime))
        {
            return false;
        }

        sWaypointMgr.SetNodeWaittime(wpOwner->GetEntry(), wpOwner->GetGUIDLow(), wpId, wpPathId, wpSource, waittime);
    }
    else if (subCmd == "scriptid")
    {
        uint32 scriptId;
        if (!ExtractUInt32(&args, scriptId))
        {
            return false;
        }

        if (!sWaypointMgr.SetNodeScriptId(wpOwner->GetEntry(), wpOwner->GetGUIDLow(), wpId, wpPathId, wpSource, scriptId))
        {
            PSendSysMessage(LANG_WAYPOINT_INFO_UNK_SCRIPTID, scriptId);
        }
    }
    else if (subCmd == "orientation")
    {
        float ori;
        if (!ExtractFloat(&args, ori))
        {
            return false;
        }

        sWaypointMgr.SetNodeOrientation(wpOwner->GetEntry(), wpOwner->GetGUIDLow(), wpId, wpPathId, wpSource, ori);
    }

    PSendSysMessage(LANG_WAYPOINT_CHANGED_NO, subCmd_str);
    return true;
}

/**
 * .wp show info | on | off | first | last [dbGuid] [pathId [wpOrigin] ]
 *
 * info -> User has selected a visual waypoint before
 *
 * on -> User has selected an NPC; all visual waypoints for this
 *       NPC are added to the world
 *
 * on <dbGuid> -> User did not select an NPC - instead the dbGuid of the
 *              NPC is provided. All visual waypoints for this NPC
 *              are added from the world.
 *
 * off -> User has selected an NPC; all visual waypoints for this
 *        NPC are removed from the world.
 */
bool ChatHandler::HandleWpShowCommand(char* args)
{
    DEBUG_LOG("DEBUG: HandleWpShowCommand");

    if (!*args)
    {
        return false;
    }

    CreatureInfo const* waypointInfo = ObjectMgr::GetCreatureTemplate(VISUAL_WAYPOINT);
    if (!waypointInfo || waypointInfo->GetHighGuid() != HIGHGUID_UNIT)
    {
        return false; // must exist as normal creature in mangos.sql 'creature_template'
    }

    // first arg: info, on, off, first, last

    char* subCmd_str = ExtractLiteralArg(&args);
    if (!subCmd_str)
    {
        return false;
    }
    std::string subCmd = subCmd_str;                        ///< info, on, off, first, last

    uint32 dbGuid = 0;
    int32 wpPathId = 0;
    WaypointPathOrigin wpOrigin = PATH_NO_PATH;

    // User selected an npc?
    Creature* targetCreature = getSelectedCreature();
    if (targetCreature)
    {
        if (ExtractOptInt32(&args, wpPathId, 0))            // Fill path-id and source
        {
            uint32 src;
            if (ExtractOptUInt32(&args, src, (uint32)PATH_NO_PATH))
            {
                wpOrigin = (WaypointPathOrigin)src;
            }
        }
    }
    else    // Guid must be provided
    {
        if (!ExtractUInt32(&args, dbGuid))                  // No creature selected and no dbGuid provided
        {
            return false;
        }

        if (ExtractOptInt32(&args, wpPathId, 0))            // Fill path-id and source
        {
            uint32 src = (uint32)PATH_NO_PATH;
            if (ExtractOptUInt32(&args, src, src))
            {
                wpOrigin = (WaypointPathOrigin)src;
            }
        }

        // Params now parsed, check them
        CreatureData const* data = sObjectMgr.GetCreatureData(dbGuid);
        if (!data)
        {
            PSendSysMessage(LANG_WAYPOINT_CREATNOTFOUND, dbGuid);
            SetSentErrorMessage(true);
            return false;
        }

        targetCreature = m_session->GetPlayer()->GetMap()->GetCreature(data->GetObjectGuid(dbGuid));
        if (!targetCreature)
        {
            PSendSysMessage(LANG_WAYPOINT_CREATNOTFOUND, dbGuid);
            SetSentErrorMessage(true);
            return false;
        }
    }

    Creature* wpOwner = NULL;                               ///< Npc that is moving
    TemporarySummonWaypoint* wpTarget = NULL;               // Define here for wp-info command

    // Show info for the selected waypoint (Step one: get moving npc)
    if (subCmd == "info")
    {
        // Check if the user did specify a visual waypoint
        if (targetCreature->GetEntry() != VISUAL_WAYPOINT || targetCreature->GetSubtype() != CREATURE_SUBTYPE_TEMPORARY_SUMMON)
        {
            PSendSysMessage(LANG_WAYPOINT_VP_SELECT);
            SetSentErrorMessage(true);
            return false;
        }
        wpTarget = dynamic_cast<TemporarySummonWaypoint*>(targetCreature);
        if (!wpTarget)
        {
            PSendSysMessage(LANG_WAYPOINT_VP_SELECT);
            SetSentErrorMessage(true);
            return false;
        }

        // Who moves along this waypoint?
        wpOwner = targetCreature->GetMap()->GetAnyTypeCreature(wpTarget->GetSummonerGuid());
        if (!wpOwner)
        {
            PSendSysMessage(LANG_WAYPOINT_NOTFOUND_NPC, wpTarget->GetSummonerGuid().GetString().c_str());
            SetSentErrorMessage(true);
            return false;
        }

        // Ignore params, use information of selected waypoint!
        wpOrigin = (WaypointPathOrigin)wpTarget->GetPathOrigin();
        wpPathId = wpTarget->GetPathId();
    }
    else
    {
        wpOwner = targetCreature;
    }

    // Get the path
    WaypointPath* wpPath = NULL;
    if (wpOrigin != PATH_NO_PATH)                           // Might have been provided by param
    {
        wpPath = sWaypointMgr.GetPathFromOrigin(wpOwner->GetEntry(), wpOwner->GetGUIDLow(), wpPathId, wpOrigin);
    }
    else
    {
        if (wpOwner->GetMotionMaster()->GetCurrentMovementGeneratorType() == WAYPOINT_MOTION_TYPE)
        {
            if (WaypointMovementGenerator<Creature> const* wpMMGen = dynamic_cast<WaypointMovementGenerator<Creature> const*>(wpOwner->GetMotionMaster()->GetCurrent()))
            {
                wpMMGen->GetPathInformation(wpPathId, wpOrigin);
                wpPath = sWaypointMgr.GetPathFromOrigin(wpOwner->GetEntry(), wpOwner->GetGUIDLow(), wpPathId, wpOrigin);
            }
        }
        if (wpOrigin == PATH_NO_PATH)
        {
            wpPath = sWaypointMgr.GetDefaultPath(wpOwner->GetEntry(), wpOwner->GetGUIDLow(), &wpOrigin);
        }
    }

    if (!wpPath || wpPath->empty())
    {
        PSendSysMessage(LANG_WAYPOINT_NOTFOUNDPATH, wpOwner->GetGuidStr().c_str(), wpPathId, WaypointManager::GetOriginString(wpOrigin).c_str());
        SetSentErrorMessage(true);
        return false;
    }

    // Show info for the selected waypoint (Step two: Show actual info)
    if (subCmd == "info")
    {
        // Find the waypoint
        WaypointPath::const_iterator point = wpPath->find(wpTarget->GetWaypointId());
        if (point == wpPath->end())
        {
            PSendSysMessage(LANG_WAYPOINT_NOTFOUND, wpTarget->GetWaypointId(), wpOwner->GetGuidStr().c_str(), wpPathId, WaypointManager::GetOriginString(wpOrigin).c_str());
            SetSentErrorMessage(true);
            return false;
        }

        PSendSysMessage(LANG_WAYPOINT_INFO_TITLE, wpTarget->GetWaypointId(), wpOwner->GetGuidStr().c_str(), wpPathId, WaypointManager::GetOriginString(wpOrigin).c_str());
        PSendSysMessage(LANG_WAYPOINT_INFO_WAITTIME, point->second.delay);
        PSendSysMessage(LANG_WAYPOINT_INFO_ORI, point->second.orientation);
        PSendSysMessage(LANG_WAYPOINT_INFO_SCRIPTID, point->second.script_id);
        if (wpOrigin == PATH_FROM_EXTERNAL)
        {
            PSendSysMessage(LANG_WAYPOINT_INFO_AISCRIPT, wpOwner->GetScriptName().c_str());
        }
        if (WaypointBehavior* behaviour = point->second.behavior)
        {
            PSendSysMessage(" ModelId1: %u", behaviour->model1);
            PSendSysMessage(" ModelId2: %u", behaviour->model2);
            PSendSysMessage(" Emote: %u", behaviour->emote);
            PSendSysMessage(" Spell: %u", behaviour->spell);
            for (int i = 0;  i < MAX_WAYPOINT_TEXT; ++i)
            {
                PSendSysMessage(" TextId%i: %i \'%s\'", i + 1, behaviour->textid[i], (behaviour->textid[i] ? GetMangosString(behaviour->textid[i]) : ""));
            }
        }

        return true;
    }

    if (subCmd == "on")
    {
        UnsummonVisualWaypoints(m_session->GetPlayer(), wpOwner->GetObjectGuid());

        for (WaypointPath::const_iterator pItr = wpPath->begin(); pItr != wpPath->end(); ++pItr)
        {
            if (!Helper_CreateWaypointFor(wpOwner, wpOrigin, wpPathId, pItr->first, &(pItr->second), waypointInfo))
            {
                PSendSysMessage(LANG_WAYPOINT_VP_NOTCREATED, VISUAL_WAYPOINT);
                SetSentErrorMessage(true);
                return false;
            }
        }

        return true;
    }

    if (subCmd == "first")
    {
        if (!Helper_CreateWaypointFor(wpOwner, wpOrigin, wpPathId, wpPath->begin()->first, &(wpPath->begin()->second), waypointInfo))
        {
            PSendSysMessage(LANG_WAYPOINT_VP_NOTCREATED, VISUAL_WAYPOINT);
            SetSentErrorMessage(true);
            return false;
        }

        // player->PlayerTalkClass->SendPointOfInterest(x, y, 6, 6, 0, "First Waypoint");
        return true;
    }

    if (subCmd == "last")
    {
        if (!Helper_CreateWaypointFor(wpOwner, wpOrigin, wpPathId, wpPath->rbegin()->first, &(wpPath->rbegin()->second), waypointInfo))
        {
            PSendSysMessage(LANG_WAYPOINT_VP_NOTCREATED, VISUAL_WAYPOINT);
            SetSentErrorMessage(true);
            return false;
        }

        // player->PlayerTalkClass->SendPointOfInterest(x, y, 6, 6, 0, "Last Waypoint");
        return true;
    }

    if (subCmd == "off")
    {
        UnsummonVisualWaypoints(m_session->GetPlayer(), wpOwner->GetObjectGuid());
        PSendSysMessage(LANG_WAYPOINT_VP_ALLREMOVED);
        return true;
    }

    return false;
}                                                           // HandleWpShowCommand

/// [Guid if no selected unit] <filename> [pathId [wpOrigin] ]
bool ChatHandler::HandleWpExportCommand(char* args)
{
    if (!*args)
    {
        return false;
    }

    Creature* wpOwner = NULL;
    WaypointPathOrigin wpOrigin = PATH_NO_PATH;
    int32 wpPathId = 0;

    if (Creature* targetCreature = getSelectedCreature())
    {
        // Check if the user did specify a visual waypoint
        if (targetCreature->GetEntry() == VISUAL_WAYPOINT && targetCreature->GetSubtype() == CREATURE_SUBTYPE_TEMPORARY_SUMMON)
        {
            TemporarySummonWaypoint* wpTarget = dynamic_cast<TemporarySummonWaypoint*>(targetCreature);
            if (!wpTarget)
            {
                PSendSysMessage(LANG_WAYPOINT_VP_SELECT);
                SetSentErrorMessage(true);
                return false;
            }

            // Who moves along this waypoint?
            wpOwner = targetCreature->GetMap()->GetAnyTypeCreature(wpTarget->GetSummonerGuid());
            if (!wpOwner)
            {
                PSendSysMessage(LANG_WAYPOINT_NOTFOUND_NPC, wpTarget->GetSummonerGuid().GetString().c_str());
                SetSentErrorMessage(true);
                return false;
            }
            wpOrigin = (WaypointPathOrigin)wpTarget->GetPathOrigin();
            wpPathId = wpTarget->GetPathId();
        }
        else // normal creature selected
        {
            wpOwner = targetCreature;
        }
    }
    else
    {
        uint32 dbGuid;
        if (!ExtractUInt32(&args, dbGuid))
        {
            PSendSysMessage(LANG_WAYPOINT_NOGUID);
            SetSentErrorMessage(true);
            return false;
        }

        CreatureData const* data = sObjectMgr.GetCreatureData(dbGuid);
        if (!data)
        {
            PSendSysMessage(LANG_WAYPOINT_CREATNOTFOUND, dbGuid);
            SetSentErrorMessage(true);
            return false;
        }

        if (m_session->GetPlayer()->GetMapId() != data->mapid)
        {
            PSendSysMessage(LANG_COMMAND_CREATUREATSAMEMAP, dbGuid);
            SetSentErrorMessage(true);
            return false;
        }

        wpOwner = m_session->GetPlayer()->GetMap()->GetAnyTypeCreature(data->GetObjectGuid(dbGuid));
        if (!wpOwner)
        {
            PSendSysMessage(LANG_WAYPOINT_CREATNOTFOUND, dbGuid);
            SetSentErrorMessage(true);
            return false;
        }
    }

    // wpOwner is now known, in case of export by visual waypoint also the to be exported path
    char* export_str = ExtractLiteralArg(&args);
    if (!export_str)
    {
        PSendSysMessage(LANG_WAYPOINT_ARGUMENTREQ, "export");
        SetSentErrorMessage(true);
        return false;
    }

    if (wpOrigin == PATH_NO_PATH)                           // No WP selected, Extract optional arguments
    {
        if (ExtractOptInt32(&args, wpPathId, 0))            // Fill path-id and source
        {
            uint32 src = (uint32)PATH_NO_PATH;
            if (ExtractOptUInt32(&args, src, src))
            {
                wpOrigin = (WaypointPathOrigin)src;
            }
            else // pathId provided but no destination
            {
                if (wpPathId != 0)
                {
                    wpOrigin = PATH_FROM_ENTRY;             // Multiple Paths must only be assigned by entry
                }
            }
        }

        if (wpOrigin == PATH_NO_PATH)
        {
            if (wpOwner->GetMotionMaster()->GetCurrentMovementGeneratorType() == WAYPOINT_MOTION_TYPE)
                if (WaypointMovementGenerator<Creature> const* wpMMGen = dynamic_cast<WaypointMovementGenerator<Creature> const*>(wpOwner->GetMotionMaster()->GetCurrent()))
                {
                    wpMMGen->GetPathInformation(wpPathId, wpOrigin);
                }
            if (wpOrigin == PATH_NO_PATH)
            {
                sWaypointMgr.GetDefaultPath(wpOwner->GetEntry(), wpOwner->GetGUIDLow(), &wpOrigin);
            }
        }
    }

    WaypointPath const* wpPath = sWaypointMgr.GetPathFromOrigin(wpOwner->GetEntry(), wpOwner->GetGUIDLow(), wpPathId, wpOrigin);
    if (!wpPath || wpPath->empty())
    {
        PSendSysMessage(LANG_WAYPOINT_NOTHINGTOEXPORT);
        SetSentErrorMessage(true);
        return false;
    }

    std::ofstream outfile;
    outfile.open(export_str);

    std::string table;
    char const* key_field;
    uint32 key;
    switch (wpOrigin)
    {
        case PATH_FROM_ENTRY: key = wpOwner->GetEntry();    key_field = "entry";    table = "creature_movement_template"; break;
        case PATH_FROM_GUID: key = wpOwner->GetGUIDLow();   key_field = "id";       table = "creature_movement"; break;
        case PATH_FROM_EXTERNAL: key = wpOwner->GetEntry(); key_field = "entry";    table = sWaypointMgr.GetExternalWPTable(); break;
        case PATH_NO_PATH:
            return false;
    }

    outfile << "DELETE FROM `" << table << "` WHERE `" << key_field << "`=" << key << ";\n";
    if (wpOrigin != PATH_FROM_EXTERNAL)
    {
        outfile << "INSERT INTO `" << table << "` (`" << key_field << "`, `point`, `position_x`, `position_y`, `position_z`, `orientation`, `waittime`, `script_id`) VALUES\n";
    }
    else
    {
        outfile << "INSERT INTO `" << table << "` (`" << key_field << "`, `point`, `position_x`, `position_y`, `position_z`, `orientation`, `waittime`) VALUES\n";
    }

    WaypointPath::const_iterator itr = wpPath->begin();
    uint32 countDown = wpPath->size();
    for (; itr != wpPath->end(); ++itr, --countDown)
    {
        outfile << "(" << key << ",";
        outfile << itr->first << ",";
        outfile << itr->second.x << ",";
        outfile << itr->second.y << ",";
        outfile << itr->second.z << ",";
        outfile << itr->second.orientation << ",";
        outfile << itr->second.delay << ",";
        if (wpOrigin != PATH_FROM_EXTERNAL)                 // Only for normal waypoints
        {
            outfile << itr->second.script_id << ")";
        }
        if (countDown > 1)
        {
            outfile << ",\n";
        }
        else
        {
            outfile << ";\n";
        }
    }

    PSendSysMessage(LANG_WAYPOINT_EXPORTED);
    outfile.close();

    return true;
}

// rename characters
bool ChatHandler::HandleCharacterRenameCommand(char* args)
{
    Player* target;
    ObjectGuid target_guid;
    std::string target_name;
    if (!ExtractPlayerTarget(&args, &target, &target_guid, &target_name))
    {
        return false;
    }

    if (target)
    {
        // check online security
        if (HasLowerSecurity(target))
        {
            return false;
        }

        PSendSysMessage(LANG_RENAME_PLAYER, GetNameLink(target).c_str());
        target->SetAtLoginFlag(AT_LOGIN_RENAME);
        CharacterDatabase.PExecute("UPDATE `characters` SET `at_login` = `at_login` | '1' WHERE `guid` = '%u'", target->GetGUIDLow());
    }
    else
    {
        // check offline security
        if (HasLowerSecurity(NULL, target_guid))
        {
            return false;
        }

        std::string oldNameLink = playerLink(target_name);

        PSendSysMessage(LANG_RENAME_PLAYER_GUID, oldNameLink.c_str(), target_guid.GetCounter());
        CharacterDatabase.PExecute("UPDATE `characters` SET `at_login` = `at_login` | '1' WHERE `guid` = '%u'", target_guid.GetCounter());
    }

    return true;
}

bool ChatHandler::HandleCharacterReputationCommand(char* args)
{
    Player* target;
    if (!ExtractPlayerTarget(&args, &target))
    {
        return false;
    }

    LocaleConstant loc = GetSessionDbcLocale();

    FactionStateList const& targetFSL = target->GetReputationMgr().GetStateList();
    for (FactionStateList::const_iterator itr = targetFSL.begin(); itr != targetFSL.end(); ++itr)
    {
        FactionEntry const* factionEntry = sFactionStore.LookupEntry(itr->second.ID);

        ShowFactionListHelper(factionEntry, loc, &itr->second, target);
    }
    return true;
}


bool ChatHandler::HandleHonorAddCommand(char* args)
{
    if (!*args)
    {
        return false;
    }

    Player* target = getSelectedPlayer();
    if (!target)
    {
        SendSysMessage(LANG_PLAYER_NOT_FOUND);
        SetSentErrorMessage(true);
        return false;
    }

    // check online security
    if (HasLowerSecurity(target))
    {
        return false;
    }

    float amount = (float)atof(args);
    target->RewardHonor(NULL, 1, amount);
    return true;
}

bool ChatHandler::HandleHonorAddKillCommand(char* /*args*/)
{
    Unit* target = getSelectedUnit();
    if (!target)
    {
        SendSysMessage(LANG_PLAYER_NOT_FOUND);
        SetSentErrorMessage(true);
        return false;
    }

    // check online security
    if (target->GetTypeId() == TYPEID_PLAYER && HasLowerSecurity((Player*)target))
    {
        return false;
    }

    m_session->GetPlayer()->RewardHonor(target, 1);
    return true;
}

bool ChatHandler::HandleHonorUpdateCommand(char* /*args*/)
{
    Player* target = getSelectedPlayer();
    if (!target)
    {
        SendSysMessage(LANG_PLAYER_NOT_FOUND);
        SetSentErrorMessage(true);
        return false;
    }

    // check online security
    if (HasLowerSecurity(target))
    {
        return false;
    }

    target->UpdateHonorFields();
    return true;
}


bool ChatHandler::HandleCombatStopCommand(char* args)
{
    Player* target;
    if (!ExtractPlayerTarget(&args, &target))
    {
        return false;
    }

    // check online security
    if (HasLowerSecurity(target))
    {
        return false;
    }

    target->CombatStop();
    target->GetHostileRefManager().deleteReferences();
    return true;
}

void ChatHandler::HandleLearnSkillRecipesHelper(Player* player, uint32 skill_id)
{
    uint32 classmask = player->getClassMask();

    for (uint32 j = 0; j < sSkillLineAbilityStore.GetNumRows(); ++j)
    {
        SkillLineAbilityEntry const* skillLine = sSkillLineAbilityStore.LookupEntry(j);
        if (!skillLine)
        {
            continue;
        }

        // wrong skill
        if (skillLine->skillId != skill_id)
        {
            continue;
        }

        // not high rank
        if (skillLine->forward_spellid)
        {
            continue;
        }

        // skip racial skills
        if (skillLine->racemask != 0)
        {
            continue;
        }

        // skip wrong class skills
        if (skillLine->classmask && (skillLine->classmask & classmask) == 0)
        {
            continue;
        }

        SpellEntry const* spellInfo = sSpellStore.LookupEntry(skillLine->spellId);
        if (!spellInfo || !SpellMgr::IsSpellValid(spellInfo, player, false))
        {
            continue;
        }

        player->learnSpell(skillLine->spellId, false);
    }
}

bool ChatHandler::HandleLearnAllCraftsCommand(char* /*args*/)
{
    for (uint32 i = 0; i < sSkillLineStore.GetNumRows(); ++i)
    {
        SkillLineEntry const* skillInfo = sSkillLineStore.LookupEntry(i);
        if (!skillInfo)
        {
            continue;
        }

        if (skillInfo->categoryId == SKILL_CATEGORY_PROFESSION || skillInfo->categoryId == SKILL_CATEGORY_SECONDARY)
        {
            HandleLearnSkillRecipesHelper(m_session->GetPlayer(), skillInfo->id);
        }
    }

    SendSysMessage(LANG_COMMAND_LEARN_ALL_CRAFT);
    return true;
}

bool ChatHandler::HandleLearnAllRecipesCommand(char* args)
{
    //  Learns all recipes of specified profession and sets skill to max
    //  Example: .learn all_recipes enchanting

    Player* target = getSelectedPlayer();
    if (!target)
    {
        SendSysMessage(LANG_PLAYER_NOT_FOUND);
        return false;
    }

    if (!*args)
    {
        return false;
    }

    std::wstring wnamepart;

    if (!Utf8toWStr(args, wnamepart))
    {
        return false;
    }

    // converting string that we try to find to lower case
    wstrToLower(wnamepart);

    std::string name;

    SkillLineEntry const* targetSkillInfo = NULL;
    for (uint32 i = 1; i < sSkillLineStore.GetNumRows(); ++i)
    {
        SkillLineEntry const* skillInfo = sSkillLineStore.LookupEntry(i);
        if (!skillInfo)
        {
            continue;
        }

        if (skillInfo->categoryId != SKILL_CATEGORY_PROFESSION &&
            skillInfo->categoryId != SKILL_CATEGORY_SECONDARY)
        {
            continue;
        }

        int loc = GetSessionDbcLocale();
        name = skillInfo->name[loc];
        if (name.empty())
        {
            continue;
        }

        if (!Utf8FitTo(name, wnamepart))
        {
            loc = 0;
            for (; loc < MAX_LOCALE; ++loc)
            {
                if (loc == GetSessionDbcLocale())
                {
                    continue;
                }

                name = skillInfo->name[loc];
                if (name.empty())
                {
                    continue;
                }

                if (Utf8FitTo(name, wnamepart))
                {
                    break;
                }
            }
        }

        if (loc < MAX_LOCALE)
        {
            targetSkillInfo = skillInfo;
            break;
        }
    }

    if (!targetSkillInfo)
    {
        return false;
    }

    HandleLearnSkillRecipesHelper(target, targetSkillInfo->id);

    uint16 maxLevel = target->GetPureMaxSkillValue(targetSkillInfo->id);
    target->SetSkill(targetSkillInfo->id, maxLevel, maxLevel);
    PSendSysMessage(LANG_COMMAND_LEARN_ALL_RECIPES, name.c_str());
    return true;
}




void ChatHandler::ShowPoolListHelper(uint16 pool_id)
{
    PoolTemplateData const& pool_template = sPoolMgr.GetPoolTemplate(pool_id);
    if (m_session)
        PSendSysMessage(LANG_POOL_ENTRY_LIST_CHAT,
                        pool_id, pool_id, pool_template.description.c_str(), pool_template.AutoSpawn ? 1 : 0, pool_template.MaxLimit,
                        sPoolMgr.GetPoolCreatures(pool_id).size(), sPoolMgr.GetPoolGameObjects(pool_id).size(), sPoolMgr.GetPoolPools(pool_id).size());
    else
        PSendSysMessage(LANG_POOL_ENTRY_LIST_CONSOLE,
                        pool_id, pool_template.description.c_str(), pool_template.AutoSpawn ? 1 : 0, pool_template.MaxLimit,
                        sPoolMgr.GetPoolCreatures(pool_id).size(), sPoolMgr.GetPoolGameObjects(pool_id).size(), sPoolMgr.GetPoolPools(pool_id).size());
}


bool ChatHandler::HandlePoolListCommand(char* /*args*/)
{
    Player* player = m_session->GetPlayer();

    MapPersistentState* mapState = player->GetMap()->GetPersistentState();

    if (!mapState->GetMapEntry()->Instanceable())
    {
        PSendSysMessage(LANG_POOL_LIST_NON_INSTANCE, mapState->GetMapEntry()->name[GetSessionDbcLocale()], mapState->GetMapId());
        SetSentErrorMessage(false);
        return false;
    }

    uint32 counter = 0;

    // spawn pools for expected map or for not initialized shared pools state for non-instanceable maps
    for (uint16 pool_id = 0; pool_id < sPoolMgr.GetMaxPoolId(); ++pool_id)
    {
        if (sPoolMgr.GetPoolTemplate(pool_id).CanBeSpawnedAtMap(mapState->GetMapEntry()))
        {
            ShowPoolListHelper(pool_id);
            ++counter;
        }
    }

    if (counter == 0)
    {
        PSendSysMessage(LANG_NO_POOL_FOR_MAP, mapState->GetMapEntry()->name[GetSessionDbcLocale()], mapState->GetMapId());
    }

    return true;
}

bool ChatHandler::HandlePoolSpawnsCommand(char* args)
{
    Player* player = m_session->GetPlayer();

    MapPersistentState* mapState = player->GetMap()->GetPersistentState();

    // shared continent pools data expected too big for show
    uint32 pool_id = 0;
    if (!ExtractUint32KeyFromLink(&args, "Hpool", pool_id) && !mapState->GetMapEntry()->Instanceable())
    {
        PSendSysMessage(LANG_POOL_SPAWNS_NON_INSTANCE, mapState->GetMapEntry()->name[GetSessionDbcLocale()], mapState->GetMapId());
        SetSentErrorMessage(false);
        return false;
    }

    SpawnedPoolData const& spawns = mapState->GetSpawnedPoolData();

    SpawnedPoolObjects const& crSpawns = spawns.GetSpawnedCreatures();
    for (SpawnedPoolObjects::const_iterator itr = crSpawns.begin(); itr != crSpawns.end(); ++itr)
        if (!pool_id || pool_id == sPoolMgr.IsPartOfAPool<Creature>(*itr))
            if (CreatureData const* data = sObjectMgr.GetCreatureData(*itr))
                if (CreatureInfo const* info = ObjectMgr::GetCreatureTemplate(data->id))
                    PSendSysMessage(LANG_CREATURE_LIST_CHAT, *itr, PrepareStringNpcOrGoSpawnInformation<Creature>(*itr).c_str(),
                                    *itr, info->Name, data->posX, data->posY, data->posZ, data->mapid);

    SpawnedPoolObjects const& goSpawns = spawns.GetSpawnedGameobjects();
    for (SpawnedPoolObjects::const_iterator itr = goSpawns.begin(); itr != goSpawns.end(); ++itr)
        if (!pool_id || pool_id == sPoolMgr.IsPartOfAPool<GameObject>(*itr))
            if (GameObjectData const* data = sObjectMgr.GetGOData(*itr))
                if (GameObjectInfo const* info = ObjectMgr::GetGameObjectInfo(data->id))
                    PSendSysMessage(LANG_GO_LIST_CHAT, *itr, PrepareStringNpcOrGoSpawnInformation<GameObject>(*itr).c_str(),
                                    *itr, info->name, data->posX, data->posY, data->posZ, data->mapid);

    return true;
}

bool ChatHandler::HandlePoolInfoCommand(char* args)
{
    // id or [name] Shift-click form |color|Hpool:id|h[name]|h|r
    uint32 pool_id;
    if (!ExtractUint32KeyFromLink(&args, "Hpool", pool_id))
    {
        return false;
    }

    if (pool_id > sPoolMgr.GetMaxPoolId())
    {
        PSendSysMessage(LANG_POOL_ENTRY_LOWER_MAX_POOL, pool_id, sPoolMgr.GetMaxPoolId());
        return true;
    }

    Player* player = m_session ? m_session->GetPlayer() : NULL;

    MapPersistentState* mapState = player ? player->GetMap()->GetPersistentState() : NULL;
    SpawnedPoolData const* spawns = mapState ? &mapState->GetSpawnedPoolData() : NULL;

    std::string active_str = GetMangosString(LANG_ACTIVE);

    PoolTemplateData const& pool_template = sPoolMgr.GetPoolTemplate(pool_id);
    uint32 mother_pool_id = sPoolMgr.IsPartOfAPool<Pool>(pool_id);
    if (!mother_pool_id)
    {
        PSendSysMessage(LANG_POOL_INFO_HEADER, pool_id, pool_template.AutoSpawn, pool_template.MaxLimit);
    }
    else
    {
        PoolTemplateData const& mother_template = sPoolMgr.GetPoolTemplate(mother_pool_id);
        if (m_session)
            PSendSysMessage(LANG_POOL_INFO_HEADER_CHAT, pool_id, mother_pool_id, mother_pool_id, mother_template.description.c_str(),
                            pool_template.AutoSpawn, pool_template.MaxLimit);
        else
            PSendSysMessage(LANG_POOL_INFO_HEADER_CONSOLE, pool_id, mother_pool_id, mother_template.description.c_str(),
                            pool_template.AutoSpawn, pool_template.MaxLimit);
    }

    PoolGroup<Creature> const& poolCreatures = sPoolMgr.GetPoolCreatures(pool_id);
    SpawnedPoolObjects const* crSpawns = spawns ? &spawns->GetSpawnedCreatures() : NULL;

    PoolObjectList const& poolCreaturesEx = poolCreatures.GetExplicitlyChanced();
    if (!poolCreaturesEx.empty())
    {
        SendSysMessage(LANG_POOL_CHANCE_CREATURE_LIST_HEADER);
        for (PoolObjectList::const_iterator itr = poolCreaturesEx.begin(); itr != poolCreaturesEx.end(); ++itr)
        {
            if (CreatureData const* data = sObjectMgr.GetCreatureData(itr->guid))
            {
                if (CreatureInfo const* info = ObjectMgr::GetCreatureTemplate(data->id))
                {
                    char const* active = crSpawns && crSpawns->find(itr->guid) != crSpawns->end() ? active_str.c_str() : "";
                    if (m_session)
                        PSendSysMessage(LANG_POOL_CHANCE_CREATURE_LIST_CHAT, itr->guid, PrepareStringNpcOrGoSpawnInformation<Creature>(itr->guid).c_str(),
                                        itr->guid, info->Name, data->posX, data->posY, data->posZ, data->mapid, itr->chance, active);
                    else
                        PSendSysMessage(LANG_POOL_CHANCE_CREATURE_LIST_CONSOLE, itr->guid, PrepareStringNpcOrGoSpawnInformation<Creature>(itr->guid).c_str(),
                                        info->Name, data->posX, data->posY, data->posZ, data->mapid, itr->chance, active);
                }
            }
        }
    }

    PoolObjectList const& poolCreaturesEq = poolCreatures.GetEqualChanced();
    if (!poolCreaturesEq.empty())
    {
        SendSysMessage(LANG_POOL_CREATURE_LIST_HEADER);
        for (PoolObjectList::const_iterator itr = poolCreaturesEq.begin(); itr != poolCreaturesEq.end(); ++itr)
        {
            if (CreatureData const* data = sObjectMgr.GetCreatureData(itr->guid))
            {
                if (CreatureInfo const* info = ObjectMgr::GetCreatureTemplate(data->id))
                {
                    char const* active = crSpawns && crSpawns->find(itr->guid) != crSpawns->end() ? active_str.c_str() : "";
                    if (m_session)
                        PSendSysMessage(LANG_POOL_CREATURE_LIST_CHAT, itr->guid, PrepareStringNpcOrGoSpawnInformation<Creature>(itr->guid).c_str(),
                                        itr->guid, info->Name, data->posX, data->posY, data->posZ, data->mapid, active);
                    else
                        PSendSysMessage(LANG_POOL_CREATURE_LIST_CONSOLE, itr->guid, PrepareStringNpcOrGoSpawnInformation<Creature>(itr->guid).c_str(),
                                        info->Name, data->posX, data->posY, data->posZ, data->mapid, active);
                }
            }
        }
    }

    PoolGroup<GameObject> const& poolGameObjects = sPoolMgr.GetPoolGameObjects(pool_id);
    SpawnedPoolObjects const* goSpawns = spawns ? &spawns->GetSpawnedGameobjects() : NULL;

    PoolObjectList const& poolGameObjectsEx = poolGameObjects.GetExplicitlyChanced();
    if (!poolGameObjectsEx.empty())
    {
        SendSysMessage(LANG_POOL_CHANCE_GO_LIST_HEADER);
        for (PoolObjectList::const_iterator itr = poolGameObjectsEx.begin(); itr != poolGameObjectsEx.end(); ++itr)
        {
            if (GameObjectData const* data = sObjectMgr.GetGOData(itr->guid))
            {
                if (GameObjectInfo const* info = ObjectMgr::GetGameObjectInfo(data->id))
                {
                    char const* active = goSpawns && goSpawns->find(itr->guid) != goSpawns->end() ? active_str.c_str() : "";
                    if (m_session)
                        PSendSysMessage(LANG_POOL_CHANCE_GO_LIST_CHAT, itr->guid, PrepareStringNpcOrGoSpawnInformation<GameObject>(itr->guid).c_str(),
                                        itr->guid, info->name, data->posX, data->posY, data->posZ, data->mapid, itr->chance, active);
                    else
                        PSendSysMessage(LANG_POOL_CHANCE_GO_LIST_CONSOLE, itr->guid, PrepareStringNpcOrGoSpawnInformation<GameObject>(itr->guid).c_str(),
                                        info->name, data->posX, data->posY, data->posZ, data->mapid, itr->chance, active);
                }
            }
        }
    }

    PoolObjectList const& poolGameObjectsEq = poolGameObjects.GetEqualChanced();
    if (!poolGameObjectsEq.empty())
    {
        SendSysMessage(LANG_POOL_GO_LIST_HEADER);
        for (PoolObjectList::const_iterator itr = poolGameObjectsEq.begin(); itr != poolGameObjectsEq.end(); ++itr)
        {
            if (GameObjectData const* data = sObjectMgr.GetGOData(itr->guid))
            {
                if (GameObjectInfo const* info = ObjectMgr::GetGameObjectInfo(data->id))
                {
                    char const* active = goSpawns && goSpawns->find(itr->guid) != goSpawns->end() ? active_str.c_str() : "";
                    if (m_session)
                        PSendSysMessage(LANG_POOL_GO_LIST_CHAT, itr->guid, PrepareStringNpcOrGoSpawnInformation<GameObject>(itr->guid).c_str(),
                                        itr->guid, info->name, data->posX, data->posY, data->posZ, data->mapid, active);
                    else
                        PSendSysMessage(LANG_POOL_GO_LIST_CONSOLE, itr->guid, PrepareStringNpcOrGoSpawnInformation<GameObject>(itr->guid).c_str(),
                                        info->name, data->posX, data->posY, data->posZ, data->mapid, active);
                }
            }
        }
    }

    PoolGroup<Pool> const& poolPools = sPoolMgr.GetPoolPools(pool_id);
    SpawnedPoolPools const* poolSpawns = spawns ? &spawns->GetSpawnedPools() : NULL;

    PoolObjectList const& poolPoolsEx = poolPools.GetExplicitlyChanced();
    if (!poolPoolsEx.empty())
    {
        SendSysMessage(LANG_POOL_CHANCE_POOL_LIST_HEADER);
        for (PoolObjectList::const_iterator itr = poolPoolsEx.begin(); itr != poolPoolsEx.end(); ++itr)
        {
            PoolTemplateData const& itr_template = sPoolMgr.GetPoolTemplate(itr->guid);
            char const* active = poolSpawns && poolSpawns->find(itr->guid) != poolSpawns->end() ? active_str.c_str() : "";
            if (m_session)
                PSendSysMessage(LANG_POOL_CHANCE_POOL_LIST_CHAT, itr->guid,
                                itr->guid, itr_template.description.c_str(), itr_template.AutoSpawn ? 1 : 0, itr_template.MaxLimit,
                                sPoolMgr.GetPoolCreatures(itr->guid).size(), sPoolMgr.GetPoolGameObjects(itr->guid).size(), sPoolMgr.GetPoolPools(itr->guid).size(),
                                itr->chance, active);
            else
                PSendSysMessage(LANG_POOL_CHANCE_POOL_LIST_CONSOLE, itr->guid,
                                itr_template.description.c_str(), itr_template.AutoSpawn ? 1 : 0, itr_template.MaxLimit,
                                sPoolMgr.GetPoolCreatures(itr->guid).size(), sPoolMgr.GetPoolGameObjects(itr->guid).size(), sPoolMgr.GetPoolPools(itr->guid).size(),
                                itr->chance, active);
        }
    }

    PoolObjectList const& poolPoolsEq = poolPools.GetEqualChanced();
    if (!poolPoolsEq.empty())
    {
        SendSysMessage(LANG_POOL_POOL_LIST_HEADER);
        for (PoolObjectList::const_iterator itr = poolPoolsEq.begin(); itr != poolPoolsEq.end(); ++itr)
        {
            PoolTemplateData const& itr_template = sPoolMgr.GetPoolTemplate(itr->guid);
            char const* active = poolSpawns && poolSpawns->find(itr->guid) != poolSpawns->end() ? active_str.c_str() : "";
            if (m_session)
                PSendSysMessage(LANG_POOL_POOL_LIST_CHAT, itr->guid,
                                itr->guid, itr_template.description.c_str(), itr_template.AutoSpawn ? 1 : 0, itr_template.MaxLimit,
                                sPoolMgr.GetPoolCreatures(itr->guid).size(), sPoolMgr.GetPoolGameObjects(itr->guid).size(), sPoolMgr.GetPoolPools(itr->guid).size(),
                                active);
            else
                PSendSysMessage(LANG_POOL_POOL_LIST_CONSOLE, itr->guid,
                                itr_template.description.c_str(), itr_template.AutoSpawn ? 1 : 0, itr_template.MaxLimit,
                                sPoolMgr.GetPoolCreatures(itr->guid).size(), sPoolMgr.GetPoolGameObjects(itr->guid).size(), sPoolMgr.GetPoolPools(itr->guid).size(),
                                active);
        }
    }
    return true;
}


bool ChatHandler::HandleRepairitemsCommand(char* args)
{
    Player* target;
    if (!ExtractPlayerTarget(&args, &target))
    {
        return false;
    }

    // check online security
    if (HasLowerSecurity(target))
    {
        return false;
    }

    // Repair items
    target->DurabilityRepairAll(false, 0, false);

    PSendSysMessage(LANG_YOU_REPAIR_ITEMS, GetNameLink(target).c_str());
    if (needReportToTarget(target))
    {
        ChatHandler(target).PSendSysMessage(LANG_YOUR_ITEMS_REPAIRED, GetNameLink().c_str());
    }
    return true;
}


bool ChatHandler::HandleLookupTitleCommand(char* args)
{
    if (!*args)
    {
        return false;
    }

    // can be NULL in console call
    Player* target = getSelectedPlayer();

    // title name have single string arg for player name
    char const* targetName = target ? target->GetName() : "NAME";

    std::string namepart = args;
    std::wstring wnamepart;

    if (!Utf8toWStr(namepart, wnamepart))
    {
        return false;
    }

    // converting string that we try to find to lower case
    wstrToLower(wnamepart);

    uint32 counter = 0;                                     // Counter for figure out that we found smth.

    // Search in CharTitles.dbc
    for (uint32 id = 0; id < sCharTitlesStore.GetNumRows(); ++id)
    {
        CharTitlesEntry const* titleInfo = sCharTitlesStore.LookupEntry(id);
        if (titleInfo)
        {
            int loc = GetSessionDbcLocale();
            std::string name = titleInfo->name[loc];
            if (name.empty())
            {
                continue;
            }

            if (!Utf8FitTo(name, wnamepart))
            {
                loc = 0;
                for (; loc < MAX_LOCALE; ++loc)
                {
                    if (loc == GetSessionDbcLocale())
                    {
                        continue;
                    }

                    name = titleInfo->name[loc];
                    if (name.empty())
                    {
                        continue;
                    }

                    if (Utf8FitTo(name, wnamepart))
                    {
                        break;
                    }
                }
            }

            if (loc < MAX_LOCALE)
            {
                char const* knownStr = target && target->HasTitle(titleInfo) ? GetMangosString(LANG_KNOWN) : "";

                char const* activeStr = target && target->GetUInt32Value(PLAYER_CHOSEN_TITLE) == titleInfo->bit_index
                                        ? GetMangosString(LANG_ACTIVE)
                                        : "";

                char titleNameStr[80];
                snprintf(titleNameStr, 80, name.c_str(), targetName);

                // send title in "id (idx:idx) - [namedlink locale]" format
                if (m_session)
                {
                    PSendSysMessage(LANG_TITLE_LIST_CHAT, id, titleInfo->bit_index, id, titleNameStr, localeNames[loc], knownStr, activeStr);
                }
                else
                {
                    PSendSysMessage(LANG_TITLE_LIST_CONSOLE, id, titleInfo->bit_index, titleNameStr, localeNames[loc], knownStr, activeStr);
                }

                ++counter;
            }
        }
    }
    if (counter == 0)                                       // if counter == 0 then we found nth
    {
        SendSysMessage(LANG_COMMAND_NOTITLEFOUND);
    }
    return true;
}

bool ChatHandler::HandleTitlesAddCommand(char* args)
{
    // number or [name] Shift-click form |color|Htitle:title_id|h[name]|h|r
    uint32 id;
    if (!ExtractUint32KeyFromLink(&args, "Htitle", id))
    {
        return false;
    }

    if (id <= 0)
    {
        PSendSysMessage(LANG_INVALID_TITLE_ID, id);
        SetSentErrorMessage(true);
        return false;
    }

    Player* target = getSelectedPlayer();
    if (!target)
    {
        SendSysMessage(LANG_NO_CHAR_SELECTED);
        SetSentErrorMessage(true);
        return false;
    }

    // check online security
    if (HasLowerSecurity(target))
    {
        return false;
    }

    CharTitlesEntry const* titleInfo = sCharTitlesStore.LookupEntry(id);
    if (!titleInfo)
    {
        PSendSysMessage(LANG_INVALID_TITLE_ID, id);
        SetSentErrorMessage(true);
        return false;
    }

    std::string tNameLink = GetNameLink(target);

    char const* targetName = target->GetName();
    char titleNameStr[80];
    snprintf(titleNameStr, 80, titleInfo->name[GetSessionDbcLocale()], targetName);

    target->SetTitle(titleInfo);
    PSendSysMessage(LANG_TITLE_ADD_RES, id, titleNameStr, tNameLink.c_str());

    return true;
}

bool ChatHandler::HandleTitlesRemoveCommand(char* args)
{
    // number or [name] Shift-click form |color|Htitle:title_id|h[name]|h|r
    uint32 id;
    if (!ExtractUint32KeyFromLink(&args, "Htitle", id))
    {
        return false;
    }

    if (id <= 0)
    {
        PSendSysMessage(LANG_INVALID_TITLE_ID, id);
        SetSentErrorMessage(true);
        return false;
    }

    Player* target = getSelectedPlayer();
    if (!target)
    {
        SendSysMessage(LANG_NO_CHAR_SELECTED);
        SetSentErrorMessage(true);
        return false;
    }

    // check online security
    if (HasLowerSecurity(target))
    {
        return false;
    }

    CharTitlesEntry const* titleInfo = sCharTitlesStore.LookupEntry(id);
    if (!titleInfo)
    {
        PSendSysMessage(LANG_INVALID_TITLE_ID, id);
        SetSentErrorMessage(true);
        return false;
    }

    target->SetTitle(titleInfo, true);

    std::string tNameLink = GetNameLink(target);

    char const* targetName = target->GetName();
    char titleNameStr[80];
    snprintf(titleNameStr, 80, titleInfo->name[GetSessionDbcLocale()], targetName);

    PSendSysMessage(LANG_TITLE_REMOVE_RES, id, titleNameStr, tNameLink.c_str());

    if (!target->HasTitle(target->GetInt32Value(PLAYER_CHOSEN_TITLE)))
    {
        target->SetUInt32Value(PLAYER_CHOSEN_TITLE, 0);
        PSendSysMessage(LANG_CURRENT_TITLE_RESET, tNameLink.c_str());
    }

    return true;
}

// Edit Player KnownTitles
bool ChatHandler::HandleTitlesSetMaskCommand(char* args)
{
    if (!*args)
    {
        return false;
    }

    uint64 titles = 0;

    sscanf(args, UI64FMTD, &titles);

    Player* target = getSelectedPlayer();
    if (!target)
    {
        SendSysMessage(LANG_NO_CHAR_SELECTED);
        SetSentErrorMessage(true);
        return false;
    }

    // check online security
    if (HasLowerSecurity(target))
    {
        return false;
    }

    uint64 titles2 = titles;

    for (uint32 i = 1; i < sCharTitlesStore.GetNumRows(); ++i)
    {
        if (CharTitlesEntry const* tEntry = sCharTitlesStore.LookupEntry(i))
        {
            titles2 &= ~(uint64(1) << tEntry->bit_index);
        }
    }
    titles &= ~titles2;                                     // remove nonexistent titles

    target->SetUInt64Value(PLAYER__FIELD_KNOWN_TITLES, titles);
    SendSysMessage(LANG_DONE);

    if (!target->HasTitle(target->GetInt32Value(PLAYER_CHOSEN_TITLE)))
    {
        target->SetUInt32Value(PLAYER_CHOSEN_TITLE, 0);
        PSendSysMessage(LANG_CURRENT_TITLE_RESET, GetNameLink(target).c_str());
    }

    return true;
}

bool ChatHandler::HandleCharacterTitlesCommand(char* args)
{
    Player* target;
    if (!ExtractPlayerTarget(&args, &target))
    {
        return false;
    }

    LocaleConstant loc = GetSessionDbcLocale();
    char const* targetName = target->GetName();
    char const* knownStr = GetMangosString(LANG_KNOWN);

    // Search in CharTitles.dbc
    for (uint32 id = 0; id < sCharTitlesStore.GetNumRows(); ++id)
    {
        CharTitlesEntry const* titleInfo = sCharTitlesStore.LookupEntry(id);
        if (titleInfo && target->HasTitle(titleInfo))
        {
            std::string name = titleInfo->name[loc];
            if (name.empty())
            {
                continue;
            }

            char const* activeStr = target && target->GetUInt32Value(PLAYER_CHOSEN_TITLE) == titleInfo->bit_index
                                    ? GetMangosString(LANG_ACTIVE)
                                    : "";

            char titleNameStr[80];
            snprintf(titleNameStr, 80, name.c_str(), targetName);

            // send title in "id (idx:idx) - [namedlink locale]" format
            if (m_session)
            {
                PSendSysMessage(LANG_TITLE_LIST_CHAT, id, titleInfo->bit_index, id, titleNameStr, localeNames[loc], knownStr, activeStr);
            }
            else
            {
                PSendSysMessage(LANG_TITLE_LIST_CONSOLE, id, titleInfo->bit_index, name.c_str(), localeNames[loc], knownStr, activeStr);
            }
        }
    }
    return true;
}

bool ChatHandler::HandleTitlesCurrentCommand(char* args)
{
    // number or [name] Shift-click form |color|Htitle:title_id|h[name]|h|r
    uint32 id;
    if (!ExtractUint32KeyFromLink(&args, "Htitle", id))
    {
        return false;
    }

    if (id <= 0)
    {
        PSendSysMessage(LANG_INVALID_TITLE_ID, id);
        SetSentErrorMessage(true);
        return false;
    }

    Player* target = getSelectedPlayer();
    if (!target)
    {
        SendSysMessage(LANG_NO_CHAR_SELECTED);
        SetSentErrorMessage(true);
        return false;
    }

    // check online security
    if (HasLowerSecurity(target))
    {
        return false;
    }

    CharTitlesEntry const* titleInfo = sCharTitlesStore.LookupEntry(id);
    if (!titleInfo)
    {
        PSendSysMessage(LANG_INVALID_TITLE_ID, id);
        SetSentErrorMessage(true);
        return false;
    }

    std::string tNameLink = GetNameLink(target);

    target->SetTitle(titleInfo);                            // to be sure that title now known
    target->SetUInt32Value(PLAYER_CHOSEN_TITLE, titleInfo->bit_index);

    PSendSysMessage(LANG_TITLE_CURRENT_RES, id, titleInfo->name[GetSessionDbcLocale()], tNameLink.c_str());

    return true;
}

bool ChatHandler::HandleMmapPathCommand(char* args)
{
    if (!MMAP::MMapFactory::createOrGetMMapManager()->GetNavMesh(m_session->GetPlayer()->GetMapId()))
    {
        PSendSysMessage("NavMesh not loaded for current map.");
        return true;
    }

    PSendSysMessage("mmap path:");

    // units
    Player* player = m_session->GetPlayer();
    Unit* target = getSelectedUnit();
    if (!player || !target)
    {
        PSendSysMessage("Invalid target/source selection.");
        return true;
    }

    char* para = strtok(args, " ");

    bool useStraightPath = false;
    bool followPath = false;
    bool unitToPlayer = false;
    if (para)
    {
        if (strcmp(para, "go") == 0)
        {
            followPath = true;
            para = strtok(NULL, " ");
            if (para && strcmp(para, "straight") == 0)
            {
                useStraightPath = true;
            }
        }
        else if (strcmp(para, "straight") == 0)
        {
            useStraightPath = true;
        }
        else if (strcmp(para, "to_me") == 0)
        {
            unitToPlayer = true;
        }
        else
        {
            PSendSysMessage("Use '.mmap path go' to move on target.");
            PSendSysMessage("Use '.mmap path straight' to generate straight path.");
            PSendSysMessage("Use '.mmap path to_me' to generate path from the target to you.");
        }
    }

    Unit* destinationUnit;
    Unit* originUnit;
    if (unitToPlayer)
    {
        destinationUnit = player;
        originUnit = target;
    }
    else
    {
        destinationUnit = target;
        originUnit = player;
    }

    // unit locations
    float x, y, z;
    destinationUnit->GetPosition(x, y, z);

    // path
    PathFinder path(originUnit);
    path.setUseStrightPath(useStraightPath);
    path.calculate(x, y, z);

    PointsArray pointPath = path.getPath();
    PSendSysMessage("%s's path to %s:", originUnit->GetName(), destinationUnit->GetName());
    PSendSysMessage("Building %s", useStraightPath ? "StraightPath" : "SmoothPath");
    PSendSysMessage("length " SIZEFMTD " type %u", pointPath.size(), path.getPathType());

    Vector3 start = path.getStartPosition();
    Vector3 end = path.getEndPosition();
    Vector3 actualEnd = path.getActualEndPosition();

    PSendSysMessage("start      (%.3f, %.3f, %.3f)", start.x, start.y, start.z);
    PSendSysMessage("end        (%.3f, %.3f, %.3f)", end.x, end.y, end.z);
    PSendSysMessage("actual end (%.3f, %.3f, %.3f)", actualEnd.x, actualEnd.y, actualEnd.z);

    if (!player->isGameMaster())
    {
        PSendSysMessage("Enable GM mode to see the path points.");
    }

    for (uint32 i = 0; i < pointPath.size(); ++i)
    {
        player->SummonCreature(VISUAL_WAYPOINT, pointPath[i].x, pointPath[i].y, pointPath[i].z, 0, TEMPSUMMON_TIMED_DESPAWN, 9000);
    }

    if (followPath)
    {
        Movement::MoveSplineInit init(*player);
        init.MovebyPath(pointPath);
        init.SetWalk(false);
        init.Launch();
    }

    return true;
}

bool ChatHandler::HandleMmapLocCommand(char* /*args*/)
{
    PSendSysMessage("mmap tileloc:");

    // grid tile location
    Player* player = m_session->GetPlayer();

    int32 gx = 32 - player->GetPositionX() / SIZE_OF_GRIDS;
    int32 gy = 32 - player->GetPositionY() / SIZE_OF_GRIDS;

    PSendSysMessage("%03u%02i%02i.mmtile", player->GetMapId(), gy, gx);
    PSendSysMessage("gridloc [%i,%i]", gx, gy);

    // calculate navmesh tile location
    const dtNavMesh* navmesh = MMAP::MMapFactory::createOrGetMMapManager()->GetNavMesh(player->GetMapId());
    const dtNavMeshQuery* navmeshquery = MMAP::MMapFactory::createOrGetMMapManager()->GetNavMeshQuery(player->GetMapId(), player->GetInstanceId());
    if (!navmesh || !navmeshquery)
    {
        PSendSysMessage("NavMesh not loaded for current map.");
        return true;
    }

    const float* min = navmesh->getParams()->orig;

    float x, y, z;
    player->GetPosition(x, y, z);
    float location[VERTEX_SIZE] = {y, z, x};
    float extents[VERTEX_SIZE] = {3.0f, 5.0f, 3.0f};

    int32 tilex = int32((y - min[0]) / SIZE_OF_GRIDS);
    int32 tiley = int32((x - min[2]) / SIZE_OF_GRIDS);

    PSendSysMessage("Calc   [%02i,%02i]", tilex, tiley);

    // navmesh poly -> navmesh tile location
    dtQueryFilter filter = dtQueryFilter();
    dtPolyRef polyRef = INVALID_POLYREF;
    navmeshquery->findNearestPoly(location, extents, &filter, &polyRef, NULL);

    if (polyRef == INVALID_POLYREF)
    {
        PSendSysMessage("Dt     [??,??] (invalid poly, probably no tile loaded)");
    }
    else
    {
        const dtMeshTile* tile;
        const dtPoly* poly;
        dtStatus dtResult = navmesh->getTileAndPolyByRef(polyRef, &tile, &poly);
        if ((dtStatusSucceed(dtResult)) && tile)
        {
            PSendSysMessage("Dt     [%02i,%02i]", tile->header->x, tile->header->y);
        }
        else
        {
            PSendSysMessage("Dt     [??,??] (no tile loaded)");
        }
    }

    return true;
}

bool ChatHandler::HandleMmapLoadedTilesCommand(char* /*args*/)
{
    uint32 mapid = m_session->GetPlayer()->GetMapId();

    const dtNavMesh* navmesh = MMAP::MMapFactory::createOrGetMMapManager()->GetNavMesh(mapid);
    const dtNavMeshQuery* navmeshquery = MMAP::MMapFactory::createOrGetMMapManager()->GetNavMeshQuery(mapid, m_session->GetPlayer()->GetInstanceId());
    if (!navmesh || !navmeshquery)
    {
        PSendSysMessage("NavMesh not loaded for current map.");
        return true;
    }

    PSendSysMessage("mmap loadedtiles:");

    for (int32 i = 0; i < navmesh->getMaxTiles(); ++i)
    {
        const dtMeshTile* tile = navmesh->getTile(i);
        if (!tile || !tile->header)
        {
            continue;
        }

        PSendSysMessage("[%02i,%02i]", tile->header->x, tile->header->y);
    }

    return true;
}

bool ChatHandler::HandleMmapStatsCommand(char* /*args*/)
{
    PSendSysMessage("mmap stats:");
    PSendSysMessage("  global mmap pathfinding is %sabled", sWorld.getConfig(CONFIG_BOOL_MMAP_ENABLED) ? "en" : "dis");

    MMAP::MMapManager* manager = MMAP::MMapFactory::createOrGetMMapManager();
    PSendSysMessage(" %u maps loaded with %u tiles overall", manager->getLoadedMapsCount(), manager->getLoadedTilesCount());

    const dtNavMesh* navmesh = manager->GetNavMesh(m_session->GetPlayer()->GetMapId());
    if (!navmesh)
    {
        PSendSysMessage("NavMesh not loaded for current map.");
        return true;
    }

    uint32 tileCount = 0;
    uint32 nodeCount = 0;
    uint32 polyCount = 0;
    uint32 vertCount = 0;
    uint32 triCount = 0;
    uint32 triVertCount = 0;
    uint32 dataSize = 0;
    for (int32 i = 0; i < navmesh->getMaxTiles(); ++i)
    {
        const dtMeshTile* tile = navmesh->getTile(i);
        if (!tile || !tile->header)
        {
            continue;
        }

        tileCount ++;
        nodeCount += tile->header->bvNodeCount;
        polyCount += tile->header->polyCount;
        vertCount += tile->header->vertCount;
        triCount += tile->header->detailTriCount;
        triVertCount += tile->header->detailVertCount;
        dataSize += tile->dataSize;
    }

    PSendSysMessage("Navmesh stats on current map:");
    PSendSysMessage(" %u tiles loaded", tileCount);
    PSendSysMessage(" %u BVTree nodes", nodeCount);
    PSendSysMessage(" %u polygons (%u vertices)", polyCount, vertCount);
    PSendSysMessage(" %u triangles (%u vertices)", triCount, triVertCount);
    PSendSysMessage(" %.2f MB of data (not including pointers)", ((float)dataSize / sizeof(unsigned char)) / 1048576);

    return true;
}
