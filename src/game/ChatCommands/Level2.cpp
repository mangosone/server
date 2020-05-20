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



static char const* const areatriggerKeys[] =
{
    "Hareatrigger",
    "Hareatrigger_target",
    NULL
};


enum CreatureLinkType
{
    CREATURE_LINK_RAW = -1,                   // non-link case
    CREATURE_LINK_GUID = 0,
    CREATURE_LINK_ENTRY = 1,
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

enum GameobjectLinkType
{
    GAMEOBJECT_LINK_RAW = -1,                   // non-link case
    GAMEOBJECT_LINK_GUID = 0,
    GAMEOBJECT_LINK_ENTRY = 1,
};

static char const* const gameobjectKeys[] =
{
    "Hgameobject",
    "Hgameobject_entry",
    NULL
};




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



// move item to other slot



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

