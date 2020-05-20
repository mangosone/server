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
#include "WorldSession.h"
#include "World.h"
#include "ObjectMgr.h"
#include "AccountMgr.h"
#include "PlayerDump.h"
#include "SpellMgr.h"
#include "Player.h"
#include "GameObject.h"
#include "Chat.h"
#include "Log.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "ObjectAccessor.h"
#include "MapManager.h"
#include "MassMailMgr.h"
#include "ScriptMgr.h"
#include "Language.h"
#include "GridNotifiersImpl.h"
#include "CellImpl.h"
#include "Weather.h"
#include "PointMovementGenerator.h"
#include "PathFinder.h"
#include "TargetedMovementGenerator.h"
#include "SkillDiscovery.h"
#include "SkillExtraItems.h"
#include "SystemConfig.h"
#include "Config/Config.h"
#include "Mail.h"
#include "Util.h"
#include "ItemEnchantmentMgr.h"
#include "BattleGround/BattleGroundMgr.h"
#include "MapPersistentStateMgr.h"
#include "InstanceData.h"
#include "DBCStores.h"
#include "CreatureEventAIMgr.h"
#include "AuctionHouseBot/AuctionHouseBot.h"
#include "SQLStorages.h"
#include "DisableMgr.h"

static uint32 ahbotQualityIds[MAX_AUCTION_QUALITY] =
{
    LANG_AHBOT_QUALITY_GREY, LANG_AHBOT_QUALITY_WHITE,
    LANG_AHBOT_QUALITY_GREEN, LANG_AHBOT_QUALITY_BLUE,
    LANG_AHBOT_QUALITY_PURPLE, LANG_AHBOT_QUALITY_ORANGE,
    LANG_AHBOT_QUALITY_YELLOW
};



// reload commands
void ChatHandler::ShowSpellListHelper(Player* target, SpellEntry const* spellInfo, LocaleConstant loc)
{
    uint32 id = spellInfo->Id;

    bool known = target && target->HasSpell(id);
    bool learn = (spellInfo->Effect[EFFECT_INDEX_0] == SPELL_EFFECT_LEARN_SPELL);

    uint32 talentCost = GetTalentSpellCost(id);

    bool talent = (talentCost > 0);
    bool passive = IsPassiveSpell(spellInfo);
    bool active = target && target->HasAura(id);

    // unit32 used to prevent interpreting uint8 as char at output
    // find rank of learned spell for learning spell, or talent rank
    uint32 rank = talentCost ? talentCost : sSpellMgr.GetSpellRank(learn ? spellInfo->EffectTriggerSpell[EFFECT_INDEX_0] : id);

    // send spell in "id - [name, rank N] [talent] [passive] [learn] [known]" format
    std::ostringstream ss;
    if (m_session)
    {
        ss << id << " - |cffffffff|Hspell:" << id << "|h[" << spellInfo->SpellName[loc];
    }
    else
    {
        ss << id << " - " << spellInfo->SpellName[loc];
    }

    // include rank in link name
    if (rank)
    {
        ss << GetMangosString(LANG_SPELL_RANK) << rank;
    }

    if (m_session)
    {
        ss << " " << localeNames[loc] << "]|h|r";
    }
    else
    {
        ss << " " << localeNames[loc];
    }

    if (talent)
    {
        ss << GetMangosString(LANG_TALENT);
    }
    if (passive)
    {
        ss << GetMangosString(LANG_PASSIVE);
    }
    if (learn)
    {
        ss << GetMangosString(LANG_LEARN);
    }
    if (known)
    {
        ss << GetMangosString(LANG_KNOWN);
    }
    if (active)
    {
        ss << GetMangosString(LANG_ACTIVE);
    }

    SendSysMessage(ss.str().c_str());
}



/** \brief GM command level 3 - Create a guild.
 *
 * This command allows a GM (level 3) to create a guild.
 *
 * The "args" parameter contains the name of the guild leader
 * and then the name of the guild.
 *
 */



bool ChatHandler::HandleModifyArenaCommand(char* args)
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

    int32 amount = (int32)atoi(args);

    target->ModifyArenaPoints(amount);

    PSendSysMessage(LANG_COMMAND_MODIFY_ARENA, GetNameLink(target).c_str(), target->GetArenaPoints());

    return true;
}


/*
ComeToMe command REQUIRED for 3rd party scripting library to have access to PointMovementGenerator
Without this function 3rd party scripting library will get linking errors (unresolved external)
when attempting to use the PointMovementGenerator
*/

bool ChatHandler::ShowPlayerListHelper(QueryResult* result, uint32* limit, bool title, bool error)
{
    if (!result)
    {
        if (error)
        {
            PSendSysMessage(LANG_NO_PLAYERS_FOUND);
            SetSentErrorMessage(true);
        }
        return false;
    }

    if (!m_session && title)
    {
        SendSysMessage(LANG_CHARACTERS_LIST_BAR);
        SendSysMessage(LANG_CHARACTERS_LIST_HEADER);
        SendSysMessage(LANG_CHARACTERS_LIST_BAR);
    }

    if (result)
    {
        ///- Circle through them. Display username and GM level
        do
        {
            // check limit
            if (limit)
            {
                if (*limit == 0)
                {
                    break;
                }
                --*limit;
            }

            Field* fields = result->Fetch();
            uint32 guid      = fields[0].GetUInt32();
            std::string name = fields[1].GetCppString();
            uint8 race       = fields[2].GetUInt8();
            uint8 class_     = fields[3].GetUInt8();
            uint32 level     = fields[4].GetUInt32();

            ChrRacesEntry const* raceEntry = sChrRacesStore.LookupEntry(race);
            ChrClassesEntry const* classEntry = sChrClassesStore.LookupEntry(class_);

            char const* race_name = raceEntry   ? raceEntry->name[GetSessionDbcLocale()] : "<?>";
            char const* class_name = classEntry ? classEntry->name[GetSessionDbcLocale()] : "<?>";

            if (!m_session)
            {
                PSendSysMessage(LANG_CHARACTERS_LIST_LINE_CONSOLE, guid, name.c_str(), race_name, class_name, level);
            }
            else
            {
                PSendSysMessage(LANG_CHARACTERS_LIST_LINE_CHAT, guid, name.c_str(), name.c_str(), race_name, class_name, level);
            }
        }
        while (result->NextRow());

        delete result;
    }

    if (!m_session)
    {
        SendSysMessage(LANG_CHARACTERS_LIST_BAR);
    }

    return true;
}


bool ChatHandler::HandleFlushArenaPointsCommand(char* /*args*/)
{
    sBattleGroundMgr.DistributeArenaPoints();
    return true;
}

