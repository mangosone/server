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

#include "ObjectMgr.h"
#include "LivingWorldAnchorPolicy.h"
#include "MotionGenerators/MotionMaster.h"  // WAYPOINT_MOTION_TYPE
#include "Database/DatabaseEnv.h"
#include "Policies/Singleton.h"

#include "SQLStorages.h"
#include "Log.h"
#include "MapManager.h"
#include "ObjectGuid.h"
#include "ScriptMgr.h"
#include "SpellMgr.h"
#include "World.h"
#include "Group.h"
#include "ArenaTeam.h"
#include "Transports.h"
#include "ProgressBar.h"
#include "Language.h"
#include "PoolManager.h"
#include "GameEventMgr.h"
#include "Chat.h"
#include "MapPersistentStateMgr.h"
#include "SpellAuras.h"
#include "Util.h"
#include "GossipDef.h"
#include "Mail.h"
#include "Formulas.h"
#include "InstanceData.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "CellImpl.h"
#include "DisableMgr.h"

#include "ItemEnchantmentMgr.h"
#include <limits>
#include <set>

INSTANTIATE_SINGLETON_1(ObjectMgr);

// Temporary startup accumulator for LivingWorld observability (written during LoadActiveEntities(NULL) only)
static ObjectMgr::LivingWorldStartupStats s_livingWorldStats;
static bool s_livingWorldStartupPass = false;

/**
 * @brief Normalizes a player name to the server's canonical casing.
 *
 * @param name The player name to normalize.
 * @return true if the name was normalized successfully; otherwise false.
 */
bool normalizePlayerName(std::string& name)
{
    if (name.empty())
    {
        return false;
    }

    wchar_t wstr_buf[MAX_INTERNAL_PLAYER_NAME + 1];
    size_t wstr_len = MAX_INTERNAL_PLAYER_NAME;

    if (!Utf8toWStr(name, &wstr_buf[0], wstr_len))
    {
        return false;
    }

    wstr_buf[0] = wcharToUpper(wstr_buf[0]);
    for (size_t i = 1; i < wstr_len; ++i)
    {
        wstr_buf[i] = wcharToLower(wstr_buf[i]);
    }

    if (!WStrToUtf8(wstr_buf, wstr_len, name))
    {
        return false;
    }

    return true;
}

LanguageDesc lang_description[LANGUAGES_COUNT] =
{
    { LANG_ADDON,           0, 0                       },
    { LANG_UNIVERSAL,       0, 0                       },
    { LANG_ORCISH,        669, SKILL_LANG_ORCISH       },
    { LANG_DARNASSIAN,    671, SKILL_LANG_DARNASSIAN   },
    { LANG_TAURAHE,       670, SKILL_LANG_TAURAHE      },
    { LANG_DWARVISH,      672, SKILL_LANG_DWARVEN      },
    { LANG_COMMON,        668, SKILL_LANG_COMMON       },
    { LANG_DEMONIC,       815, SKILL_LANG_DEMON_TONGUE },
    { LANG_TITAN,         816, SKILL_LANG_TITAN        },
    { LANG_THALASSIAN,    813, SKILL_LANG_THALASSIAN   },
    { LANG_DRACONIC,      814, SKILL_LANG_DRACONIC     },
    { LANG_KALIMAG,       817, SKILL_LANG_OLD_TONGUE   },
    { LANG_GNOMISH,      7340, SKILL_LANG_GNOMISH      },
    { LANG_TROLL,        7341, SKILL_LANG_TROLL        },
    { LANG_GUTTERSPEAK, 17737, SKILL_LANG_GUTTERSPEAK  },
    { LANG_DRAENEI,     29932, SKILL_LANG_DRAENEI      },
    { LANG_ZOMBIE,          0, 0                       },
    { LANG_GNOMISH_BINARY,  0, 0                       },
    { LANG_GOBLIN_BINARY,   0, 0                       }
};

/**
 * @brief Looks up language metadata by language id.
 *
 * @param lang The language id.
 * @return LanguageDesc const* The matching language descriptor, or null if not found.
 */
LanguageDesc const* GetLanguageDescByID(uint32 lang)
{
    for (int i = 0; i < LANGUAGES_COUNT; ++i)
    {
        if (uint32(lang_description[i].lang_id) == lang)
        {
            return &lang_description[i];
        }
    }

    return NULL;
}

/**
 * @brief Checks whether a player satisfies spell-click interaction requirements.
 *
 * @param player The player attempting the interaction.
 * @param clickedCreature The clicked creature.
 * @return true if the interaction requirements are met; otherwise, false.
 */
bool SpellClickInfo::IsFitToRequirements(Player const* player, Creature const* clickedCreature) const
{
    if (conditionId)
    {
        return sObjectMgr.IsPlayerMeetToCondition(conditionId, player, player->GetMap(), clickedCreature, CONDITION_FROM_SPELLCLICK);
    }

    if (questStart)
    {
        // not in expected required quest state
        if (!player || ((!questStartCanActive || !player->IsActiveQuest(questStart)) && !player->GetQuestRewardStatus(questStart)))
        {
            return false;
        }
    }

    if (questEnd)
    {
        // not in expected forbidden quest state
        if (!player || player->GetQuestRewardStatus(questEnd))
        {
            return false;
        }
    }

    return true;
}

template<typename T>
/**
 * @brief Generates the next identifier from the typed guid generator.
 *
 * @return T The next generated identifier value.
 */
T IdGenerator<T>::Generate()
{
    if (m_nextGuid >= std::numeric_limits<T>::max() - 1)
    {
        sLog.outError("%s guid overflow!! Can't continue, shutting down server. ", m_name);
        World::StopNow(ERROR_EXIT_CODE);
    }
    return m_nextGuid++;
}

template uint32 IdGenerator<uint32>::Generate();
template uint64 IdGenerator<uint64>::Generate();

/**
 * @brief Initializes the global object manager.
 */
ObjectMgr::ObjectMgr() :
    m_ArenaTeamIds("Arena team ids"),
    m_AuctionIds("Auction ids"),
    m_GuildIds("Guild ids"),
    m_MailIds("Mail ids"),
    m_PetNumbers("Pet numbers"),
    m_FirstTemporaryCreatureGuid(1),
    m_FirstTemporaryGameObjectGuid(1),
    DBCLocaleIndex(LOCALE_enUS)
{
}

/**
 * @brief Releases dynamically allocated object manager resources.
 */
ObjectMgr::~ObjectMgr()
{
    for (QuestMap::iterator i = mQuestTemplates.begin(); i != mQuestTemplates.end(); ++i)
    {
        delete i->second;
    }

    for (PetLevelInfoMap::iterator i = petInfo.begin(); i != petInfo.end(); ++i)
    {
        delete[] i->second;
    }

    // free only if loaded
    for (int class_ = 0; class_ < MAX_CLASSES; ++class_)
    {
        delete[] playerClassInfo[class_].levelInfo;
    }

    for (int race = 0; race < MAX_RACES; ++race)
    {
        for (int class_ = 0; class_ < MAX_CLASSES; ++class_)
        {
            delete[] playerInfo[race][class_].levelInfo;
        }
    }

    // free objects
    for (GroupMap::iterator itr = mGroupMap.begin(); itr != mGroupMap.end(); ++itr)
    {
        delete itr->second;
    }

    for (ArenaTeamMap::iterator itr = mArenaTeamMap.begin(); itr != mArenaTeamMap.end(); ++itr)
    {
        delete itr->second;
    }

    for (CacheVendorItemMap::iterator itr = m_mCacheVendorTemplateItemMap.begin(); itr != m_mCacheVendorTemplateItemMap.end(); ++itr)
    {
        itr->second.Clear();
    }

    for (CacheVendorItemMap::iterator itr = m_mCacheVendorItemMap.begin(); itr != m_mCacheVendorItemMap.end(); ++itr)
    {
        itr->second.Clear();
    }

    for (CacheTrainerSpellMap::iterator itr = m_mCacheTrainerSpellMap.begin(); itr != m_mCacheTrainerSpellMap.end(); ++itr)
    {
        itr->second.Clear();
    }
}

/**
 * @brief Finds a loaded group by its identifier.
 *
 * @param id The group id.
 * @return The matching group, or null if not found.
 */
Group* ObjectMgr::GetGroupById(uint32 id) const
{
    GroupMap::const_iterator itr = mGroupMap.find(id);
    if (itr != mGroupMap.end())
    {
        return itr->second;
    }

    return NULL;
}

ArenaTeam* ObjectMgr::GetArenaTeamById(uint32 arenateamid) const
{
    ArenaTeamMap::const_iterator itr = mArenaTeamMap.find(arenateamid);
    if (itr != mArenaTeamMap.end())
    {
        return itr->second;
    }

    return NULL;
}

ArenaTeam* ObjectMgr::GetArenaTeamByName(const std::string& arenateamname) const
{
    for (ArenaTeamMap::const_iterator itr = mArenaTeamMap.begin(); itr != mArenaTeamMap.end(); ++itr)
    {
        if (itr->second->GetName() == arenateamname)
        {
            return itr->second;
        }
    }

    return NULL;
}

ArenaTeam* ObjectMgr::GetArenaTeamByCaptain(ObjectGuid guid) const
{
    for (ArenaTeamMap::const_iterator itr = mArenaTeamMap.begin(); itr != mArenaTeamMap.end(); ++itr)
    {
        if (itr->second->GetCaptainGuid() == guid)
        {
            return itr->second;
        }
    }

    return NULL;
}

/**
 * @brief Stores a localized string in a locale-indexed vector.
 *
 * @param s The localized string value.
 * @param locale The locale index.
 * @param data The destination locale vector.
 */
void ObjectMgr::AddLocaleString(std::string const& s, LocaleConstant locale, StringVector& data)
{
    if (!s.empty())
    {
        if (data.size() <= size_t(locale))
        {
            data.resize(locale + 1);
        }
        data[locale] = s;
    }
}

/**
 * @brief Loads localized creature names and subnames from the database.
 */
void ObjectMgr::LoadCreatureLocales()
{
    mCreatureLocaleMap.clear();                             // need for reload case

    QueryResult* result = WorldDatabase.Query("SELECT `entry`,`name_loc1`,`subname_loc1`,`name_loc2`,`subname_loc2`,`name_loc3`,`subname_loc3`,`name_loc4`,`subname_loc4`,`name_loc5`,`subname_loc5`,`name_loc6`,`subname_loc6`,`name_loc7`,`subname_loc7`,`name_loc8`,`subname_loc8` FROM `locales_creature`");

    if (!result)
    {
        BarGoLink bar(1);
        bar.step();
        sLog.outString(">> Loaded 0 creature locale strings. DB table `locales_creature` is empty.");
        return;
    }

    BarGoLink bar(result->GetRowCount());

    do
    {
        Field* fields = result->Fetch();
        bar.step();

        uint32 entry = fields[0].GetUInt32();

        if (!GetCreatureTemplate(entry))
        {
            ERROR_DB_STRICT_LOG("Table `locales_creature` has data for not existed creature entry %u, skipped.", entry);
            continue;
        }

        CreatureLocale& data = mCreatureLocaleMap[entry];

        for (int i = 1; i < MAX_LOCALE; ++i)
        {
            std::string str = fields[1 + 2 * (i - 1)].GetCppString();
            if (!str.empty())
            {
                int idx = GetOrNewIndexForLocale(LocaleConstant(i));
                if (idx >= 0)
                {
                    if ((int32)data.Name.size() <= idx)
                    {
                        data.Name.resize(idx + 1);
                    }

                    data.Name[idx] = str;
                }
            }
            str = fields[1 + 2 * (i - 1) + 1].GetCppString();
            if (!str.empty())
            {
                int idx = GetOrNewIndexForLocale(LocaleConstant(i));
                if (idx >= 0)
                {
                    if ((int32)data.SubName.size() <= idx)
                    {
                        data.SubName.resize(idx + 1);
                    }

                    data.SubName[idx] = str;
                }
            }
        }
    }
    while (result->NextRow());

    delete result;

    sLog.outString(">> Loaded %zu creature locale strings", mCreatureLocaleMap.size());
    sLog.outString();
}

/**
 * @brief Loads localized gossip menu option and confirmation box text.
 */
void ObjectMgr::LoadGossipMenuItemsLocales()
{
    mGossipMenuItemsLocaleMap.clear();                      // need for reload case

    QueryResult* result = WorldDatabase.Query("SELECT `menu_id`,`id`,"
                          "`option_text_loc1`,`box_text_loc1`,`option_text_loc2`,`box_text_loc2`,"
                          "`option_text_loc3`,`box_text_loc3`,`option_text_loc4`,`box_text_loc4`,"
                          "`option_text_loc5`,`box_text_loc5`,`option_text_loc6`,`box_text_loc6`,"
                          "`option_text_loc7`,`box_text_loc7`,`option_text_loc8`,`box_text_loc8` "
                          "FROM `locales_gossip_menu_option`");

    if (!result)
    {
        BarGoLink bar(1);

        bar.step();

        sLog.outString();
        sLog.outString(">> Loaded 0 gossip_menu_option locale strings. DB table `locales_gossip_menu_option` is empty.");
        return;
    }

    BarGoLink bar(result->GetRowCount());

    do
    {
        Field* fields = result->Fetch();
        bar.step();

        uint16 menuId   = fields[0].GetUInt16();
        uint16 id       = fields[1].GetUInt16();

        GossipMenuItemsMapBounds bounds = GetGossipMenuItemsMapBounds(menuId);

        bool found = false;
        if (bounds.first != bounds.second)
        {
            for (GossipMenuItemsMap::const_iterator itr = bounds.first; itr != bounds.second; ++itr)
            {
                if (itr->second.id == id)
                {
                    found = true;
                    break;
                }
            }
        }

        if (!found)
        {
            ERROR_DB_STRICT_LOG("Table `locales_gossip_menu_option` has data for nonexistent gossip menu %u item %u, skipped.", menuId, id);
            continue;
        }

        GossipMenuItemsLocale& data = mGossipMenuItemsLocaleMap[MAKE_PAIR32(menuId, id)];

        for (int i = 1; i < MAX_LOCALE; ++i)
        {
            std::string str = fields[2 + 2 * (i - 1)].GetCppString();
            if (!str.empty())
            {
                int idx = GetOrNewIndexForLocale(LocaleConstant(i));
                if (idx >= 0)
                {
                    if ((int32)data.OptionText.size() <= idx)
                    {
                        data.OptionText.resize(idx + 1);
                    }

                    data.OptionText[idx] = str;
                }
            }
            str = fields[2 + 2 * (i - 1) + 1].GetCppString();
            if (!str.empty())
            {
                int idx = GetOrNewIndexForLocale(LocaleConstant(i));
                if (idx >= 0)
                {
                    if ((int32)data.BoxText.size() <= idx)
                    {
                        data.BoxText.resize(idx + 1);
                    }

                    data.BoxText[idx] = str;
                }
            }
        }
    }
    while (result->NextRow());

    delete result;

    sLog.outString(">> Loaded %zu gossip_menu_option locale strings", mGossipMenuItemsLocaleMap.size());
    sLog.outString();
}

/**
 * @brief Loads localized point-of-interest icon names.
 */
void ObjectMgr::LoadPointOfInterestLocales()
{
    mPointOfInterestLocaleMap.clear();                      // need for reload case

    QueryResult* result = WorldDatabase.Query("SELECT `entry`,`icon_name_loc1`,`icon_name_loc2`,`icon_name_loc3`,`icon_name_loc4`,`icon_name_loc5`,`icon_name_loc6`,`icon_name_loc7`,`icon_name_loc8` FROM `locales_points_of_interest`");

    if (!result)
    {
        BarGoLink bar(1);
        bar.step();
        sLog.outString(">> Loaded 0 points_of_interest locale strings. DB table `locales_points_of_interest` is empty.");
        return;
    }

    BarGoLink bar(result->GetRowCount());

    do
    {
        Field* fields = result->Fetch();
        bar.step();

        uint32 entry = fields[0].GetUInt32();

        if (!GetPointOfInterest(entry))
        {
            ERROR_DB_STRICT_LOG("Table `locales_points_of_interest` has data for nonexistent POI entry %u, skipped.", entry);
            continue;
        }

        PointOfInterestLocale& data = mPointOfInterestLocaleMap[entry];

        for (int i = 1; i < MAX_LOCALE; ++i)
        {
            std::string str = fields[i].GetCppString();
            if (str.empty())
            {
                continue;
            }

            int idx = GetOrNewIndexForLocale(LocaleConstant(i));
            if (idx >= 0)
            {
                if ((int32)data.IconName.size() <= idx)
                {
                    data.IconName.resize(idx + 1);
                }

                data.IconName[idx] = str;
            }
        }
    }
    while (result->NextRow());

    delete result;

    sLog.outString(">> Loaded %zu points_of_interest locale strings", mPointOfInterestLocaleMap.size());
    sLog.outString();
}

// name must be checked to correctness (if received) before call this function
ObjectGuid ObjectMgr::GetPlayerGuidByName(std::string name) const
{
    ObjectGuid guid;

    CharacterDatabase.escape_string(name);

    // Player name safe to sending to DB (checked at login) and this function using
    QueryResult* result = CharacterDatabase.PQuery("SELECT `guid` FROM `characters` WHERE `name` = '%s'", name.c_str());
    if (result)
    {
        guid = ObjectGuid(HIGHGUID_PLAYER, (*result)[0].GetUInt32());

        delete result;
    }

    return guid;
}

/**
 * @brief Resolves a player name from a player GUID.
 *
 * @param guid The player GUID.
 * @param name Receives the resolved player name.
 * @return true if the player name was found; otherwise, false.
 */
bool ObjectMgr::GetPlayerNameByGUID(ObjectGuid guid, std::string& name) const
{
    // prevent DB access for online player
    if (Player* player = GetPlayer(guid))
    {
        name = player->GetName();
        return true;
    }

    uint32 lowguid = guid.GetCounter();

    QueryResult* result = CharacterDatabase.PQuery("SELECT `name` FROM `characters` WHERE `guid` = '%u'", lowguid);

    if (result)
    {
        name = (*result)[0].GetCppString();
        delete result;
        return true;
    }

    return false;
}

/**
 * @brief Resolves a player's team from a player GUID.
 *
 * @param guid The player GUID.
 * @return The player's team, or TEAM_NONE if unavailable.
 */
Team ObjectMgr::GetPlayerTeamByGUID(ObjectGuid guid) const
{
    // prevent DB access for online player
    if (Player* player = GetPlayer(guid))
    {
        return Player::TeamForRace(player->getRace());
    }

    uint32 lowguid = guid.GetCounter();

    QueryResult* result = CharacterDatabase.PQuery("SELECT `race` FROM `characters` WHERE `guid` = '%u'", lowguid);

    if (result)
    {
        uint8 race = (*result)[0].GetUInt8();
        delete result;
        return Player::TeamForRace(race);
    }

    return TEAM_NONE;
}

/**
 * @brief Resolves an account id from a player GUID.
 *
 * @param guid The player GUID.
 * @return The account id, or 0 if unavailable.
 */
uint32 ObjectMgr::GetPlayerAccountIdByGUID(ObjectGuid guid) const
{
    if (!guid.IsPlayer())
    {
        return 0;
    }

    // prevent DB access for online player
    if (Player* player = GetPlayer(guid))
    {
        return player->GetSession()->GetAccountId();
    }

    uint32 lowguid = guid.GetCounter();

    QueryResult* result = CharacterDatabase.PQuery("SELECT `account` FROM `characters` WHERE `guid` = '%u'", lowguid);
    if (result)
    {
        uint32 acc = (*result)[0].GetUInt32();
        delete result;
        return acc;
    }

    return 0;
}

/**
 * @brief Resolves an account id from a player name.
 *
 * @param name The player name.
 * @return The account id, or 0 if unavailable.
 */
uint32 ObjectMgr::GetPlayerAccountIdByPlayerName(const std::string& name) const
{
    QueryResult* result = CharacterDatabase.PQuery("SELECT `account` FROM `characters` WHERE `name` = '%s'", name.c_str());
    if (result)
    {
        uint32 acc = (*result)[0].GetUInt32();
        delete result;
        return acc;
    }

    return 0;
}

/* ********************************************************************************************* */
/* *                                Static Wrappers                                              */
/* ********************************************************************************************* */

/**
 * @brief Gets static gameobject template data by entry id.
 *
 * @param id The gameobject entry id.
 * @return The gameobject template, or null if missing.
 */
GameObjectInfo const* ObjectMgr::GetGameObjectInfo(uint32 id) { return sGOStorage.LookupEntry<GameObjectInfo>(id); }

/**
 * @brief Finds an online player by name.
 *
 * @param name The player name.
 * @return The matching player, or null if not found.
 */
Player* ObjectMgr::GetPlayer(const char* name) { return sObjectAccessor.FindPlayerByName(name); }

/**
 * @brief Finds a player by GUID.
 *
 * @param guid The player GUID.
 * @param inWorld true to restrict the search to players currently in world.
 * @return The matching player, or null if not found.
 */
Player* ObjectMgr::GetPlayer(ObjectGuid guid, bool inWorld /*=true*/) { return sObjectAccessor.FindPlayer(guid, inWorld); }

/**
 * @brief Gets static creature template data by entry id.
 *
 * @param id The creature entry id.
 * @return The creature template, or null if missing.
 */
CreatureInfo const* ObjectMgr::GetCreatureTemplate(uint32 id) { return sCreatureStorage.LookupEntry<CreatureInfo>(id); }

/**
 * @brief Gets creature model metadata by display id.
 *
 * @param modelid The creature model id.
 * @return The creature model info, or null if missing.
 */
CreatureModelInfo const* ObjectMgr::GetCreatureModelInfo(uint32 modelid) { return sCreatureModelStorage.LookupEntry<CreatureModelInfo>(modelid); }

/**
 * @brief Gets equipment template data by entry id.
 *
 * @param entry The equipment template entry id.
 * @return The equipment template, or null if missing.
 */
EquipmentInfo const* ObjectMgr::GetEquipmentInfo(uint32 entry) { return sEquipmentStorage.LookupEntry<EquipmentInfo>(entry); }

/**
 * @brief Gets raw deprecated equipment template data by entry id.
 *
 * @param entry The raw equipment template entry id.
 * @return The raw equipment info, or null if missing.
 */
EquipmentInfoRaw const* ObjectMgr::GetEquipmentInfoRaw(uint32 entry) { return sEquipmentStorageRaw.LookupEntry<EquipmentInfoRaw>(entry); }

/**
 * @brief Gets creature spawn addon data by low GUID.
 *
 * @param lowguid The creature spawn low GUID.
 * @return The addon data, or null if missing.
 */
CreatureDataAddon const* ObjectMgr::GetCreatureAddon(uint32 lowguid) { return sCreatureDataAddonStorage.LookupEntry<CreatureDataAddon>(lowguid); }

/**
 * @brief Gets creature template addon data by entry id.
 *
 * @param entry The creature entry id.
 * @return The template addon data, or null if missing.
 */
CreatureDataAddon const* ObjectMgr::GetCreatureTemplateAddon(uint32 entry) { return sCreatureInfoAddonStorage.LookupEntry<CreatureDataAddon>(entry); }

/**
 * @brief Gets item prototype data by entry id.
 *
 * @param id The item entry id.
 * @return The item prototype, or null if missing.
 */
ItemPrototype const* ObjectMgr::GetItemPrototype(uint32 id) { return sItemStorage.LookupEntry<ItemPrototype>(id); }

/**
 * @brief Gets instance template data by map id.
 *
 * @param map The instance map id.
 * @return The instance template, or null if missing.
 */
InstanceTemplate const* ObjectMgr::GetInstanceTemplate(uint32 map) { return sInstanceTemplate.LookupEntry<InstanceTemplate>(map); }

/* ********************************************************************************************* */
/* *                                Loading Functions                                            */
/* ********************************************************************************************* */
void ObjectMgr::LoadArenaTeams()
{
    uint32 count = 0;

    //                                                     0                          1      2             3      4                 5
    QueryResult* result = CharacterDatabase.Query("SELECT `arena_team`.`arenateamid`,`name`,`captainguid`,`type`,`BackgroundColor`,`EmblemStyle`,"
                          //   6          7             8              9        10           11          12             13            14
                          "`EmblemColor`,`BorderStyle`,`BorderColor`, `rating`,`games_week`,`wins_week`,`games_season`,`wins_season`,`rank` "
                          "FROM `arena_team` LEFT JOIN `arena_team_stats` ON `arena_team`.`arenateamid` = `arena_team_stats`.`arenateamid` ORDER BY `arena_team`.`arenateamid` ASC");

    if (!result)
    {
        BarGoLink bar(1);

        bar.step();

        sLog.outString();
        sLog.outString(">> Loaded %u arenateam definitions", count);
        return;
    }

    // load arena_team members
    QueryResult* arenaTeamMembersResult = CharacterDatabase.Query(
            //          0                   1      2             3           4               5             6                 7      8
            "SELECT `arenateamid`,`member`.`guid`,`played_week`,`wons_week`,`played_season`,`wons_season`,`personal_rating`,`name`,`class` "
            "FROM `arena_team_member` member LEFT JOIN `characters` chars on member.`guid` = chars.`guid` ORDER BY member.`arenateamid` ASC");

    BarGoLink bar(result->GetRowCount());

    do
    {
        bar.step();
        ++count;

        ArenaTeam* newArenaTeam = new ArenaTeam;
        if (!newArenaTeam->LoadArenaTeamFromDB(result) ||
                !newArenaTeam->LoadMembersFromDB(arenaTeamMembersResult))
        {
            newArenaTeam->Disband(NULL);
            delete newArenaTeam;
            continue;
        }
        AddArenaTeam(newArenaTeam);
    }
    while (result->NextRow());

    delete result;
    delete arenaTeamMembersResult;

    sLog.outString();
    sLog.outString(">> Loaded %u arenateam definitions", count);
}

/**
 * @brief Loads groups, group members, and group instance bindings from the database.
 */
void ObjectMgr::LoadGroups()
{
    // -- loading groups --
    uint32 count = 0;
    //                                                     0           1                2             3             4                5        6        7        8        9        10       11       12       13        14            15            16
    QueryResult* result = CharacterDatabase.Query("SELECT `mainTank`, `mainAssistant`, `lootMethod`, `looterGuid`, `lootThreshold`, `icon1`, `icon2`, `icon3`, `icon4`, `icon5`, `icon6`, `icon7`, `icon8`, `isRaid`, `difficulty`, `leaderGuid`, `groupId` FROM `groups`");

    if (!result)
    {
        BarGoLink bar(1);
        bar.step();
        sLog.outString(">> Loaded %u group definitions", count);
        sLog.outString();
        return;
    }

    BarGoLink bar(result->GetRowCount());

    do
    {
        bar.step();
        Field* fields = result->Fetch();
        ++count;
        Group* group = new Group;
        if (!group->LoadGroupFromDB(fields))
        {
            group->Disband();
            delete group;
            continue;
        }
        AddGroup(group);
    }
    while (result->NextRow());

    delete result;

    sLog.outString(">> Loaded %u group definitions", count);
    sLog.outString();

    // -- loading members --
    count = 0;
    //                                       0           1          2         3
    result = CharacterDatabase.Query("SELECT `memberGuid`, `assistant`, `subgroup`, `groupId` FROM `group_member` ORDER BY `groupId`");
    if (!result)
    {
        BarGoLink bar2(1);
        bar2.step();
    }
    else
    {
        Group* group = NULL;                                // used as cached pointer for avoid relookup group for each member

        BarGoLink bar2(result->GetRowCount());
        do
        {
            bar2.step();
            Field* fields = result->Fetch();
            ++count;

            uint32 memberGuidlow = fields[0].GetUInt32();
            ObjectGuid memberGuid = ObjectGuid(HIGHGUID_PLAYER, memberGuidlow);
            bool   assistent     = fields[1].GetBool();
            uint8  subgroup      = fields[2].GetUInt8();
            uint32 groupId       = fields[3].GetUInt32();
            if (!group || group->GetId() != groupId)
            {
                group = GetGroupById(groupId);
                if (!group)
                {
                    sLog.outErrorDb("Incorrect entry in group_member table : no group with Id %d for member %s!",
                                    groupId, memberGuid.GetString().c_str());
                    CharacterDatabase.PExecute("DELETE FROM `group_member` WHERE `memberGuid` = '%u'", memberGuidlow);
                    continue;
                }
            }

            if (!group->LoadMemberFromDB(memberGuidlow, subgroup, assistent))
            {
                sLog.outErrorDb("Incorrect entry in group_member table : member %s can not be added to group (Id: %u)!",
                                memberGuid.GetString().c_str(), groupId);
                CharacterDatabase.PExecute("DELETE FROM `group_member` WHERE `memberGuid` = '%u'", memberGuidlow);
            }
        }
        while (result->NextRow());
        delete result;
    }

    // clean groups
    // TODO: maybe delete from the DB before loading in this case
    for (GroupMap::iterator itr = mGroupMap.begin(); itr != mGroupMap.end();)
    {
        if (itr->second->GetMembersCount() < 2)
        {
            itr->second->Disband();
            delete itr->second;
            mGroupMap.erase(itr++);
        }
        else
        {
            ++itr;
        }
    }

    // -- loading instances --
    count = 0;
    result = CharacterDatabase.Query(
                 //                        0             1      2           3                       4             5
                 "SELECT `group_instance`.`leaderGuid`, `map`, `instance`, `permanent`, `instance`.`difficulty`, `resettime`, "
                 // 6
                 "(SELECT COUNT(*) FROM `character_instance` WHERE `guid` = `group_instance`.`leaderGuid` AND `instance` = `group_instance`.`instance` AND `permanent` = 1 LIMIT 1), "
                 // 7
                 " `groups`.`groupId` "
                 "FROM `group_instance` LEFT JOIN `instance` ON `instance` = `id` LEFT JOIN `groups` ON `groups`.`leaderGUID` = `group_instance`.`leaderGUID` ORDER BY `leaderGuid`"
             );

    if (!result)
    {
        BarGoLink bar2(1);
        bar2.step();
    }
    else
    {
        Group* group = NULL;                                // used as cached pointer for avoid relookup group for each member

        BarGoLink bar2(result->GetRowCount());
        do
        {
            bar2.step();
            Field* fields = result->Fetch();
            ++count;

            uint32 leaderGuidLow = fields[0].GetUInt32();
            uint32 mapId = fields[1].GetUInt32();
            Difficulty diff = (Difficulty)fields[4].GetUInt8();
            uint32 groupId = fields[7].GetUInt32();

            if (!group || group->GetId() != groupId)
            {
                // find group id in map by leader low guid
                group = GetGroupById(groupId);
                if (!group)
                {
                    sLog.outErrorDb("Incorrect entry in group_instance table : no group with leader %d", leaderGuidLow);
                    continue;
                }
            }

            MapEntry const* mapEntry = sMapStore.LookupEntry(mapId);
            if (!mapEntry || !mapEntry->IsDungeon())
            {
                sLog.outErrorDb("Incorrect entry in group_instance table : no dungeon map %d", mapId);
                continue;
            }

            if (diff >= MAX_DIFFICULTY)
            {
                sLog.outErrorDb("Wrong dungeon difficulty use in group_instance table: %d", diff);
                diff = REGULAR_DIFFICULTY;
            }

            DungeonPersistentState* state = (DungeonPersistentState*)sMapPersistentStateMgr.AddPersistentState(mapEntry, fields[2].GetUInt32(), diff, (time_t)fields[5].GetUInt64(), (fields[6].GetUInt32() == 0), true);
            group->BindToInstance(state, fields[3].GetBool(), true);
        }
        while (result->NextRow());
        delete result;
    }

    sLog.outString(">> Loaded %u group-instance binds total", count);
    sLog.outString();

    sLog.outString(">> Loaded %u group members total", count);
    sLog.outString();
}

/**
 * @brief Loads starting pet spells from the database and DBC fallbacks.
 */
void ObjectMgr::LoadPetCreateSpells()
{
    QueryResult* result = WorldDatabase.Query("SELECT `entry`, `Spell1`, `Spell2`, `Spell3`, `Spell4` FROM `petcreateinfo_spell`");
    if (!result)
    {
        BarGoLink bar(1);
        bar.step();

        sLog.outString();
        sLog.outString(">> Loaded 0 pet create spells");
        // sLog.outErrorDb("`petcreateinfo_spell` table is empty!");
        return;
    }

    uint32 count = 0;

    BarGoLink bar(result->GetRowCount());

    mPetCreateSpell.clear();

    do
    {
        Field* fields = result->Fetch();
        bar.step();

        uint32 creature_id = fields[0].GetUInt32();

        if (!creature_id)
        {
            sLog.outErrorDb("Creature id %u listed in `petcreateinfo_spell` not exist.", creature_id);
            continue;
        }

        CreatureInfo const* cInfo = sCreatureStorage.LookupEntry<CreatureInfo>(creature_id);
        if (!cInfo)
        {
            sLog.outErrorDb("Creature id %u listed in `petcreateinfo_spell` not exist.", creature_id);
            continue;
        }

        if (CreatureSpellDataEntry const* petSpellEntry = cInfo->PetSpellDataId ? sCreatureSpellDataStore.LookupEntry(cInfo->PetSpellDataId) : NULL)
        {
            sLog.outErrorDb("Creature id %u listed in `petcreateinfo_spell` have set `PetSpellDataId` field and will use its instead, skip.", creature_id);
            continue;
        }

        PetCreateSpellEntry PetCreateSpell;

        bool have_spell = false;
        bool have_spell_db = false;
        for (int i = 0; i < 4; ++i)
        {
            PetCreateSpell.spellid[i] = fields[i + 1].GetUInt32();

            if (!PetCreateSpell.spellid[i])
            {
                continue;
            }

            have_spell_db = true;

            SpellEntry const* i_spell = sSpellStore.LookupEntry(PetCreateSpell.spellid[i]);
            if (!i_spell)
            {
                sLog.outErrorDb("Spell %u listed in `petcreateinfo_spell` does not exist", PetCreateSpell.spellid[i]);
                PetCreateSpell.spellid[i] = 0;
                continue;
            }

            have_spell = true;
        }

        if (!have_spell_db)
        {
            sLog.outErrorDb("Creature %u listed in `petcreateinfo_spell` have only 0 spell data, why it listed?", creature_id);
            continue;
        }

        if (!have_spell)
        {
            continue;
        }

        mPetCreateSpell[creature_id] = PetCreateSpell;
        ++count;
    }
    while (result->NextRow());

    delete result;

    // cache spell->learn spell map for use in next loop
    std::map<uint32, uint32> learnCache;
    for (uint32 spell_id = 1; spell_id < sSpellStore.GetNumRows(); ++spell_id)
    {
        SpellEntry const* spellproto = sSpellStore.LookupEntry(spell_id);
        if (!spellproto)
        {
            continue;
        }

        if (spellproto->Effect[0] != SPELL_EFFECT_LEARN_SPELL && spellproto->Effect[0] != SPELL_EFFECT_LEARN_PET_SPELL)
        {
            continue;
        }

        if (!spellproto->EffectTriggerSpell[0])
        {
            continue;
        }

        learnCache[spellproto->EffectTriggerSpell[0]] = spellproto->Id;
    }

    // fill data from DBC as more correct source if available
    uint32 dcount = 0;
    for (uint32 cr_id = 1; cr_id < sCreatureStorage.GetMaxEntry(); ++cr_id)
    {
        CreatureInfo const* cInfo = sCreatureStorage.LookupEntry<CreatureInfo>(cr_id);
        if (!cInfo)
        {
            continue;
        }

        CreatureSpellDataEntry const* petSpellEntry = cInfo->PetSpellDataId ? sCreatureSpellDataStore.LookupEntry(cInfo->PetSpellDataId) : NULL;
        if (!petSpellEntry)
        {
            continue;
        }

        PetCreateSpellEntry PetCreateSpell;
        for (int i = 0; i < MAX_CREATURE_SPELL_DATA_SLOT; ++i)
        {
            uint32 petspell_id = petSpellEntry->spellId[i];
            if (petspell_id)
            {
                // in dbc stored spell for pet use, but for teaching work we need learn spell ids
                std::map<uint32, uint32>::const_iterator cache_itr = learnCache.find(petspell_id);
                if (cache_itr != learnCache.end())
                {
                    petspell_id = cache_itr->second;
                }
            }

            PetCreateSpell.spellid[i] = petspell_id;
        }

        mPetCreateSpell[cr_id] = PetCreateSpell;
        ++dcount;
    }

    sLog.outString();
    sLog.outString(">> Loaded %u pet create spells from table and %u from DBC", count, dcount);
}

/**
 * @brief Loads page text records and validates page chains.
 */
void ObjectMgr::LoadPageTexts()
{
    sPageTextStore.Load();
    sLog.outString(">> Loaded %u page texts", sPageTextStore.GetRecordCount());
    sLog.outString();

    for (uint32 i = 1; i < sPageTextStore.GetMaxEntry(); ++i)
    {
        // check data correctness
        PageText const* page = sPageTextStore.LookupEntry<PageText>(i);
        if (!page)
        {
            continue;
        }

        if (page->Next_Page && !sPageTextStore.LookupEntry<PageText>(page->Next_Page))
        {
            sLog.outErrorDb("Page text (Id: %u) has not existing next page (Id:%u)", i, page->Next_Page);
            continue;
        }

        // detect circular reference
        std::set<uint32> checkedPages;
        for (PageText const* pageItr = page; pageItr; pageItr = sPageTextStore.LookupEntry<PageText>(pageItr->Next_Page))
        {
            if (!pageItr->Next_Page)
            {
                break;
            }
            checkedPages.insert(pageItr->Page_ID);
            if (checkedPages.find(pageItr->Next_Page) != checkedPages.end())
            {
                std::ostringstream ss;
                ss << "The text page(s) ";
                for (std::set<uint32>::iterator itr = checkedPages.begin(); itr != checkedPages.end(); ++itr)
                {
                    ss << *itr << " ";
                }
                ss << "create(s) a circular reference, which can cause the server to freeze. Changing Next_Page of page "
                   << pageItr->Page_ID << " to 0";
                sLog.outErrorDb("%s", ss.str().c_str());
                const_cast<PageText*>(pageItr)->Next_Page = 0;
                break;
            }
        }
    }
}

/**
 * @brief Loads localized page text content.
 */
void ObjectMgr::LoadPageTextLocales()
{
    mPageTextLocaleMap.clear();                             // need for reload case

    QueryResult* result = WorldDatabase.Query("SELECT `entry`,`text_loc1`,`text_loc2`,`text_loc3`,`text_loc4`,`text_loc5`,`text_loc6`,`text_loc7`,`text_loc8` FROM `locales_page_text`");

    if (!result)
    {
        BarGoLink bar(1);
        bar.step();
        sLog.outString(">> Loaded 0 PageText locale strings. DB table `locales_page_text` is empty.");
        return;
    }

    BarGoLink bar(result->GetRowCount());

    do
    {
        Field* fields = result->Fetch();
        bar.step();

        uint32 entry = fields[0].GetUInt32();

        if (!sPageTextStore.LookupEntry<PageText>(entry))
        {
            ERROR_DB_STRICT_LOG("Table `locales_page_text` has data for nonexistent page text entry %u, skipped.", entry);
            continue;
        }

        PageTextLocale& data = mPageTextLocaleMap[entry];

        for (int i = 1; i < MAX_LOCALE; ++i)
        {
            std::string str = fields[i].GetCppString();
            if (str.empty())
            {
                continue;
            }

            int idx = GetOrNewIndexForLocale(LocaleConstant(i));
            if (idx >= 0)
            {
                if ((int32)data.Text.size() <= idx)
                {
                    data.Text.resize(idx + 1);
                }

                data.Text[idx] = str;
            }
        }
    }
    while (result->NextRow());

    delete result;

    sLog.outString(">> Loaded %zu PageText locale strings", mPageTextLocaleMap.size());
    sLog.outString();
}

struct SQLInstanceLoader : public SQLStorageLoaderBase<SQLInstanceLoader, SQLStorage>
{
    template<class D>
    void convert_from_str(uint32 /*field_pos*/, char const* src, D& dst)
    {
        dst = D(sScriptMgr.GetScriptId(src));
    }
};

/**
 * @brief Loads instance templates and validates map and ghost entrance data.
 */
void ObjectMgr::LoadInstanceTemplate()
{
    SQLInstanceLoader loader;
    loader.Load(sInstanceTemplate);

    for (uint32 i = 0; i < sInstanceTemplate.GetMaxEntry(); ++i)
    {
        InstanceTemplate const* temp = GetInstanceTemplate(i);
        if (!temp)
        {
            continue;
        }

        MapEntry const* mapEntry = sMapStore.LookupEntry(temp->map);
        if (!mapEntry)
        {
            sLog.outErrorDb("ObjectMgr::LoadInstanceTemplate: bad mapid %d for template!", temp->map);
            sInstanceTemplate.EraseEntry(i);
            continue;
        }

        if (!mapEntry->Instanceable())
        {
            sLog.outErrorDb("ObjectMgr::LoadInstanceTemplate: non-instanceable mapid %d for template!", temp->map);
            sInstanceTemplate.EraseEntry(i);
            continue;
        }

        if (temp->parent > 0)
        {
            // check existence
            MapEntry const* parentEntry = sMapStore.LookupEntry(temp->parent);
            if (!parentEntry)
            {
                sLog.outErrorDb("ObjectMgr::LoadInstanceTemplate: bad parent map id %u for instance template %d template!",
                                temp->parent, temp->map);
                const_cast<InstanceTemplate*>(temp)->parent = 0;
                continue;
            }

            if (parentEntry->IsContinent())
            {
                sLog.outErrorDb("ObjectMgr::LoadInstanceTemplate: parent point to continent map id %u for instance template %d template, ignored, need be set only for non-continent parents!",
                                parentEntry->MapID, temp->map);
                const_cast<InstanceTemplate*>(temp)->parent = 0;
                continue;
            }
        }

        if (mapEntry->HasResetTime())
        {
            if (temp->reset_delay == 0)
            {
                // use defaults from the DBC
                if (mapEntry->SupportsHeroicMode())
                {
                    const_cast<InstanceTemplate*>(temp)->reset_delay = mapEntry->resetTimeHeroic / DAY;
                }
                else if (mapEntry->resetTimeRaid && mapEntry->map_type == MAP_RAID)
                {
                    const_cast<InstanceTemplate*>(temp)->reset_delay = mapEntry->resetTimeRaid / DAY;
                }
            }

            // the reset_delay must be at least one day
            const_cast<InstanceTemplate*>(temp)->reset_delay = std::max((uint32)1, (uint32)(temp->reset_delay * sWorld.getConfig(CONFIG_FLOAT_RATE_INSTANCE_RESET_TIME)));
        }
    }

    sLog.outString(">> Loaded %u Instance Template definitions", sInstanceTemplate.GetRecordCount());
    sLog.outString();
}

struct SQLWorldLoader : public SQLStorageLoaderBase<SQLWorldLoader, SQLStorage>
{
    template<class D>
    void convert_from_str(uint32 /*field_pos*/, char const* src, D& dst)
    {
        dst = D(sScriptMgr.GetScriptId(src));
    }
};

/**
 * @brief Loads player condition definitions and removes invalid entries.
 */
void ObjectMgr::LoadConditions()
{
    SQLWorldLoader loader;
    loader.Load(sConditionStorage);

    for (uint32 i = 0; i < sConditionStorage.GetMaxEntry(); ++i)
    {
        const PlayerCondition* condition = sConditionStorage.LookupEntry<PlayerCondition>(i);
        if (!condition)
        {
            continue;
        }

        if (!condition->IsValid())
        {
            sLog.outErrorDb("ObjectMgr::LoadConditions: invalid condition_entry %u, skip", i);
            sConditionStorage.EraseEntry(i);
            continue;
        }
    }

    sLog.outString(">> Loaded %u Condition definitions", sConditionStorage.GetRecordCount());
    sLog.outString();
}

/**
 * @brief Gets a loaded gossip text record by id.
 *
 * @param Text_ID The gossip text identifier.
 * @return The gossip text record, or null if missing.
 */
GossipText const* ObjectMgr::GetGossipText(uint32 Text_ID) const
{
    GossipTextMap::const_iterator itr = mGossipText.find(Text_ID);
    if (itr != mGossipText.end())
    {
        return &itr->second;
    }
    return NULL;
}

/**
 * @brief Loads npc gossip text records from the database.
 */
void ObjectMgr::LoadGossipText()
{
    QueryResult* result = WorldDatabase.Query("SELECT * FROM `npc_text`");

    int count = 0;
    if (!result)
    {
        BarGoLink bar(1);
        bar.step();

        sLog.outString(">> Loaded %u npc texts", count);
        sLog.outString();
        return;
    }

    BarGoLink bar(result->GetRowCount());

    do
    {
        ++count;
        int cic = 0;

        Field* fields = result->Fetch();

        bar.step();

        uint32 Text_ID    = fields[cic++].GetUInt32();
        if (!Text_ID)
        {
            sLog.outErrorDb("Table `npc_text` has record wit reserved id 0, ignore.");
            continue;
        }

        GossipText& gText = mGossipText[Text_ID];

        for (int i = 0; i < MAX_GOSSIP_TEXT_OPTIONS; ++i)
        {
            gText.Options[i].Text_0           = fields[cic++].GetCppString();
            gText.Options[i].Text_1           = fields[cic++].GetCppString();

            gText.Options[i].Language         = fields[cic++].GetUInt32();
            gText.Options[i].Probability      = fields[cic++].GetFloat();

            for (int j = 0; j < 3; ++j)
            {
                gText.Options[i].Emotes[j]._Delay  = fields[cic++].GetUInt32();
                gText.Options[i].Emotes[j]._Emote  = fields[cic++].GetUInt32();
            }
        }
    }
    while (result->NextRow());

    sLog.outString(">> Loaded %u npc texts", count);
    sLog.outString();
    delete result;
}

/**
 * @brief Loads localized npc gossip text variants.
 */
void ObjectMgr::LoadGossipTextLocales()
{
    mNpcTextLocaleMap.clear();                              // need for reload case

    QueryResult* result = WorldDatabase.Query("SELECT `entry`,"
                          "`Text0_0_loc1`,`Text0_1_loc1`,`Text1_0_loc1`,`Text1_1_loc1`,`Text2_0_loc1`,`Text2_1_loc1`,`Text3_0_loc1`,`Text3_1_loc1`,`Text4_0_loc1`,`Text4_1_loc1`,`Text5_0_loc1`,`Text5_1_loc1`,`Text6_0_loc1`,`Text6_1_loc1`,`Text7_0_loc1`,`Text7_1_loc1`,"
                          "`Text0_0_loc2`,`Text0_1_loc2`,`Text1_0_loc2`,`Text1_1_loc2`,`Text2_0_loc2`,`Text2_1_loc2`,`Text3_0_loc2`,`Text3_1_loc2`,`Text4_0_loc2`,`Text4_1_loc2`,`Text5_0_loc2`,`Text5_1_loc2`,`Text6_0_loc2`,`Text6_1_loc2`,`Text7_0_loc2`,`Text7_1_loc2`,"
                          "`Text0_0_loc3`,`Text0_1_loc3`,`Text1_0_loc3`,`Text1_1_loc3`,`Text2_0_loc3`,`Text2_1_loc3`,`Text3_0_loc3`,`Text3_1_loc3`,`Text4_0_loc3`,`Text4_1_loc3`,`Text5_0_loc3`,`Text5_1_loc3`,`Text6_0_loc3`,`Text6_1_loc3`,`Text7_0_loc3`,`Text7_1_loc3`,"
                          "`Text0_0_loc4`,`Text0_1_loc4`,`Text1_0_loc4`,`Text1_1_loc4`,`Text2_0_loc4`,`Text2_1_loc4`,`Text3_0_loc4`,`Text3_1_loc4`,`Text4_0_loc4`,`Text4_1_loc4`,`Text5_0_loc4`,`Text5_1_loc4`,`Text6_0_loc4`,`Text6_1_loc4`,`Text7_0_loc4`,`Text7_1_loc4`,"
                          "`Text0_0_loc5`,`Text0_1_loc5`,`Text1_0_loc5`,`Text1_1_loc5`,`Text2_0_loc5`,`Text2_1_loc5`,`Text3_0_loc5`,`Text3_1_loc5`,`Text4_0_loc5`,`Text4_1_loc5`,`Text5_0_loc5`,`Text5_1_loc5`,`Text6_0_loc5`,`Text6_1_loc5`,`Text7_0_loc5`,`Text7_1_loc5`,"
                          "`Text0_0_loc6`,`Text0_1_loc6`,`Text1_0_loc6`,`Text1_1_loc6`,`Text2_0_loc6`,`Text2_1_loc6`,`Text3_0_loc6`,`Text3_1_loc6`,`Text4_0_loc6`,`Text4_1_loc6`,`Text5_0_loc6`,`Text5_1_loc6`,`Text6_0_loc6`,`Text6_1_loc6`,`Text7_0_loc6`,`Text7_1_loc6`,"
                          "`Text0_0_loc7`,`Text0_1_loc7`,`Text1_0_loc7`,`Text1_1_loc7`,`Text2_0_loc7`,`Text2_1_loc7`,`Text3_0_loc7`,`Text3_1_loc7`,`Text4_0_loc7`,`Text4_1_loc7`,`Text5_0_loc7`,`Text5_1_loc7`,`Text6_0_loc7`,`Text6_1_loc7`,`Text7_0_loc7`,`Text7_1_loc7`, "
                          "`Text0_0_loc8`,`Text0_1_loc8`,`Text1_0_loc8`,`Text1_1_loc8`,`Text2_0_loc8`,`Text2_1_loc8`,`Text3_0_loc8`,`Text3_1_loc8`,`Text4_0_loc8`,`Text4_1_loc8`,`Text5_0_loc8`,`Text5_1_loc8`,`Text6_0_loc8`,`Text6_1_loc8`,`Text7_0_loc8`,`Text7_1_loc8` "
                          " FROM `locales_npc_text`");

    if (!result)
    {
        BarGoLink bar(1);
        bar.step();
        sLog.outString(">> Loaded 0 Quest locale strings. DB table `locales_npc_text` is empty.");
        return;
    }

    BarGoLink bar(result->GetRowCount());

    do
    {
        Field* fields = result->Fetch();
        bar.step();

        uint32 entry = fields[0].GetUInt32();

        if (!GetGossipText(entry))
        {
            ERROR_DB_STRICT_LOG("Table `locales_npc_text` has data for nonexistent gossip text entry %u, skipped.", entry);
            continue;
        }

        NpcTextLocale& data = mNpcTextLocaleMap[entry];

        for (int i = 1; i < MAX_LOCALE; ++i)
        {
            for (int j = 0; j < 8; ++j)
            {
                std::string str0 = fields[1 + 8 * 2 * (i - 1) + 2 * j].GetCppString();
                if (!str0.empty())
                {
                    int idx = GetOrNewIndexForLocale(LocaleConstant(i));
                    if (idx >= 0)
                    {
                        if ((int32)data.Text_0[j].size() <= idx)
                        {
                            data.Text_0[j].resize(idx + 1);
                        }

                        data.Text_0[j][idx] = str0;
                    }
                }
                std::string str1 = fields[1 + 8 * 2 * (i - 1) + 2 * j + 1].GetCppString();
                if (!str1.empty())
                {
                    int idx = GetOrNewIndexForLocale(LocaleConstant(i));
                    if (idx >= 0)
                    {
                        if ((int32)data.Text_1[j].size() <= idx)
                        {
                            data.Text_1[j].resize(idx + 1);
                        }

                        data.Text_1[j][idx] = str1;
                    }
                }
            }
        }
    }
    while (result->NextRow());

    delete result;

    sLog.outString(">> Loaded %zu NpcText locale strings", mNpcTextLocaleMap.size());
    sLog.outString();
}

// not very fast function but it is called only once a day, or on starting-up
/// @param serverUp true if the server is already running, false when the server is started
void ObjectMgr::ReturnOrDeleteOldMails(bool serverUp)
{
    time_t curTime = time(NULL);
    std::tm lt = safe_localtime(curTime);
    uint64 basetime(curTime);
    sLog.outString("Returning mails current time: hour: %d, minute: %d, second: %d ", lt.tm_hour, lt.tm_min, lt.tm_sec);

    // delete all old mails without item and without body immediately, if starting server
    if (!serverUp)
    {
        CharacterDatabase.PExecute("DELETE FROM `mail` WHERE `expire_time` < '" UI64FMTD "' AND `has_items` = '0' AND `body` = ''", (uint64)basetime);
    }
    //                                                     0  1           2      3        4          5         6           7   8       9
    QueryResult* result = CharacterDatabase.PQuery("SELECT `id`,`messageType`,`sender`,`receiver`,`has_items`,`expire_time`,`cod`,`checked`,`mailTemplateId` FROM `mail` WHERE `expire_time` < '" UI64FMTD "'", (uint64)basetime);
    if (!result)
    {
        BarGoLink bar(1);
        bar.step();
        sLog.outString(">> Only expired mails (need to be return or delete) or DB table `mail` is empty.");
        sLog.outString();
        return;                                             // any mails need to be returned or deleted
    }

    // std::ostringstream delitems, delmails; // will be here for optimization
    // bool deletemail = false, deleteitem = false;
    // delitems << "DELETE FROM `item_instance` WHERE `guid` IN ( ";
    // delmails << "DELETE FROM `mail` WHERE `id` IN ( "

    BarGoLink bar(result->GetRowCount());
    uint32 count = 0;
    Field* fields;

    do
    {
        bar.step();

        fields = result->Fetch();
        Mail* m = new Mail;
        m->messageID = fields[0].GetUInt32();
        m->messageType = fields[1].GetUInt8();
        m->sender = fields[2].GetUInt32();
        m->receiverGuid = ObjectGuid(HIGHGUID_PLAYER, fields[3].GetUInt32());
        bool has_items = fields[4].GetBool();
        m->expire_time = (time_t)fields[5].GetUInt64();
        m->deliver_time = 0;
        m->COD = fields[6].GetUInt32();
        m->checked = fields[7].GetUInt32();
        m->mailTemplateId = fields[8].GetInt16();

        Player* pl = 0;
        if (serverUp)
        {
            pl = GetPlayer(m->receiverGuid);
        }
        if (pl)
        {
            // this code will run very improbably (the time is between 4 and 5 am, in game is online a player, who has old mail
            // his in mailbox and he has already listed his mails )
            delete m;
            continue;
        }
        // delete or return mail:
        if (has_items)
        {
            QueryResult* resultItems = CharacterDatabase.PQuery("SELECT `item_guid`,`item_template` FROM `mail_items` WHERE `mail_id`='%u'", m->messageID);
            if (resultItems)
            {
                do
                {
                    Field* fields2 = resultItems->Fetch();

                    uint32 item_guid_low = fields2[0].GetUInt32();
                    uint32 item_template = fields2[1].GetUInt32();

                    m->AddItem(item_guid_low, item_template);
                }
                while (resultItems->NextRow());

                delete resultItems;
            }
            // if it is mail from non-player, or if it's already return mail, it shouldn't be returned, but deleted
            if (m->messageType != MAIL_NORMAL || (m->checked & (MAIL_CHECK_MASK_COD_PAYMENT | MAIL_CHECK_MASK_RETURNED)))
            {
                // mail open and then not returned
                for (MailItemInfoVec::iterator itr2 = m->items.begin(); itr2 != m->items.end(); ++itr2)
                {
                    CharacterDatabase.PExecute("DELETE FROM `item_instance` WHERE `guid` = '%u'", itr2->item_guid);
                }
            }
            else
            {
                // mail will be returned:
                CharacterDatabase.PExecute("UPDATE `mail` SET `sender` = '%u', `receiver` = '%u', `expire_time` = '" UI64FMTD "', `deliver_time` = '" UI64FMTD "', `cod` = '0', `checked` = '%u' WHERE `id` = '%u'",
                                           m->receiverGuid.GetCounter(), m->sender, (uint64)(basetime + 30 * DAY), (uint64)basetime, MAIL_CHECK_MASK_RETURNED, m->messageID);
                for (MailItemInfoVec::iterator itr2 = m->items.begin(); itr2 != m->items.end(); ++itr2)
                {
                    // update receiver in mail items for its proper delivery, and in instance_item for avoid lost item at sender delete
                    CharacterDatabase.PExecute("UPDATE `mail_items` SET `receiver` = %u WHERE `item_guid` = '%u'", m->sender, itr2->item_guid);
                    CharacterDatabase.PExecute("UPDATE `item_instance` SET `owner_guid` = %u WHERE `guid` = '%u'", m->sender, itr2->item_guid);
                }
                delete m;
                continue;
            }
        }

        // deletemail = true;
        // delmails << m->messageID << ", ";
        CharacterDatabase.PExecute("DELETE FROM `mail` WHERE `id` = '%u'", m->messageID);
        delete m;
        ++count;
    }
    while (result->NextRow());
    delete result;

    sLog.outString(">> Loaded %u mails", count);
    sLog.outString();
}

/**
 * @brief Loads area trigger to quest objective relationships.
 */
void ObjectMgr::LoadQuestAreaTriggers()
{
    mQuestAreaTriggerMap.clear();                           // need for reload case

    QueryResult* result = WorldDatabase.PQuery("SELECT `entry`, `quest` FROM `quest_relations` WHERE `actor` = %d", QA_AREATRIGGER);

    uint32 count = 0;

    if (!result)
    {
        BarGoLink bar(1);
        bar.step();
        sLog.outString(">> Loaded %u quest trigger points", count);
        sLog.outString();
        return;
    }

    BarGoLink bar(result->GetRowCount());

    do
    {
        ++count;
        bar.step();

        Field* fields = result->Fetch();

        uint32 trigger_ID = fields[0].GetUInt32();
        uint32 quest_ID   = fields[1].GetUInt32();

        AreaTriggerEntry const* atEntry = sAreaTriggerStore.LookupEntry(trigger_ID);
        if (!atEntry)
        {
            sLog.outErrorDb("Table `quest_relations` has area trigger (ID: %u) not listed in `AreaTrigger.dbc`.", trigger_ID);
            continue;
        }

        Quest const* quest = GetQuestTemplate(quest_ID);
        if (!quest)
        {
            sLog.outErrorDb("Table `quest_relations` has record (id: %u) for not existing quest %u", trigger_ID, quest_ID);
            continue;
        }

        if (!quest->HasSpecialFlag(QUEST_SPECIAL_FLAG_EXPLORATION_OR_EVENT))
        {
            sLog.outErrorDb("Table `quest_relations` has record (id: %u) for not quest %u, but quest not have flag QUEST_SPECIAL_FLAG_EXPLORATION_OR_EVENT. Trigger or quest flags must be fixed, quest modified to require objective.", trigger_ID, quest_ID);

            // this will prevent quest completing without objective
            const_cast<Quest*>(quest)->SetSpecialFlag(QUEST_SPECIAL_FLAG_EXPLORATION_OR_EVENT);

            // continue; - quest modified to required objective and trigger can be allowed.
        }

        mQuestAreaTriggerMap[trigger_ID] = quest_ID;
    }
    while (result->NextRow());

    delete result;

    sLog.outString(">> Loaded %u quest trigger points", count);
    sLog.outString();
}

/**
 * @brief Loads area triggers that mark tavern rest zones.
 */
void ObjectMgr::LoadTavernAreaTriggers()
{
    mTavernAreaTriggerSet.clear();                          // need for reload case

    QueryResult* result = WorldDatabase.Query("SELECT `id` FROM `areatrigger_tavern`");

    uint32 count = 0;

    if (!result)
    {
        BarGoLink bar(1);
        bar.step();
        sLog.outString(">> Loaded %u tavern triggers", count);
        sLog.outString();
        return;
    }

    BarGoLink bar(result->GetRowCount());

    do
    {
        ++count;
        bar.step();

        Field* fields = result->Fetch();

        uint32 Trigger_ID      = fields[0].GetUInt32();

        AreaTriggerEntry const* atEntry = sAreaTriggerStore.LookupEntry(Trigger_ID);
        if (!atEntry)
        {
            sLog.outErrorDb("Table `areatrigger_tavern` has area trigger (ID:%u) not listed in `AreaTrigger.dbc`.", Trigger_ID);
            continue;
        }

        mTavernAreaTriggerSet.insert(Trigger_ID);
    }
    while (result->NextRow());

    delete result;

    sLog.outString(">> Loaded %u tavern triggers", count);
    sLog.outString();
}

/**
 * @brief Renumbers group ids into a compact sequential range.
 */
void ObjectMgr::PackGroupIds()
{
    // this routine renumbers groups in such a way so they start from 1 and go up

    // obtain set of all groups
    std::set<uint32> groupIds;

    // all valid ids are in the instance table
    // any associations to ids not in this table are assumed to be
    // cleaned already in CleanupInstances
    QueryResult* result = CharacterDatabase.Query("SELECT `groupId` FROM `groups`");
    if (result)
    {
        do
        {
            Field* fields = result->Fetch();

            uint32 id = fields[0].GetUInt32();

            if (id == 0)
            {
                CharacterDatabase.BeginTransaction();
                CharacterDatabase.PExecute("DELETE FROM `groups` WHERE `groupId` = '%u'", id);
                CharacterDatabase.PExecute("DELETE FROM `group_member` WHERE `groupId` = '%u'", id);
                CharacterDatabase.CommitTransaction();
                continue;
            }

            groupIds.insert(id);
        }
        while (result->NextRow());
        delete result;
    }

    BarGoLink bar(groupIds.size() + 1);
    bar.step();

    uint32 groupId = 1;
    // we do assume std::set is sorted properly on integer value
    for (std::set<uint32>::iterator i = groupIds.begin(); i != groupIds.end(); ++i)
    {
        if (*i != groupId)
        {
            // remap group id
            CharacterDatabase.BeginTransaction();
            CharacterDatabase.PExecute("UPDATE `groups` SET `groupId` = '%u' WHERE `groupId` = '%u'", groupId, *i);
            CharacterDatabase.PExecute("UPDATE `group_member` SET `groupId` = '%u' WHERE `groupId` = '%u'", groupId, *i);
            CharacterDatabase.CommitTransaction();
        }

        ++groupId;
        bar.step();
    }

    sLog.outString(">> Group Ids remapped, next group id is %u", groupId);
    sLog.outString();
}

/**
 * @brief Initializes high-water marks for generated GUID and id sequences.
 */
void ObjectMgr::SetHighestGuids()
{
    QueryResult* result = CharacterDatabase.Query("SELECT MAX(`guid`) FROM `characters`");
    if (result)
    {
        m_CharGuids.Set((*result)[0].GetUInt32() + 1);
        delete result;
    }

    result = WorldDatabase.Query("SELECT MAX(`guid`) FROM `creature`");
    if (result)
    {
        m_FirstTemporaryCreatureGuid = (*result)[0].GetUInt32() + 1;
        delete result;
    }

    result = CharacterDatabase.Query("SELECT MAX(`guid`) FROM `item_instance`");
    if (result)
    {
        m_ItemGuids.Set((*result)[0].GetUInt32() + 1);
        delete result;
    }

    // Cleanup other tables from nonexistent guids (>=m_hiItemGuid)
    CharacterDatabase.BeginTransaction();
    CharacterDatabase.PExecute("DELETE FROM `character_inventory` WHERE `item` >= '%u'", m_ItemGuids.GetNextAfterMaxUsed());
    CharacterDatabase.PExecute("DELETE FROM `mail_items` WHERE `item_guid` >= '%u'", m_ItemGuids.GetNextAfterMaxUsed());
    CharacterDatabase.PExecute("DELETE FROM `auction` WHERE `itemguid` >= '%u'", m_ItemGuids.GetNextAfterMaxUsed());
    CharacterDatabase.PExecute("DELETE FROM `guild_bank_item` WHERE `item_guid` >= '%u'", m_ItemGuids.GetNextAfterMaxUsed());
    CharacterDatabase.CommitTransaction();

    result = WorldDatabase.Query("SELECT MAX(`guid`) FROM `gameobject`");
    if (result)
    {
        m_FirstTemporaryGameObjectGuid = (*result)[0].GetUInt32() + 1;
        delete result;
    }

    result = CharacterDatabase.Query("SELECT MAX(`id`) FROM `auction`");
    if (result)
    {
        m_AuctionIds.Set((*result)[0].GetUInt32() + 1);
        delete result;
    }

    result = CharacterDatabase.Query("SELECT MAX(`id`) FROM `mail`");
    if (result)
    {
        m_MailIds.Set((*result)[0].GetUInt32() + 1);
        delete result;
    }

    result = CharacterDatabase.Query("SELECT MAX(`guid`) FROM `corpse`");
    if (result)
    {
        m_CorpseGuids.Set((*result)[0].GetUInt32() + 1);
        delete result;
    }

    result = CharacterDatabase.Query("SELECT MAX(`arenateamid`) FROM `arena_team`");
    if (result)
    {
        m_ArenaTeamIds.Set((*result)[0].GetUInt32() + 1);
        delete result;
    }

    result = CharacterDatabase.Query("SELECT MAX(`guildid`) FROM `guild`");
    if (result)
    {
        m_GuildIds.Set((*result)[0].GetUInt32() + 1);
        delete result;
    }

    result = CharacterDatabase.Query("SELECT MAX(`groupId`) FROM `groups`");
    if (result)
    {
        m_GroupGuids.Set((*result)[0].GetUInt32() + 1);
        delete result;
    }

    // setup reserved ranges for static guids spawn
    m_StaticCreatureGuids.Set(m_FirstTemporaryCreatureGuid);
    m_FirstTemporaryCreatureGuid += sWorld.getConfig(CONFIG_UINT32_GUID_RESERVE_SIZE_CREATURE);

    m_StaticGameObjectGuids.Set(m_FirstTemporaryGameObjectGuid);
    m_FirstTemporaryGameObjectGuid += sWorld.getConfig(CONFIG_UINT32_GUID_RESERVE_SIZE_GAMEOBJECT);
}

/**
 * @brief Loads localized gameobject names.
 */
void ObjectMgr::LoadGameObjectLocales()
{
    mGameObjectLocaleMap.clear();                           // need for reload case

    QueryResult* result = WorldDatabase.Query("SELECT `entry`,"
                          "`name_loc1`,`name_loc2`,`name_loc3`,`name_loc4`,`name_loc5`,`name_loc6`,`name_loc7`,`name_loc8`,"
                          "`castbarcaption_loc1`,`castbarcaption_loc2`,`castbarcaption_loc3`,`castbarcaption_loc4`,"
                          "`castbarcaption_loc5`,`castbarcaption_loc6`,`castbarcaption_loc7`,`castbarcaption_loc8` FROM `locales_gameobject`");

    if (!result)
    {
        BarGoLink bar(1);
        bar.step();
        sLog.outString(">> Loaded 0 gameobject locale strings. DB table `locales_gameobject` is empty.");
        return;
    }

    BarGoLink bar(result->GetRowCount());

    do
    {
        Field* fields = result->Fetch();
        bar.step();

        uint32 entry = fields[0].GetUInt32();

        if (!GetGameObjectInfo(entry))
        {
            ERROR_DB_STRICT_LOG("Table `locales_gameobject` has data for nonexistent gameobject entry %u, skipped.", entry);
            continue;
        }

        GameObjectLocale& data = mGameObjectLocaleMap[entry];

        for (int i = 1; i < MAX_LOCALE; ++i)
        {
            std::string str = fields[i].GetCppString();
            if (!str.empty())
            {
                int idx = GetOrNewIndexForLocale(LocaleConstant(i));
                if (idx >= 0)
                {
                    if ((int32)data.Name.size() <= idx)
                    {
                        data.Name.resize(idx + 1);
                    }

                    data.Name[idx] = str;
                }
            }
        }

        for (int i = 1; i < MAX_LOCALE; ++i)
        {
            std::string str = fields[i+(MAX_LOCALE-1)].GetCppString();
            if (!str.empty())
            {
                int idx = GetOrNewIndexForLocale(LocaleConstant(i));
                if (idx >= 0)
                {
                    if ((int32)data.CastBarCaption.size() <= idx)
                    {
                        data.CastBarCaption.resize(idx + 1);
                    }

                    data.CastBarCaption[idx] = str;
                }
            }
        }

    }
    while (result->NextRow());

    delete result;

    sLog.outString(">> Loaded %zu gameobject locale strings", mGameObjectLocaleMap.size());
    sLog.outString();
}

struct SQLGameObjectLoader : public SQLStorageLoaderBase<SQLGameObjectLoader, SQLHashStorage>
{
    template<class D>
    void convert_from_str(uint32 /*field_pos*/, char const* src, D& dst)
    {
        dst = D(sScriptMgr.GetScriptId(src));
    }
};

/**
 * @brief Validates a referenced gameobject lock id.
 *
 * @param goInfo The gameobject template being checked.
 * @param dataN The referenced lock id.
 * @param N The source data index.
 */
inline void CheckGOLockId(GameObjectInfo const* goInfo, uint32 dataN, uint32 N)
{
    if (sLockStore.LookupEntry(dataN))
    {
        return;
    }

    sLog.outErrorDb("Gameobject (Entry: %u GoType: %u) have data%d=%u but lock (Id: %u) not found.",
                    goInfo->id, goInfo->type, N, dataN, dataN);
}

/**
 * @brief Validates that a linked trap entry exists and is a trap gameobject.
 *
 * @param goInfo The gameobject template being checked.
 * @param dataN The referenced trap entry.
 * @param N The source data index.
 */
inline void CheckGOLinkedTrapId(GameObjectInfo const* goInfo, uint32 dataN, uint32 N)
{
    if (GameObjectInfo const* trapInfo = sGOStorage.LookupEntry<GameObjectInfo>(dataN))
    {
        if (trapInfo->type != GAMEOBJECT_TYPE_TRAP)
            sLog.outErrorDb("Gameobject (Entry: %u GoType: %u) have data%d=%u but GO (Entry %u) have not GAMEOBJECT_TYPE_TRAP (%u) type.",
                            goInfo->id, goInfo->type, N, dataN, dataN, GAMEOBJECT_TYPE_TRAP);
    }
    else
        // too many error reports about nonexistent trap templates
        ERROR_DB_STRICT_LOG("Gameobject (Entry: %u GoType: %u) have data%d=%u but trap GO (Entry %u) not exist in `gameobject_template`.",
                            goInfo->id, goInfo->type, N, dataN, dataN);
}

/**
 * @brief Validates a referenced spell id in gameobject data.
 *
 * @param goInfo The gameobject template being checked.
 * @param dataN The referenced spell id.
 * @param N The source data index.
 */
inline void CheckGOSpellId(GameObjectInfo const* goInfo, uint32 dataN, uint32 N)
{
    if (sSpellStore.LookupEntry(dataN))
    {
        return;
    }

    sLog.outErrorDb("Gameobject (Entry: %u GoType: %u) have data%d=%u but Spell (Entry %u) not exist.",
                    goInfo->id, goInfo->type, N, dataN, dataN);
}

/**
 * @brief Validates and clamps chair height data for a gameobject.
 *
 * @param goInfo The gameobject template being checked.
 * @param dataN The chair height value.
 * @param N The source data index.
 */
inline void CheckAndFixGOChairHeightId(GameObjectInfo const* goInfo, uint32 const& dataN, uint32 N)
{
    if (dataN <= (UNIT_STAND_STATE_SIT_HIGH_CHAIR - UNIT_STAND_STATE_SIT_LOW_CHAIR))
    {
        return;
    }

    sLog.outErrorDb("Gameobject (Entry: %u GoType: %u) have data%d=%u but correct chair height in range 0..%i.",
                    goInfo->id, goInfo->type, N, dataN, UNIT_STAND_STATE_SIT_HIGH_CHAIR - UNIT_STAND_STATE_SIT_LOW_CHAIR);

    // prevent client and server unexpected work
    const_cast<uint32&>(dataN) = 0;
}

/**
 * @brief Validates a boolean no-damage-immune flag in gameobject data.
 *
 * @param goInfo The gameobject template being checked.
 * @param dataN The field value.
 * @param N The source data index.
 */
inline void CheckGONoDamageImmuneId(GameObjectInfo const* goInfo, uint32 dataN, uint32 N)
{
    // 0/1 correct values
    if (dataN <= 1)
    {
        return;
    }

    sLog.outErrorDb("Gameobject (Entry: %u GoType: %u) have data%d=%u but expected boolean (0/1) noDamageImmune field value.",
                    goInfo->id, goInfo->type, N, dataN);
}

/**
 * @brief Validates a boolean consumable flag in gameobject data.
 *
 * @param goInfo The gameobject template being checked.
 * @param dataN The field value.
 * @param N The source data index.
 */
inline void CheckGOConsumable(GameObjectInfo const* goInfo, uint32 dataN, uint32 N)
{
    // 0/1 correct values
    if (dataN <= 1)
    {
        return;
    }

    sLog.outErrorDb("Gameobject (Entry: %u GoType: %u) have data%d=%u but expected boolean (0/1) consumable field value.",
                    goInfo->id, goInfo->type, N, dataN);
}

/**
 * @brief Validates and fixes minimum capture time data for a gameobject.
 *
 * @param goInfo The gameobject template being checked.
 * @param dataN The minimum capture time value.
 * @param N The source data index.
 */
inline void CheckAndFixGOCaptureMinTime(GameObjectInfo const* goInfo, uint32 const& dataN, uint32 N)
{
    if (dataN > 0)
    {
        return;
    }

    sLog.outErrorDb("Gameobject (Entry: %u GoType: %u) has data%d=%u but minTime field value must be > 0.",
                    goInfo->id, goInfo->type, N, dataN);

    // prevent division through 0 exception
    const_cast<uint32&>(dataN) = 1;
}

/**
 * @brief Loads gameobject templates and validates type-specific fields.
 */
void ObjectMgr::LoadGameobjectInfo()
{
    SQLGameObjectLoader loader;
    loader.Load(sGOStorage);

    // some checks
    for (SQLStorageBase::SQLSIterator<GameObjectInfo> itr = sGOStorage.getDataBegin<GameObjectInfo>(); itr < sGOStorage.getDataEnd<GameObjectInfo>(); ++itr)
    {
        GameObjectInfo const* goInfo = itr.getValue();

        if (goInfo->size <= 0.0f)                           // prevent use too small scales
        {
            ERROR_DB_STRICT_LOG("Gameobject (Entry: %u GoType: %u) have too small size=%f",
                                goInfo->id, goInfo->type, goInfo->size);
            const_cast<GameObjectInfo*>(goInfo)->size =  DEFAULT_OBJECT_SCALE;
        }

        // some GO types have unused go template, check goInfo->displayId at GO spawn data loading or ignore

        switch (goInfo->type)
        {
            case GAMEOBJECT_TYPE_DOOR:                      // 0
            {
                if (goInfo->door.lockId)
                {
                    CheckGOLockId(goInfo, goInfo->door.lockId, 1);
                }
                CheckGONoDamageImmuneId(goInfo, goInfo->door.noDamageImmune, 3);
                break;
            }
            case GAMEOBJECT_TYPE_BUTTON:                    // 1
            {
                if (goInfo->button.lockId)
                {
                    CheckGOLockId(goInfo, goInfo->button.lockId, 1);
                }
                if (goInfo->button.linkedTrapId)            // linked trap
                {
                    CheckGOLinkedTrapId(goInfo, goInfo->button.linkedTrapId, 3);
                }
                CheckGONoDamageImmuneId(goInfo, goInfo->button.noDamageImmune, 4);
                break;
            }
            case GAMEOBJECT_TYPE_QUESTGIVER:                // 2
            {
                if (goInfo->questgiver.lockId)
                {
                    CheckGOLockId(goInfo, goInfo->questgiver.lockId, 0);
                }
                CheckGONoDamageImmuneId(goInfo, goInfo->questgiver.noDamageImmune, 5);
                break;
            }
            case GAMEOBJECT_TYPE_CHEST:                     // 3
            {
                if (goInfo->chest.lockId)
                {
                    CheckGOLockId(goInfo, goInfo->chest.lockId, 0);
                }

                CheckGOConsumable(goInfo, goInfo->chest.consumable, 3);

                if (goInfo->chest.linkedTrapId)             // linked trap
                {
                    CheckGOLinkedTrapId(goInfo, goInfo->chest.linkedTrapId, 7);
                }
                break;
            }
            case GAMEOBJECT_TYPE_TRAP:                      // 6
            {
                if (goInfo->trap.lockId)
                {
                    CheckGOLockId(goInfo, goInfo->trap.lockId, 0);
                }
                /* disable check for while, too many nonexistent spells
                if (goInfo->trap.spellId)                   // spell
                {
                    CheckGOSpellId(goInfo,goInfo->trap.spellId,3);
                }
                */
                break;
            }
            case GAMEOBJECT_TYPE_CHAIR:                     // 7
                CheckAndFixGOChairHeightId(goInfo, goInfo->chair.height, 1);
                break;
            case GAMEOBJECT_TYPE_SPELL_FOCUS:               // 8
            {
                if (goInfo->spellFocus.focusId)
                {
                    if (!sSpellFocusObjectStore.LookupEntry(goInfo->spellFocus.focusId))
                        sLog.outErrorDb("Gameobject (Entry: %u GoType: %u) have data0=%u but SpellFocus (Id: %u) not exist.",
                                        goInfo->id, goInfo->type, goInfo->spellFocus.focusId, goInfo->spellFocus.focusId);
                }

                if (goInfo->spellFocus.linkedTrapId)        // linked trap
                {
                    CheckGOLinkedTrapId(goInfo, goInfo->spellFocus.linkedTrapId, 2);
                }
                break;
            }
            case GAMEOBJECT_TYPE_GOOBER:                    // 10
            {
                if (goInfo->goober.lockId)
                {
                    CheckGOLockId(goInfo, goInfo->goober.lockId, 0);
                }

                CheckGOConsumable(goInfo, goInfo->goober.consumable, 3);

                if (goInfo->goober.pageId)                  // pageId
                {
                    if (!sPageTextStore.LookupEntry<PageText>(goInfo->goober.pageId))
                        sLog.outErrorDb("Gameobject (Entry: %u GoType: %u) have data7=%u but PageText (Entry %u) not exist.",
                                        goInfo->id, goInfo->type, goInfo->goober.pageId, goInfo->goober.pageId);
                }
                /* disable check for while, too many nonexistent spells
                if (goInfo->goober.spellId)                 // spell
                {
                    CheckGOSpellId(goInfo,goInfo->goober.spellId,10);
                }
                */
                CheckGONoDamageImmuneId(goInfo, goInfo->goober.noDamageImmune, 11);
                if (goInfo->goober.linkedTrapId)            // linked trap
                {
                    CheckGOLinkedTrapId(goInfo, goInfo->goober.linkedTrapId, 12);
                }
                break;
            }
            case GAMEOBJECT_TYPE_AREADAMAGE:                // 12
            {
                if (goInfo->areadamage.lockId)
                {
                    CheckGOLockId(goInfo, goInfo->areadamage.lockId, 0);
                }
                break;
            }
            case GAMEOBJECT_TYPE_CAMERA:                    // 13
            {
                if (goInfo->camera.lockId)
                {
                    CheckGOLockId(goInfo, goInfo->camera.lockId, 0);
                }
                break;
            }
            case GAMEOBJECT_TYPE_MO_TRANSPORT:              // 15
            {
                if (goInfo->moTransport.taxiPathId)
                {
                    if (goInfo->moTransport.taxiPathId >= sTaxiPathNodesByPath.size() || sTaxiPathNodesByPath[goInfo->moTransport.taxiPathId].empty())
                        sLog.outErrorDb("Gameobject (Entry: %u GoType: %u) have data0=%u but TaxiPath (Id: %u) not exist.",
                                        goInfo->id, goInfo->type, goInfo->moTransport.taxiPathId, goInfo->moTransport.taxiPathId);
                }
                break;
            }
            case GAMEOBJECT_TYPE_SUMMONING_RITUAL:          // 18
            {
                /* disable check for while, too many nonexistent spells
                // always must have spell
                CheckGOSpellId(goInfo,goInfo->summoningRitual.spellId,1);
                */
                break;
            }
            case GAMEOBJECT_TYPE_SPELLCASTER:               // 22
            {
                // always must have spell
                CheckGOSpellId(goInfo, goInfo->spellcaster.spellId, 0);
                break;
            }
            case GAMEOBJECT_TYPE_FLAGSTAND:                 // 24
            {
                if (goInfo->flagstand.lockId)
                {
                    CheckGOLockId(goInfo, goInfo->flagstand.lockId, 0);
                }
                CheckGONoDamageImmuneId(goInfo, goInfo->flagstand.noDamageImmune, 5);
                break;
            }
            case GAMEOBJECT_TYPE_FISHINGHOLE:               // 25
            {
                if (goInfo->fishinghole.lockId)
                {
                    CheckGOLockId(goInfo, goInfo->fishinghole.lockId, 4);
                }
                break;
            }
            case GAMEOBJECT_TYPE_FLAGDROP:                  // 26
            {
                if (goInfo->flagdrop.lockId)
                {
                    CheckGOLockId(goInfo, goInfo->flagdrop.lockId, 0);
                }
                CheckGONoDamageImmuneId(goInfo, goInfo->flagdrop.noDamageImmune, 3);
                break;
            }
            case GAMEOBJECT_TYPE_CAPTURE_POINT:             // 29
            {
                CheckAndFixGOCaptureMinTime(goInfo, goInfo->capturePoint.minTime, 16);
                break;
            }
        }
    }

    sLog.outString(">> Loaded %u game object templates", sGOStorage.GetRecordCount());
    sLog.outString();
}

/**
 * @brief Loads exploration base experience values by level.
 */
void ObjectMgr::LoadExplorationBaseXP()
{
    uint32 count = 0;
    QueryResult* result = WorldDatabase.Query("SELECT `level`,`basexp` FROM `exploration_basexp`");

    if (!result)
    {
        BarGoLink bar(1);
        bar.step();
        sLog.outString(">> Loaded %u BaseXP definitions", count);
        sLog.outString();
        return;
    }

    BarGoLink bar(result->GetRowCount());

    do
    {
        bar.step();

        Field* fields = result->Fetch();
        uint32 level  = fields[0].GetUInt32();
        uint32 basexp = fields[1].GetUInt32();
        mBaseXPTable[level] = basexp;
        ++count;
    }
    while (result->NextRow());

    delete result;

    sLog.outString(">> Loaded %u BaseXP definitions", count);
    sLog.outString();
}

/**
 * @brief Gets the exploration base experience for a level.
 *
 * @param level The player level.
 * @return The configured base exploration XP, or 0 if missing.
 */
uint32 ObjectMgr::GetBaseXP(uint32 level) const
{
    BaseXPMap::const_iterator itr = mBaseXPTable.find(level);
    return itr != mBaseXPTable.end() ? itr->second : 0;
}

/**
 * @brief Gets the XP required for the next player level.
 *
 * @param level The current player level index.
 * @return The XP requirement, or 0 if out of range.
 */
uint32 ObjectMgr::GetXPForLevel(uint32 level) const
{
    if (level < mPlayerXPperLevel.size())
    {
        return mPlayerXPperLevel[level];
    }
    return 0;
}

/**
 * @brief Loads pet name fragments used for random pet naming.
 */
void ObjectMgr::LoadPetNames()
{
    uint32 count = 0;
    QueryResult* result = WorldDatabase.Query("SELECT `word`,`entry`,`half` FROM `pet_name_generation`");

    if (!result)
    {
        BarGoLink bar(1);
        bar.step();
        sLog.outString(">> Loaded %u pet name parts", count);
        sLog.outString();
        return;
    }

    BarGoLink bar(result->GetRowCount());

    do
    {
        bar.step();

        Field* fields = result->Fetch();
        std::string word = fields[0].GetString();
        uint32 entry     = fields[1].GetUInt32();
        bool   half      = fields[2].GetBool();
        if (half)
        {
            PetHalfName1[entry].push_back(word);
        }
        else
        {
            PetHalfName0[entry].push_back(word);
        }
        ++count;
    }
    while (result->NextRow());
    delete result;

    sLog.outString(">> Loaded %u pet name parts", count);
    sLog.outString();
}

/**
 * @brief Initializes the next generated pet number from existing pets.
 */
void ObjectMgr::LoadPetNumber()
{
    QueryResult* result = CharacterDatabase.Query("SELECT MAX(`id`) FROM `character_pet`");
    if (result)
    {
        Field* fields = result->Fetch();
        m_PetNumbers.Set(fields[0].GetUInt32() + 1);
        delete result;
    }

    BarGoLink bar(1);
    bar.step();

    sLog.outString(">> Loaded the max pet number: %d", m_PetNumbers.GetNextAfterMaxUsed() - 1);
    sLog.outString();
}

/**
 * @brief Generates a random or fallback pet name for a creature entry.
 *
 * @param entry The creature entry id.
 * @return The generated pet name.
 */
std::string ObjectMgr::GeneratePetName(uint32 entry)
{
    std::vector<std::string>& list0 = PetHalfName0[entry];
    std::vector<std::string>& list1 = PetHalfName1[entry];

    if (list0.empty() || list1.empty())
    {
        CreatureInfo const* cinfo = GetCreatureTemplate(entry);
        char const* petname = GetPetName(cinfo->Family, sWorld.GetDefaultDbcLocale());
        if (!petname)
        {
            petname = cinfo->Name;
        }
        return std::string(petname);
    }

    return *(list0.begin() + urand(0, list0.size() - 1)) + *(list1.begin() + urand(0, list1.size() - 1));
}

/**
 * @brief Loads persistent corpse records from the character database.
 */
void ObjectMgr::LoadCorpses()
{
    uint32 count = 0;
    //                                                    0            1       2                  3                  4                  5                   6
    QueryResult* result = CharacterDatabase.Query("SELECT `corpse`.`guid`, `player`, `corpse`.`position_x`, `corpse`.`position_y`, `corpse`.`position_z`, `corpse`.`orientation`, `corpse`.`map`, "
                          //   7     8            9         10      11    12     13           14            15              16       17
                          "`time`, `corpse_type`, `instance`, `gender`, `race`, `class`, `playerBytes`, `playerBytes2`, `equipmentCache`, `guildId`, `playerFlags` FROM `corpse` "
                          "JOIN `characters` ON `player` = `characters`.`guid` "
                          "LEFT JOIN `guild_member` ON `player`=`guild_member`.`guid` WHERE `corpse_type` <> 0");

    if (!result)
    {
        BarGoLink bar(1);
        bar.step();
        sLog.outString(">> Loaded %u corpses", count);
        sLog.outString();
        return;
    }

    BarGoLink bar(result->GetRowCount());

    do
    {
        bar.step();

        Field* fields = result->Fetch();

        uint32 guid = fields[0].GetUInt32();

        Corpse* corpse = new Corpse;
        if (!corpse->LoadFromDB(guid, fields))
        {
            delete corpse;
            continue;
        }

        sObjectAccessor.AddCorpse(corpse);

        ++count;
    }
    while (result->NextRow());
    delete result;

    sLog.outString(">> Loaded %u corpses", count);
    sLog.outString();
}

/**
 * @brief Loads point-of-interest definitions used by NPC map markers.
 */
void ObjectMgr::LoadPointsOfInterest()
{
    mPointsOfInterest.clear();                              // need for reload case

    uint32 count = 0;

    //                                                0      1  2  3      4     5
    QueryResult* result = WorldDatabase.Query("SELECT `entry`, `x`, `y`, `icon`, `flags`, `data`, `icon_name` FROM `points_of_interest`");

    if (!result)
    {
        BarGoLink bar(1);
        bar.step();
        sLog.outErrorDb(">> Loaded 0 Points of Interest definitions. DB table `points_of_interest` is empty.");
        sLog.outString();
        return;
    }

    BarGoLink bar(result->GetRowCount());

    do
    {
        Field* fields = result->Fetch();
        bar.step();

        uint32 point_id = fields[0].GetUInt32();

        PointOfInterest POI;
        POI.x                    = fields[1].GetFloat();
        POI.y                    = fields[2].GetFloat();
        POI.icon                 = fields[3].GetUInt32();
        POI.flags                = fields[4].GetUInt32();
        POI.data                 = fields[5].GetUInt32();
        POI.icon_name            = fields[6].GetCppString();

        if (!MaNGOS::IsValidMapCoord(POI.x, POI.y))
        {
            sLog.outErrorDb("Table `points_of_interest` (Entry: %u) have invalid coordinates (X: %f Y: %f), ignored.", point_id, POI.x, POI.y);
            continue;
        }

        mPointsOfInterest[point_id] = POI;

        ++count;
    }
    while (result->NextRow());

    delete result;

    sLog.outString(">> Loaded %u Points of Interest definitions", count);
    sLog.outString();
}

static char SERVER_SIDE_SPELL[] = "MaNGOS server-side spell";

struct SQLSpellLoader : public SQLStorageLoaderBase<SQLSpellLoader, SQLHashStorage>
{
    template<class S, class D>
    void default_fill(uint32 field_pos, S src, D& dst)
    {
        if (field_pos == LOADED_SPELLDBC_FIELD_POS_EQUIPPED_ITEM_CLASS)
        {
            dst = D(-1);
        }
        else
        {
            dst = D(src);
        }
    }

    void default_fill_to_str(uint32 field_pos, char const* /*src*/, char*& dst)
    {
        if (field_pos == LOADED_SPELLDBC_FIELD_POS_SPELLNAME_0)
        {
            dst = new char[sizeof(SERVER_SIDE_SPELL)]; // SERVER_SIDE_SPELL is (const char *)
            memcpy(dst, SERVER_SIDE_SPELL, sizeof(SERVER_SIDE_SPELL) );
        }
        else
        {
            dst = new char[1];
            *dst = 0;
        }
    }
};

void ObjectMgr::LoadSpellTemplate()
{
    SQLSpellLoader loader;
    loader.Load(sSpellTemplate);

    sLog.outString(">> Loaded %u spell definitions", sSpellTemplate.GetRecordCount());
    sLog.outString();

    for (uint32 i = 1; i < sSpellTemplate.GetMaxEntry(); ++i)
    {
        // check data correctness
        SpellEntry const* spellEntry = sSpellTemplate.LookupEntry<SpellEntry>(i);
        if (!spellEntry)
        {
            continue;
        }

        // insert serverside spell data
        if (sSpellStore.GetNumRows() <= i)
        {
            sLog.outErrorDb("Loading Spell Template for spell %u, index out of bounds (max = %u)", i, sSpellStore.GetNumRows());
            continue;
        }
        else
        {
            sSpellStore.InsertEntry(const_cast<SpellEntry*>(spellEntry), i);
        }
    }
}

/**
 * @brief Removes stored creature spawn data and its grid mapping.
 *
 * @param guid The creature spawn GUID.
 */
void ObjectMgr::DeleteCreatureData(uint32 guid)
{
    // remove mapid*cellid -> guid_set map
    CreatureData const* data = GetCreatureData(guid);
    if (data)
    {
        RemoveCreatureFromGrid(guid, data);
    }

    mCreatureDataMap.erase(guid);
}

/**
 * @brief Removes stored gameobject spawn data and its grid mapping.
 *
 * @param guid The gameobject spawn GUID.
 */
void ObjectMgr::DeleteGOData(uint32 guid)
{
    // remove mapid*cellid -> guid_set map
    GameObjectData const* data = GetGOData(guid);
    if (data)
    {
        RemoveGameobjectFromGrid(guid, data);
    }

    mGameObjectDataMap.erase(guid);
}

/**
 * @brief Adds corpse cell lookup data for a player corpse.
 *
 * @param mapid The map id.
 * @param cellid The cell id.
 * @param player_guid The owning player GUID low part.
 * @param instance The instance id.
 */
void ObjectMgr::AddCorpseCellData(uint32 mapid, uint32 cellid, uint32 player_guid, uint32 instance)
{
    // corpses are always added to spawn mode 0 and they are spawned by their instance id
    CellObjectGuids& cell_guids = mMapObjectGuids[MAKE_PAIR32(mapid, 0)][cellid];
    cell_guids.corpses[player_guid] = instance;
}

/**
 * @brief Removes corpse cell lookup data for a player corpse.
 *
 * @param mapid The map id.
 * @param cellid The cell id.
 * @param player_guid The owning player GUID low part.
 */
void ObjectMgr::DeleteCorpseCellData(uint32 mapid, uint32 cellid, uint32 player_guid)
{
    // corpses are always added to spawn mode 0 and they are spawned by their instance id
    CellObjectGuids& cell_guids = mMapObjectGuids[MAKE_PAIR32(mapid, 0)][cellid];
    cell_guids.corpses.erase(player_guid);
}

/**
 * @brief Loads quest relation mappings for a specific actor and role.
 *
 * @param map The destination relations map.
 * @param actor The quest actor type.
 * @param role The quest relation role.
 */
void ObjectMgr::LoadQuestRelationsHelper(QuestRelationsMap& map, QuestActor actor, QuestRole role)
{
    map.clear();                                            // need for reload case

    uint32 count = 0;

    QueryResult* result = WorldDatabase.PQuery("SELECT `entry`, `quest` FROM `quest_relations` WHERE `actor` = %d AND `role` = %d", actor, role);

    if (!result)
    {
        BarGoLink bar(1);

        bar.step();

        sLog.outString();
        sLog.outErrorDb(">> Loaded 0 quest relations. DB table `quest_relations` is empty.");
        return;
    }

    BarGoLink bar(result->GetRowCount());

    do
    {
        Field* fields = result->Fetch();
        bar.step();

        uint32 id    = fields[0].GetUInt32();
        uint32 quest = fields[1].GetUInt32();

        if (mQuestTemplates.find(quest) == mQuestTemplates.end())
        {
            sLog.outErrorDb("Table `quest_relations`: Quest %u listed for entry %u does not exist.", quest, id);
            continue;
        }

        map.insert(QuestRelationsMap::value_type(id, quest));

        ++count;
    }
    while (result->NextRow());

    delete result;

    sLog.outString();
    sLog.outString(">> Loaded %u %s quest %s from `quest_relations`", count, (actor == 1) ? "gameobject" : "creature", (role == 1) ? "takers" : "givers");
}

/**
 * @brief Loads quest-giver relations for gameobjects.
 */
void ObjectMgr::LoadGameobjectQuestRelations()
{
    LoadQuestRelationsHelper(m_GOQuestRelations, QA_GAMEOBJECT, QR_START);

    for (QuestRelationsMap::iterator itr = m_GOQuestRelations.begin(); itr != m_GOQuestRelations.end(); ++itr)
    {
        GameObjectInfo const* goInfo = GetGameObjectInfo(itr->first);
        if (!goInfo)
        {
            sLog.outErrorDb("Table `quest_relations` have data for nonexistent gameobject entry (%u) and existing quest %u", itr->first, itr->second);
        }
        else if (goInfo->type != GAMEOBJECT_TYPE_QUESTGIVER)
        {
            sLog.outErrorDb("Table `quest_relations` have data gameobject entry (%u) for quest %u, but GO is not GAMEOBJECT_TYPE_QUESTGIVER", itr->first, itr->second);
        }
    }
}

/**
 * @brief Loads quest-completion relations for gameobjects.
 */
void ObjectMgr::LoadGameobjectInvolvedRelations()
{
    LoadQuestRelationsHelper(m_GOQuestInvolvedRelations, QA_GAMEOBJECT, QR_END);

    for (QuestRelationsMap::iterator itr = m_GOQuestInvolvedRelations.begin(); itr != m_GOQuestInvolvedRelations.end(); ++itr)
    {
        GameObjectInfo const* goInfo = GetGameObjectInfo(itr->first);
        if (!goInfo)
        {
            sLog.outErrorDb("Table `quest_relations` have data for nonexistent gameobject entry (%u) and existing quest %u", itr->first, itr->second);
        }
        else if (goInfo->type != GAMEOBJECT_TYPE_QUESTGIVER)
        {
            sLog.outErrorDb("Table `quest_relations` have data gameobject entry (%u) for quest %u, but GO is not GAMEOBJECT_TYPE_QUESTGIVER", itr->first, itr->second);
        }
    }
}

/**
 * @brief Loads quest-giver relations for creatures.
 */
void ObjectMgr::LoadCreatureQuestRelations()
{
    LoadQuestRelationsHelper(m_CreatureQuestRelations, QA_CREATURE, QR_START);

    for (QuestRelationsMap::iterator itr = m_CreatureQuestRelations.begin(); itr != m_CreatureQuestRelations.end(); ++itr)
    {
        CreatureInfo const* cInfo = GetCreatureTemplate(itr->first);
        if (!cInfo)
        {
            sLog.outErrorDb("Table `quest_relations` have data for nonexistent creature entry (%u) and existing quest %u", itr->first, itr->second);
        }
        else if (!(cInfo->NpcFlags & UNIT_NPC_FLAG_QUESTGIVER))
        {
            sLog.outErrorDb("Table `quest_relations` has creature entry (%u) for quest %u, but npcflag does not include UNIT_NPC_FLAG_QUESTGIVER", itr->first, itr->second);
        }
    }
}

/**
 * @brief Loads quest-completion relations for creatures.
 */
void ObjectMgr::LoadCreatureInvolvedRelations()
{
    LoadQuestRelationsHelper(m_CreatureQuestInvolvedRelations, QA_CREATURE, QR_END);

    for (QuestRelationsMap::iterator itr = m_CreatureQuestInvolvedRelations.begin(); itr != m_CreatureQuestInvolvedRelations.end(); ++itr)
    {
        CreatureInfo const* cInfo = GetCreatureTemplate(itr->first);
        if (!cInfo)
        {
            sLog.outErrorDb("Table `quest_relations` have data for nonexistent creature entry (%u) and existing quest %u", itr->first, itr->second);
        }
        else if (!(cInfo->NpcFlags & UNIT_NPC_FLAG_QUESTGIVER))
        {
            sLog.outErrorDb("Table `quest_relations` has creature entry (%u) for quest %u, but npcflag does not include UNIT_NPC_FLAG_QUESTGIVER", itr->first, itr->second);
        }
    }
}

/**
 * @brief Gets the internal locale index for a locale constant.
 *
 * @param loc The locale constant.
 * @return The locale index, or -1 for the default locale.
 */
int ObjectMgr::GetIndexForLocale(LocaleConstant loc)
{
    if (loc == LOCALE_enUS)
    {
        return -1;
    }

    for (size_t i = 0; i < m_LocalForIndex.size(); ++i)
    {
        if (m_LocalForIndex[i] == loc)
        {
            return i;
        }
    }

    return -1;
}

/**
 * @brief Gets the locale constant mapped to an internal locale index.
 *
 * @param i The locale index.
 * @return The mapped locale constant, or the default locale if out of range.
 */
LocaleConstant ObjectMgr::GetLocaleForIndex(int i)
{
    if (i < 0 || i >= (int32)m_LocalForIndex.size())
    {
        return LOCALE_enUS;
    }

    return m_LocalForIndex[i];
}

/**
 * @brief Gets or creates the internal locale index for a locale constant.
 *
 * @param loc The locale constant.
 * @return The locale index, or -1 for the default locale.
 */
int ObjectMgr::GetOrNewIndexForLocale(LocaleConstant loc)
{
    if (loc == LOCALE_enUS)
    {
        return -1;
    }

    for (size_t i = 0; i < m_LocalForIndex.size(); ++i)
    {
        if (m_LocalForIndex[i] == loc)
        {
            return i;
        }
    }

    m_LocalForIndex.push_back(loc);
    return m_LocalForIndex.size() - 1;
}

/**
 * @brief Builds the set of gameobject entries that must activate for quests.
 */
void ObjectMgr::LoadGameObjectForQuests()
{
    mGameObjectForQuestSet.clear();                         // need for reload case

    if (!sGOStorage.GetMaxEntry())
    {
        BarGoLink bar(1);
        bar.step();
        sLog.outString(">> Loaded 0 GameObjects for quests");
        sLog.outString();
        return;
    }

    BarGoLink bar(sGOStorage.GetRecordCount());
    uint32 count = 0;

    // collect GO entries for GO that must activated
    for (SQLStorageBase::SQLSIterator<GameObjectInfo> itr = sGOStorage.getDataBegin<GameObjectInfo>(); itr < sGOStorage.getDataEnd<GameObjectInfo>(); ++itr)
    {
        bar.step();
        switch (itr->type)
        {
            case GAMEOBJECT_TYPE_QUESTGIVER:
            {
                if (m_GOQuestRelations.find(itr->id) != m_GOQuestRelations.end() ||
                    m_GOQuestInvolvedRelations.find(itr->id) != m_GOQuestInvolvedRelations.end())
                {
                    mGameObjectForQuestSet.insert(itr->id);
                    ++count;
                }

                break;
            }
            case GAMEOBJECT_TYPE_CHEST:
            {
                // scan GO chest with loot including quest items
                uint32 loot_id = itr->GetLootId();

                // always activate to quest, GO may not have loot, OR find if GO has loot for quest.
                if (itr->chest.questId || LootTemplates_Gameobject.HaveQuestLootFor(loot_id))
                {
                    mGameObjectForQuestSet.insert(itr->id);
                    ++count;
                }
                break;
            }
            case GAMEOBJECT_TYPE_GENERIC:
            {
                if (itr->_generic.questID)                  // quest related objects, has visual effects
                {
                    mGameObjectForQuestSet.insert(itr->id);
                    ++count;
                }
                break;
            }
            case GAMEOBJECT_TYPE_SPELL_FOCUS:
            {
                if (itr->spellFocus.questID)                // quest related objects, has visual effect
                {
                    mGameObjectForQuestSet.insert(itr->id);
                    ++count;
                }
                break;
            }
            case GAMEOBJECT_TYPE_GOOBER:
            {
                if (itr->goober.questId)                    // quests objects
                {
                    mGameObjectForQuestSet.insert(itr->id);
                    ++count;
                }
                break;
            }
            default:
                break;
        }
    }

    sLog.outString(">> Loaded %u GameObjects for quests", count);
    sLog.outString();
}

/**
 * @brief Logs a formatted script text lookup error for a string entry.
 *
 * @param entry The string entry id.
 * @param text The printf-style error text.
 */
inline void _DoStringError(int32 entry, char const* text, ...)
{
    MANGOS_ASSERT(text);

    char buf[256];
    va_list ap;
    va_start(ap, text);
    vsnprintf(buf, 256, text, ap);
    va_end(ap);

    if (entry <= MAX_CREATURE_AI_TEXT_STRING_ID)            // script library error
    {
        sLog.outErrorScriptLib("%s", buf);
    }
    else if (entry <= MIN_CREATURE_AI_TEXT_STRING_ID)       // eventAI error
    {
        sLog.outErrorEventAI("%s", buf);
    }
    else if (entry < MIN_DB_SCRIPT_STRING_ID)               // mangos string error
    {
        sLog.outError("%s", buf);
    }
    else // if (entry > MIN_DB_SCRIPT_STRING_ID)            // DB script text error
    {
        sLog.outErrorDb("DB-SCRIPTS: %s", buf);
    }
}

/**
 * @brief Loads localized string templates from a database table.
 *
 * @param db The database to query.
 * @param table The source table name.
 * @param min_value The inclusive lower id bound.
 * @param max_value The exclusive upper id bound.
 * @param extra_content true to also load sound/chat metadata.
 * @return true if the load succeeded; otherwise, false.
 */
bool ObjectMgr::LoadMangosStrings(DatabaseType& db, char const* table, int32 min_value, int32 max_value, bool extra_content)
{
    int32 start_value = min_value;
    int32 end_value   = max_value;
    // some string can have negative indexes range
    if (start_value < 0)
    {
        if (end_value >= start_value)
        {
            sLog.outErrorDb("Table '%s' attempt loaded with invalid range (%d - %d), strings not loaded.", table, min_value, max_value);
            return false;
        }

        // real range (max+1,min+1) exaple: (-10,-1000) -> -999...-10+1
        std::swap(start_value, end_value);
        ++start_value;
        ++end_value;
    }
    else
    {
        if (start_value >= end_value)
        {
            sLog.outErrorDb("Table '%s' attempt loaded with invalid range (%d - %d), strings not loaded.", table, min_value, max_value);
            return false;
        }
    }

    // cleanup affected map part for reloading case
    for (MangosStringLocaleMap::iterator itr = mMangosStringLocaleMap.begin(); itr != mMangosStringLocaleMap.end();)
    {
        if (itr->first >= start_value && itr->first < end_value)
        {
            mMangosStringLocaleMap.erase(itr++);
        }
        else
        {
            ++itr;
        }
    }

    sLog.outString("Loading texts from %s%s", table, extra_content ? ", with additional data" : "");

    QueryResult* result = db.PQuery("SELECT `entry`,`content_default`,`content_loc1`,`content_loc2`,`content_loc3`,`content_loc4`,`content_loc5`,`content_loc6`,`content_loc7`,`content_loc8` %s FROM %s",
                                    extra_content ? ",sound,type,language,emote" : "", table);

    if (!result)
    {
        BarGoLink bar(1);

        bar.step();

        sLog.outString();
        if (min_value == MIN_MANGOS_STRING_ID)              // error only in case internal strings
        {
            sLog.outErrorDb(">> Loaded 0 mangos strings. DB table `%s` is empty. Can not continue.", table);
        }
        else
        {
            sLog.outString(">> Loaded 0 string templates. DB table `%s` is empty.", table);
        }
        return false;
    }

    uint32 count = 0;

    BarGoLink bar(result->GetRowCount());

    do
    {
        Field* fields = result->Fetch();
        bar.step();

        int32 entry = fields[0].GetInt32();

        if (entry == 0)
        {
            _DoStringError(start_value, "Table `%s` contain reserved entry 0, ignored.", table);
            continue;
        }
        else if (entry < start_value || entry >= end_value)
        {
            _DoStringError(start_value, "Table `%s` contain entry %i out of allowed range (%d - %d), ignored.", table, entry, min_value, max_value);
            continue;
        }

        MangosStringLocale& data = mMangosStringLocaleMap[entry];

        if (!data.Content.empty())
        {
            _DoStringError(entry, "Table `%s` contain data for already loaded entry  %i (from another table?), ignored.", table, entry);
            continue;
        }

        data.Content.resize(1);
        ++count;

        // 0 -> default, idx in to idx+1
        data.Content[0] = fields[1].GetCppString();

        for (int i = 1; i < MAX_LOCALE; ++i)
        {
            std::string str = fields[i + 1].GetCppString();
            if (!str.empty())
            {
                int idx = GetOrNewIndexForLocale(LocaleConstant(i));
                if (idx >= 0)
                {
                    // 0 -> default, idx in to idx+1
                    if ((int32)data.Content.size() <= idx + 1)
                    {
                        data.Content.resize(idx + 2);
                    }

                    data.Content[idx + 1] = str;
                }
            }
        }

        // Load additional string content if necessary
        if (extra_content)
        {
            data.SoundId     = fields[10].GetUInt32();
            data.Type        = fields[11].GetUInt32();
            data.LanguageId  = Language(fields[12].GetUInt32());
            data.Emote       = fields[13].GetUInt32();

            if (data.SoundId && !sSoundEntriesStore.LookupEntry(data.SoundId))
            {
                _DoStringError(entry, "Entry %i in table `%s` has soundId %u but sound does not exist.", entry, table, data.SoundId);
                data.SoundId = 0;
            }

            if (!GetLanguageDescByID(data.LanguageId))
            {
                _DoStringError(entry, "Entry %i in table `%s` using Language %u but Language does not exist.", entry, table, uint32(data.LanguageId));
                data.LanguageId = LANG_UNIVERSAL;
            }

            if (data.Type > CHAT_TYPE_ZONE_YELL)
            {
                _DoStringError(entry, "Entry %i in table `%s` has Type %u but this Chat Type does not exist.", entry, table, data.Type);
                data.Type = CHAT_TYPE_SAY;
            }

            if (data.Emote && !sEmotesStore.LookupEntry(data.Emote))
            {
                _DoStringError(entry, "Entry %i in table `%s` has Emote %u but emote does not exist.", entry, table, data.Emote);
                data.Emote = EMOTE_ONESHOT_NONE;
            }
        }
    }
    while (result->NextRow());

    delete result;

    if (min_value == MIN_MANGOS_STRING_ID)
    {
        sLog.outString(">> Loaded %u MaNGOS strings from table %s", count, table);
    }
    else
    {
        sLog.outString(">> Loaded %u %s templates from %s", count, extra_content ? "text" : "string", table);
    }
    sLog.outString();

    m_loadedStringCount[min_value] = count;

    return true;
}

/**
 * @brief Gets a localized MaNGOS string entry.
 *
 * @param entry The string entry id.
 * @param locale_idx The internal locale index.
 * @return The localized text, or a fallback error string.
 */
const char* ObjectMgr::GetMangosString(int32 entry, int locale_idx) const
{
    // locale_idx==-1 -> default, locale_idx >= 0 in to idx+1
    // Content[0] always exist if exist MangosStringLocale
    if (MangosStringLocale const* msl = GetMangosStringLocale(entry))
    {
        if ((int32)msl->Content.size() > locale_idx + 1 && !msl->Content[locale_idx + 1].empty())
        {
            return msl->Content[locale_idx + 1].c_str();
        }
        else
        {
            return msl->Content[0].c_str();
        }
    }

    _DoStringError(entry, "Entry %i not found but requested", entry);

    return "<error>";
}

/**
 * @brief Loads base fishing skill requirements for areas.
 */
void ObjectMgr::LoadFishingBaseSkillLevel()
{
    mFishingBaseForArea.clear();                            // for reload case

    uint32 count = 0;
    QueryResult* result = WorldDatabase.Query("SELECT `entry`,`skill` FROM `skill_fishing_base_level`");

    if (!result)
    {
        BarGoLink bar(1);
        bar.step();
        sLog.outErrorDb(">> Loaded `skill_fishing_base_level`, table is empty!");
        sLog.outString();
        return;
    }

    BarGoLink bar(result->GetRowCount());

    do
    {
        bar.step();

        Field* fields = result->Fetch();
        uint32 entry  = fields[0].GetUInt32();
        int32 skill   = fields[1].GetInt32();

        AreaTableEntry const* fArea = GetAreaEntryByAreaID(entry);
        if (!fArea)
        {
            sLog.outErrorDb("AreaId %u defined in `skill_fishing_base_level` does not exist", entry);
            continue;
        }

        mFishingBaseForArea[entry] = skill;
        ++count;
    }
    while (result->NextRow());

    delete result;

    sLog.outString(">> Loaded %u areas for fishing base skill level", count);
    sLog.outString();
}

// Check if a player meets condition conditionId
bool ObjectMgr::IsPlayerMeetToCondition(uint16 conditionId, Player const* pPlayer, Map const* map, WorldObject const* source, ConditionSource conditionSourceType) const
{
    if (const PlayerCondition* condition = sConditionStorage.LookupEntry<PlayerCondition>(conditionId))
    {
        return condition->Meets(pPlayer, map, source, conditionSourceType);
    }

    return false;
}

bool ObjectMgr::CheckDeclinedNames(const std::wstring& mainpart, DeclinedName const& names)
{
    for (int i = 0; i < MAX_DECLINED_NAME_CASES; ++i)
    {
        std::wstring wname;
        if (!Utf8toWStr(names.name[i], wname))
        {
            return false;
        }

        if (mainpart != GetMainPartOfName(wname, i + 1))
        {
            return false;
        }
    }
    return true;
}

// Attention: make sure to keep this list in sync with ConditionSource to avoid array
//            out of bounds access! It is accessed with ConditionSource as index!
char const* conditionSourceToStr[] =
{
    "loot system",
    "referencing loot",
    "gossip menu",
    "gossip menu option",
    "event AI",
    "hardcoded",
    "vendor's item check",
    "spell_area check",
    "npc_spellclick_spells check", // Unused. For 3.x and later.
    "DBScript engine"
};

// Checks if player meets the condition
bool PlayerCondition::Meets(Player const* player, Map const* map, WorldObject const* source, ConditionSource conditionSourceType) const
{
    DEBUG_LOG("Condition-System: Check condition %u, type %i - called from %s with params plr: %s, map %i, src %s",
              m_entry, m_condition, conditionSourceToStr[conditionSourceType], player ? player->GetGuidStr().c_str() : "<NULL>", map ? map->GetId() : -1, source ? source->GetGuidStr().c_str() : "<NULL>");

    if (!CheckParamRequirements(player, map, source, conditionSourceType))
    {
        return false;
    }

    switch (m_condition)
    {
        case CONDITION_NOT:
            // Checked on load
            return !sConditionStorage.LookupEntry<PlayerCondition>(m_value1)->Meets(player, map, source, conditionSourceType);
        case CONDITION_OR:
            // Checked on load
            return sConditionStorage.LookupEntry<PlayerCondition>(m_value1)->Meets(player, map, source, conditionSourceType) || sConditionStorage.LookupEntry<PlayerCondition>(m_value2)->Meets(player, map, source, conditionSourceType);
        case CONDITION_AND:
            // Checked on load
            return sConditionStorage.LookupEntry<PlayerCondition>(m_value1)->Meets(player, map, source, conditionSourceType) && sConditionStorage.LookupEntry<PlayerCondition>(m_value2)->Meets(player, map, source, conditionSourceType);
        case CONDITION_NONE:
            return true;                                    // empty condition, always met
        case CONDITION_AURA:
            return player->HasAura(m_value1, SpellEffectIndex(m_value2));
        case CONDITION_ITEM:
            return player->HasItemCount(m_value1, m_value2);
        case CONDITION_ITEM_EQUIPPED:
            return player->HasItemOrGemWithIdEquipped(m_value1, 1);
        case CONDITION_AREAID:
        {
            uint32 zone, area;
            WorldObject const* searcher = source ? source : player;
            searcher->GetZoneAndAreaId(zone, area);
            return (zone == m_value1 || area == m_value1) == (m_value2 == 0);
        }
        case CONDITION_REPUTATION_RANK_MIN:
        {
            FactionEntry const* faction = sFactionStore.LookupEntry(m_value1);
            return faction && player->GetReputationMgr().GetRank(faction) >= ReputationRank(m_value2);
        }
        case CONDITION_TEAM:
        {
            if (conditionSourceType == CONDITION_FROM_REFERING_LOOT && sWorld.getConfig(CONFIG_BOOL_ALLOW_TWO_SIDE_INTERACTION_AUCTION))
            {
                return true;
            }
            else
            {
                return uint32(player->GetTeam()) == m_value1;
            }
        }
        case CONDITION_SKILL:
            return player->HasSkill(m_value1) && player->GetBaseSkillValue(m_value1) >= m_value2;
        case CONDITION_QUESTREWARDED:
            return player->GetQuestRewardStatus(m_value1);
        case CONDITION_QUESTTAKEN:
            return player->IsCurrentQuest(m_value1, m_value2);
        case CONDITION_AD_COMMISSION_AURA:
        {
            Unit::SpellAuraHolderMap const& auras = player->GetSpellAuraHolderMap();
            for (Unit::SpellAuraHolderMap::const_iterator itr = auras.begin(); itr != auras.end(); ++itr)
            {
                if ((itr->second->GetSpellProto()->HasAttribute(SPELL_ATTR_CASTABLE_WHILE_MOUNTED) || itr->second->GetSpellProto()->HasAttribute(SPELL_ATTR_ABILITY)) && itr->second->GetSpellProto()->SpellVisual == 3580)
                {
                    return true;
                }
            }
            return false;
        }
        case CONDITION_NO_AURA:
            return !player->HasAura(m_value1, SpellEffectIndex(m_value2));
        case CONDITION_ACTIVE_GAME_EVENT:
            return sGameEventMgr.IsActiveEvent(m_value1);
        case CONDITION_AREA_FLAG:
        {
            WorldObject const* searcher = source ? source : player;
            if (AreaTableEntry const* pAreaEntry = GetAreaEntryByAreaID(searcher->GetAreaId()))
            {
                if ((!m_value1 || (pAreaEntry->flags & m_value1)) && (!m_value2 || !(pAreaEntry->flags & m_value2)))
                {
                    return true;
                }
            }
            return false;
        }
        case CONDITION_RACE_CLASS:
            if ((!m_value1 || (player->getRaceMask() & m_value1)) && (!m_value2 || (player->getClassMask() & m_value2)))
            {
                return true;
            }
            return false;
        case CONDITION_LEVEL:
        {
            switch (m_value2)
            {
                case 0: return player->getLevel() == m_value1;
                case 1: return player->getLevel() >= m_value1;
                case 2: return player->getLevel() <= m_value1;
            }
            return false;
        }
        case CONDITION_NOITEM:
            return !player->HasItemCount(m_value1, m_value2);
        case CONDITION_SPELL:
        {
            switch (m_value2)
            {
                case 0: return player->HasSpell(m_value1);
                case 1: return !player->HasSpell(m_value1);
            }
            return false;
        }
        case CONDITION_INSTANCE_SCRIPT:
        {
            if (!map)
            {
                map = player ? player->GetMap() : source->GetMap();
            }

            if (InstanceData* data = map->GetInstanceData())
            {
                return data->CheckConditionCriteriaMeet(player, m_value1, source, conditionSourceType);
            }
            return false;
        }
        case CONDITION_QUESTAVAILABLE:
        {
            return player->CanTakeQuest(sObjectMgr.GetQuestTemplate(m_value1), false);
        }
        case CONDITION_RESERVED_1:
        case CONDITION_RESERVED_2:
        case CONDITION_RESERVED_3:
        case CONDITION_RESERVED_4:
            return false;
        case CONDITION_QUEST_NONE:
        {
            if (!player->IsCurrentQuest(m_value1) && !player->GetQuestRewardStatus(m_value1))
            {
                return true;
            }
            return false;
        }
        case CONDITION_ITEM_WITH_BANK:
            return player->HasItemCount(m_value1, m_value2, true);
        case CONDITION_NOITEM_WITH_BANK:
            return !player->HasItemCount(m_value1, m_value2, true);
        case CONDITION_NOT_ACTIVE_GAME_EVENT:
            return !sGameEventMgr.IsActiveEvent(m_value1);
        case CONDITION_ACTIVE_HOLIDAY:
            return sGameEventMgr.IsActiveHoliday(HolidayIds(m_value1));
        case CONDITION_NOT_ACTIVE_HOLIDAY:
            return !sGameEventMgr.IsActiveHoliday(HolidayIds(m_value1));
        case CONDITION_LEARNABLE_ABILITY:
        {
            // Already know the spell
            if (player->HasSpell(m_value1))
            {
                return false;
            }

            // If item defined, check if player has the item already.
            if (m_value2)
            {
                // Hard coded item count. This should be ok, since the intention with this condition is to have
                // a all-in-one check regarding items that learn some ability (primary/secondary tradeskills).
                // Commonly, items like this is unique and/or are not expected to be obtained more than once.
                if (player->HasItemCount(m_value2, 1, true))
                {
                    return false;
                }
            }

            bool isSkillOk = false;

            SkillLineAbilityMapBounds bounds = sSpellMgr.GetSkillLineAbilityMapBounds(m_value1);

            for (SkillLineAbilityMap::const_iterator itr = bounds.first; itr != bounds.second; ++itr)
            {
                const SkillLineAbilityEntry* skillInfo = itr->second;

                if (!skillInfo)
                {
                    continue;
                }

                // doesn't have skill
                if (!player->HasSkill(skillInfo->skillId))
                {
                    return false;
                }

                // doesn't match class
                if (skillInfo->classmask && (skillInfo->classmask & player->getClassMask()) == 0)
                {
                    return false;
                }

                // doesn't match race
                if (skillInfo->racemask && (skillInfo->racemask & player->getRaceMask()) == 0)
                {
                    return false;
                }

                // skill level too low
                if (skillInfo->min_value > player->GetSkillValue(skillInfo->skillId))
                {
                    return false;
                }

                isSkillOk = true;
                break;
            }

            if (isSkillOk)
            {
                return true;
            }

            return false;
        }
        case CONDITION_SKILL_BELOW:
        {
            if (m_value2 == 1)
            {
                return !player->HasSkill(m_value1);
            }
            else
            {
                return player->HasSkill(m_value1) && player->GetBaseSkillValue(m_value1) < m_value2;
            }
        }
        case CONDITION_REPUTATION_RANK_MAX:
        {
            FactionEntry const* faction = sFactionStore.LookupEntry(m_value1);
            return faction && player->GetReputationMgr().GetRank(faction) <= ReputationRank(m_value2);
        }
        case CONDITION_SOURCE_AURA:
        {
            if (!source->isType(TYPEMASK_UNIT))
            {
                sLog.outErrorDb("CONDITION_SOURCE_AURA (entry %u) is used for non unit source (source %s) by %s", m_entry, source->GetGuidStr().c_str(), player->GetGuidStr().c_str());
                return false;
            }
            return ((Unit*)source)->HasAura(m_value1, SpellEffectIndex(m_value2));
        }
        case CONDITION_LAST_WAYPOINT:
        {
            if (source->GetTypeId() != TYPEID_UNIT)
            {
                sLog.outErrorDb("CONDITION_LAST_WAYPOINT (entry %u) is used for non creature source (source %s) by %s", m_entry, source->GetGuidStr().c_str(), player->GetGuidStr().c_str());
                return false;
            }
            uint32 lastReachedWp = ((Creature*)source)->GetMotionMaster()->getLastReachedWaypoint();
            switch (m_value2)
            {
                case 0: return m_value1 == lastReachedWp;
                case 1: return m_value1 <= lastReachedWp;
                case 2: return m_value1 > lastReachedWp;
            }
            return false;
        }
        case CONDITION_GENDER:
            return player->getGender() == m_value1;
        case CONDITION_DEAD_OR_AWAY:
            switch (m_value1)
            {
                case 0:                                     // Player dead or out of range
                    return !player || !player->IsAlive() || (m_value2 && source && !source->IsWithinDistInMap(player, m_value2));
                case 1:                                     // All players in Group dead or out of range
                    if (!player)
                    {
                        return true;
                    }
                    if (Group const* grp = player->GetGroup())
                    {
                        for (GroupReference const* itr = grp->GetFirstMember(); itr != NULL; itr = itr->next())
                        {
                            Player const* pl = itr->getSource();
                            if (pl && pl->IsAlive() && !pl->isGameMaster() && (!m_value2 || !source || source->IsWithinDistInMap(pl, m_value2)))
                            {
                                return false;
                            }
                        }
                        return true;
                    }
                    else
                    {
                        return !player->IsAlive() || (m_value2 && source && !source->IsWithinDistInMap(player, m_value2));
                    }
                case 2:                                     // All players in instance dead or out of range
                    for (Map::PlayerList::const_iterator itr = map->GetPlayers().begin(); itr != map->GetPlayers().end(); ++itr)
                    {
                        Player const* plr = itr->getSource();
                        if (plr && plr->IsAlive() && !plr->isGameMaster() && (!m_value2 || !source || source->IsWithinDistInMap(plr, m_value2)))
                        {
                            return false;
                        }
                    }
                    return true;
                case 3:                                     // Creature source is dead
                    return !source || source->GetTypeId() != TYPEID_UNIT || !((Unit*)source)->IsAlive();
            }
        case CONDITION_CREATURE_IN_RANGE:
        {
            Creature* creature = NULL;

            MaNGOS::NearestCreatureEntryWithLiveStateInObjectRangeCheck creature_check(*player, m_value1, true, false, m_value2, true);
            MaNGOS::CreatureLastSearcher<MaNGOS::NearestCreatureEntryWithLiveStateInObjectRangeCheck> searcher(creature, creature_check);
            Cell::VisitGridObjects(player, searcher, m_value2);

            return creature;
        }
        case CONDITION_GAMEOBJECT_IN_RANGE:
        {
            GameObject* pGo = NULL;

            if (source)
            {
                MaNGOS::NearestGameObjectEntryInObjectRangeCheck go_check(*source, m_value1, m_value2);
                MaNGOS::GameObjectLastSearcher<MaNGOS::NearestGameObjectEntryInObjectRangeCheck> searcher(pGo, go_check);

                Cell::VisitGridObjects(source, searcher, m_value2);
            }
            return pGo;
        }
        default:
            return false;
    }
}

// Which params must be provided to a Condition
bool PlayerCondition::CheckParamRequirements(Player const* pPlayer, Map const* map, WorldObject const* source, ConditionSource conditionSourceType) const
{
    switch (m_condition)
    {
        case CONDITION_NOT:
        case CONDITION_AND:
        case CONDITION_OR:
        case CONDITION_NONE:
        case CONDITION_ACTIVE_GAME_EVENT:
        case CONDITION_NOT_ACTIVE_GAME_EVENT:
        case CONDITION_ACTIVE_HOLIDAY:
        case CONDITION_NOT_ACTIVE_HOLIDAY:
            break;
        case CONDITION_AREAID:
        case CONDITION_AREA_FLAG:
            if (!pPlayer && !source)
            {
                sLog.outErrorDb("CONDITION %u type %u used with bad parameters, called from %s, used with plr: %s, map %i, src %s",
                                m_entry, m_condition, conditionSourceToStr[conditionSourceType], pPlayer ? pPlayer->GetGuidStr().c_str() : "NULL", map ? map->GetId() : -1, source ? source->GetGuidStr().c_str() : "NULL");
                return false;
            }
            break;
        case CONDITION_INSTANCE_SCRIPT:
            if (!pPlayer && !source && !map)
            {
                sLog.outErrorDb("CONDITION %u type %u used with bad parameters, called from %s, used with plr: %s, map %i, src %s",
                                m_entry, m_condition, conditionSourceToStr[conditionSourceType], pPlayer ? pPlayer->GetGuidStr().c_str() : "NULL", map ? map->GetId() : -1, source ? source->GetGuidStr().c_str() : "NULL");
                return false;
            }
            break;
        case CONDITION_SOURCE_AURA:
        case CONDITION_LAST_WAYPOINT:
            if (!source)
            {
                sLog.outErrorDb("CONDITION %u type %u used with bad parameters, called from %s, used with plr: %s, map %i, src %s",
                                m_entry, m_condition, conditionSourceToStr[conditionSourceType], pPlayer ? pPlayer->GetGuidStr().c_str() : "NULL", map ? map->GetId() : -1, source ? source->GetGuidStr().c_str() : "NULL");
                return false;
            }
            break;
        case CONDITION_DEAD_OR_AWAY:
            switch (m_value1)
            {
                case 0:                                     // Player dead or out of range
                case 1:                                     // All players in Group dead or out of range
                case 2:                                     // All players in instance dead or out of range
                    if (m_value2 && !source)
                    {
                        sLog.outErrorDb("CONDITION_DEAD_OR_AWAY %u - called from %s without source, but source expected for range check", m_entry, conditionSourceToStr[conditionSourceType]);
                        return false;
                    }
                    if (m_value1 != 2)
                    {
                        return true;
                    }
                    // Case 2 (Instance map only)
                    if (!map && (pPlayer || source))
                    {
                        map = source ? source->GetMap() : pPlayer->GetMap();
                    }
                    if (!map || !map->Instanceable())
                    {
                        sLog.outErrorDb("CONDITION_DEAD_OR_AWAY %u (Player in instance case) - called from %s without map param or from non-instanceable map %i", m_entry,  conditionSourceToStr[conditionSourceType], map ? map->GetId() : -1);
                        return false;
                    }
                case 3:                                     // Creature source is dead
                    return true;
            }
            break;
        default:
            if (!pPlayer)
            {
                sLog.outErrorDb("CONDITION %u type %u used with bad parameters, called from %s, used with plr: %s, map %i, src %s",
                                m_entry, m_condition, conditionSourceToStr[conditionSourceType], pPlayer ? pPlayer->GetGuidStr().c_str() : "NULL", map ? map->GetId() : -1, source ? source->GetGuidStr().c_str() : "NULL");
                return false;
            }
            break;
    }
    return true;
}

// Verification of condition values validity
bool PlayerCondition::IsValid(uint16 entry, ConditionType condition, uint32 value1, uint32 value2)
{
    switch (condition)
    {
        case CONDITION_NOT:
        {
            if (value1 >= entry)
            {
                sLog.outErrorDb("CONDITION_NOT (entry %u, type %d) has invalid value1 %u, must be lower than entry, skipped", entry, condition, value1);
                return false;
            }
            const PlayerCondition* condition1 = sConditionStorage.LookupEntry<PlayerCondition>(value1);
            if (!condition1)
            {
                sLog.outErrorDb("CONDITION_NOT (entry %u, type %d) has value1 %u without proper condition, skipped", entry, condition, value1);
                return false;
            }
            break;
        }
        case CONDITION_OR:
        case CONDITION_AND:
        {
            if (value1 >= entry)
            {
                sLog.outErrorDb("CONDITION _AND or _OR (entry %u, type %d) has invalid value1 %u, must be lower than entry, skipped", entry, condition, value1);
                return false;
            }
            if (value2 >= entry)
            {
                sLog.outErrorDb("CONDITION _AND or _OR (entry %u, type %d) has invalid value2 %u, must be lower than entry, skipped", entry, condition, value2);
                return false;
            }
            const PlayerCondition* condition1 = sConditionStorage.LookupEntry<PlayerCondition>(value1);
            if (!condition1)
            {
                sLog.outErrorDb("CONDITION _AND or _OR (entry %u, type %d) has value1 %u without proper condition, skipped", entry, condition, value1);
                return false;
            }
            const PlayerCondition* condition2 = sConditionStorage.LookupEntry<PlayerCondition>(value2);
            if (!condition2)
            {
                sLog.outErrorDb("CONDITION _AND or _OR (entry %u, type %d) has value2 %u without proper condition, skipped", entry, condition, value2);
                return false;
            }
            break;
        }
        case CONDITION_AURA:
        case CONDITION_SOURCE_AURA:
        {
            if (!sSpellStore.LookupEntry(value1))
            {
                sLog.outErrorDb("Aura condition (entry %u, type %u) requires to have non existing spell (Id: %d), skipped", entry, condition, value1);
                return false;
            }
            if (value2 >= MAX_EFFECT_INDEX)
            {
                sLog.outErrorDb("Aura condition (entry %u, type %u) requires to have non existing effect index (%u) (must be 0..%u), skipped", entry, condition, value2, MAX_EFFECT_INDEX - 1);
                return false;
            }
            break;
        }
        case CONDITION_ITEM:
        case CONDITION_NOITEM:
        case CONDITION_ITEM_WITH_BANK:
        case CONDITION_NOITEM_WITH_BANK:
        {
            ItemPrototype const* proto = ObjectMgr::GetItemPrototype(value1);
            if (!proto)
            {
                sLog.outErrorDb("Item condition (entry %u, type %u) requires to have non existing item (%u), skipped", entry, condition, value1);
                return false;
            }

            if (value2 < 1)
            {
                sLog.outErrorDb("Item condition (entry %u, type %u) useless with count < 1, skipped", entry, condition);
                return false;
            }
            break;
        }
        case CONDITION_ITEM_EQUIPPED:
        {
            ItemPrototype const* proto = ObjectMgr::GetItemPrototype(value1);
            if (!proto)
            {
                sLog.outErrorDb("ItemEquipped condition (entry %u, type %u) requires to have non existing item (%u) equipped, skipped", entry, condition, value1);
                return false;
            }
            break;
        }
        case CONDITION_AREAID:
        {
            AreaTableEntry const* areaEntry = GetAreaEntryByAreaID(value1);
            if (!areaEntry)
            {
                sLog.outErrorDb("Zone condition (entry %u, type %u) requires to be in non existing area (%u), skipped", entry, condition, value1);
                return false;
            }

            if (value2 > 1)
            {
                sLog.outErrorDb("Zone condition (entry %u, type %u) has invalid argument %u (must be 0..1), skipped", entry, condition, value2);
                return false;
            }
            break;
        }
        case CONDITION_REPUTATION_RANK_MIN:
        case CONDITION_REPUTATION_RANK_MAX:
        {
            FactionEntry const* factionEntry = sFactionStore.LookupEntry(value1);
            if (!factionEntry)
            {
                sLog.outErrorDb("Reputation condition (entry %u, type %u) requires to have reputation non existing faction (%u), skipped", entry, condition, value1);
                return false;
            }

            if (value2 >= MAX_REPUTATION_RANK)
            {
                sLog.outErrorDb("Reputation condition (entry %u, type %u) has invalid rank requirement (value2 = %u) - must be between %u and %u, skipped", entry, condition, value2, MIN_REPUTATION_RANK, MAX_REPUTATION_RANK - 1);
                return false;
            }
            break;
        }
        case CONDITION_TEAM:
        {
            if (value1 != ALLIANCE && value1 != HORDE)
            {
                sLog.outErrorDb("Team condition (entry %u, type %u) specifies unknown team (%u), skipped", entry, condition, value1);
                return false;
            }
            break;
        }
        case CONDITION_SKILL:
        case CONDITION_SKILL_BELOW:
        {
            SkillLineEntry const* pSkill = sSkillLineStore.LookupEntry(value1);
            if (!pSkill)
            {
                sLog.outErrorDb("Skill condition (entry %u, type %u) specifies non-existing skill (%u), skipped", entry, condition, value1);
                return false;
            }
            if (value2 < 1 || value2 > sWorld.GetConfigMaxSkillValue())
            {
                sLog.outErrorDb("Skill condition (entry %u, type %u) specifies invalid skill value (%u), skipped", entry, condition, value2);
                return false;
            }
            break;
        }
        case CONDITION_QUESTREWARDED:
        case CONDITION_QUESTTAKEN:
        case CONDITION_QUESTAVAILABLE:
        case CONDITION_QUEST_NONE:
        {
            Quest const* Quest = sObjectMgr.GetQuestTemplate(value1);
            if (!Quest)
            {
                sLog.outErrorDb("Quest condition (entry %u, type %u) specifies non-existing quest (%u), skipped", entry, condition, value1);
                return false;
            }

            if (value2 && condition != CONDITION_QUESTTAKEN)
            {
                sLog.outErrorDb("Quest condition (entry %u, type %u) has useless data in value2 (%u)!", entry, condition, value2);
            }
            break;
        }
        case CONDITION_AD_COMMISSION_AURA:
        {
            if (value1)
            {
                sLog.outErrorDb("Quest condition (entry %u, type %u) has useless data in value1 (%u)!", entry, condition, value1);
            }
            if (value2)
            {
                sLog.outErrorDb("Quest condition (entry %u, type %u) has useless data in value2 (%u)!", entry, condition, value2);
            }
            break;
        }
        case CONDITION_NO_AURA:
        {
            if (!sSpellStore.LookupEntry(value1))
            {
                sLog.outErrorDb("Aura condition (entry %u, type %u) requires to have non existing spell (Id: %d), skipped", entry, condition, value1);
                return false;
            }
            if (value2 > MAX_EFFECT_INDEX)
            {
                sLog.outErrorDb("Aura condition (entry %u, type %u) requires to have non existing effect index (%u) (must be 0..%u), skipped", entry, condition, value2, MAX_EFFECT_INDEX - 1);
                return false;
            }
            break;
        }
        case CONDITION_ACTIVE_GAME_EVENT:
        case CONDITION_NOT_ACTIVE_GAME_EVENT:
        {
            if (!sGameEventMgr.IsValidEvent(value1))
            {
                sLog.outErrorDb("(Not)Active event condition (entry %u, type %u) requires existing event id (%u), skipped", entry, condition, value1);
                return false;
            }
            break;
        }
        case CONDITION_AREA_FLAG:
        {
            if (!value1 && !value2)
            {
                sLog.outErrorDb("Area flag condition (entry %u, type %u) has both values like 0, skipped", entry, condition);
                return false;
            }
            break;
        }
        case CONDITION_RACE_CLASS:
        {
            if (!value1 && !value2)
            {
                sLog.outErrorDb("Race_class condition (entry %u, type %u) has both values like 0, skipped", entry, condition);
                return false;
            }

            if (value1 && !(value1 & RACEMASK_ALL_PLAYABLE))
            {
                sLog.outErrorDb("Race_class condition (entry %u, type %u) has invalid player class %u, skipped", entry, condition, value1);
                return false;
            }

            if (value2 && !(value2 & CLASSMASK_ALL_PLAYABLE))
            {
                sLog.outErrorDb("Race_class condition (entry %u, type %u) has invalid race mask %u, skipped", entry, condition, value2);
                return false;
            }
            break;
        }
        case CONDITION_LEVEL:
        {
            if (!value1 || value1 > sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL))
            {
                sLog.outErrorDb("Level condition (entry %u, type %u)has invalid level %u, skipped", entry, condition, value1);
                return false;
            }

            if (value2 > 2)
            {
                sLog.outErrorDb("Level condition (entry %u, type %u) has invalid argument %u (must be 0..2), skipped", entry, condition, value2);
                return false;
            }

            break;
        }
        case CONDITION_SPELL:
        {
            if (!sSpellStore.LookupEntry(value1))
            {
                sLog.outErrorDb("Spell condition (entry %u, type %u) requires to have non existing spell (Id: %d), skipped", entry, condition, value1);
                return false;
            }

            if (value2 > 1)
            {
                sLog.outErrorDb("Spell condition (entry %u, type %u) has invalid argument %u (must be 0..1), skipped", entry, condition, value2);
                return false;
            }

            break;
        }
        case CONDITION_INSTANCE_SCRIPT:
            break;
        case CONDITION_RESERVED_1:
        case CONDITION_RESERVED_2:
        case CONDITION_RESERVED_3:
        case CONDITION_RESERVED_4:
        {
            sLog.outErrorDb("Condition (%u) reserved for later versions, skipped", condition);
            return false;
        }
        case CONDITION_ACTIVE_HOLIDAY:
        case CONDITION_NOT_ACTIVE_HOLIDAY:
            // no way check holidays in pre-3.x
            break;
        case CONDITION_LEARNABLE_ABILITY:
        {
            SkillLineAbilityMapBounds bounds = sSpellMgr.GetSkillLineAbilityMapBounds(value1);

            if (bounds.first == bounds.second)
            {
                sLog.outErrorDb("Learnable ability condition (entry %u, type %u) has spell id %u defined, but this spell is not listed in SkillLineAbility and can not be used, skipping.", entry, condition, value1);
                return false;
            }

            if (value2)
            {
                ItemPrototype const* proto = ObjectMgr::GetItemPrototype(value2);
                if (!proto)
                {
                    sLog.outErrorDb("Learnable ability condition (entry %u, type %u) has item entry %u defined but item does not exist, skipping.", entry, condition, value2);
                    return false;
                }
            }

            break;
        }
        case CONDITION_LAST_WAYPOINT:
        {
            if (value2 > 2)
            {
                sLog.outErrorDb("Last Waypoint condition (entry %u, type %u) has an invalid value in value2. (Has %u, supported 0, 1, or 2), skipping.", entry, condition, value2);
                return false;
            }
            break;
        }
        case CONDITION_GENDER:
        {
            if (value1 >= MAX_GENDER)
            {
                sLog.outErrorDb("Gender condition (entry %u, type %u) has an invalid value in value1. (Has %u, must be smaller than %u), skipping.", entry, condition, value1, MAX_GENDER);
                return false;
            }
            break;
        }
        case CONDITION_DEAD_OR_AWAY:
        {
            if (value1 >= 4)
            {
                sLog.outErrorDb("Dead condition (entry %u, type %u) has an invalid value in value1. (Has %u, must be smaller than 4), skipping.", entry, condition, value1);
                return false;
            }
            break;
        }
        case CONDITION_CREATURE_IN_RANGE:
        {
            if (!sCreatureStorage.LookupEntry<CreatureInfo> (value1))
            {
                sLog.outErrorDb("Creature in range condition (entry %u, type %u) has an invalid value in value1. (Creature %u does not exist in the database), skipping.", entry, condition, value1);
                return false;
            }
            if (value2 <= 0)
            {
                sLog.outErrorDb("Creature in range condition (entry %u, type %u) has an invalid value in value2. (Range %u must be greater than 0), skipping.", entry, condition, value2);
                return false;
            }
            break;
        }
        case CONDITION_GAMEOBJECT_IN_RANGE:
        {
            if (!sGOStorage.LookupEntry<GameObjectInfo>(value1))
            {
                sLog.outErrorDb("Game object in range condition (entry %u, type %u) has an invalid value in value1 (gameobject). (Game object %u does not exist in the database), skipping.", entry, condition, value1);
                return false;
            }
            if (value2 <= 0)
            {
                sLog.outErrorDb("Game object in range condition (entry %u, type %u) has an invalid value in value2 (range). (Range %u must be greater than 0), skipping.", entry, condition, value2);
                return false;
            }
            break;
        }
        case CONDITION_NONE:
            break;
        default:
            sLog.outErrorDb("Condition entry %u has bad type of %d, skipped ", entry, condition);
            return false;
    }
    return true;
}

// Check if a condition can be used without providing a player param
bool PlayerCondition::CanBeUsedWithoutPlayer(uint16 entry)
{
    PlayerCondition const* condition = sConditionStorage.LookupEntry<PlayerCondition>(entry);
    if (!condition)
    {
        return false;
    }

    switch (condition->m_condition)
    {
        case CONDITION_NOT:
            return CanBeUsedWithoutPlayer(condition->m_value1);
        case CONDITION_AND:
        case CONDITION_OR:
            return CanBeUsedWithoutPlayer(condition->m_value1) && CanBeUsedWithoutPlayer(condition->m_value2);
        case CONDITION_NONE:
        case CONDITION_ACTIVE_GAME_EVENT:
        case CONDITION_NOT_ACTIVE_GAME_EVENT:
        case CONDITION_ACTIVE_HOLIDAY:
        case CONDITION_NOT_ACTIVE_HOLIDAY:
        case CONDITION_AREAID:
        case CONDITION_AREA_FLAG:
        case CONDITION_INSTANCE_SCRIPT:
        case CONDITION_SOURCE_AURA:
        case CONDITION_LAST_WAYPOINT:
            return true;
        default:
            return false;
    }
}

/**
 * @brief Determines the training range type used by a skill line.
 *
 * @param pSkill The skill line entry.
 * @param racial True if the skill is racial.
 * @return SkillRangeType The applicable skill range type.
 */
SkillRangeType GetSkillRangeType(SkillLineEntry const* pSkill, bool racial)
{
    switch (pSkill->categoryId)
    {
        case SKILL_CATEGORY_LANGUAGES: return SKILL_RANGE_LANGUAGE;
        case SKILL_CATEGORY_WEAPON:
            if (pSkill->id != SKILL_FIST_WEAPONS)
            {
                return SKILL_RANGE_LEVEL;
            }
            else
            {
                return SKILL_RANGE_MONO;
            }
        case SKILL_CATEGORY_ARMOR:
        case SKILL_CATEGORY_CLASS:
            if (pSkill->id != SKILL_POISONS && pSkill->id != SKILL_LOCKPICKING)
            {
                return SKILL_RANGE_MONO;
            }
            else
            {
                return SKILL_RANGE_LEVEL;
            }
        case SKILL_CATEGORY_SECONDARY:
        case SKILL_CATEGORY_PROFESSION:
            // not set skills for professions and racial abilities
            if (IsProfessionSkill(pSkill->id))
            {
                return SKILL_RANGE_RANK;
            }
            else if (racial)
            {
                return SKILL_RANGE_NONE;
            }
            else
            {
                return SKILL_RANGE_MONO;
            }
        default:
        case SKILL_CATEGORY_ATTRIBUTES:                     // not found in dbc
        case SKILL_CATEGORY_GENERIC:                        // only GENERIC(DND)
            return SKILL_RANGE_NONE;
    }
}

void ObjectMgr::LoadMailLevelRewards()
{
    m_mailLevelRewardMap.clear();                           // for reload case

    uint32 count = 0;
    QueryResult* result = WorldDatabase.Query("SELECT `level`, `raceMask`, `mailTemplateId`, `senderEntry` FROM `mail_level_reward`");

    if (!result)
    {
        BarGoLink bar(1);

        bar.step();

        sLog.outString();
        sLog.outErrorDb(">> Loaded `mail_level_reward`, table is empty!");
        return;
    }

    BarGoLink bar(result->GetRowCount());

    do
    {
        bar.step();

        Field* fields = result->Fetch();

        uint8 level           = fields[0].GetUInt8();
        uint32 raceMask       = fields[1].GetUInt32();
        uint32 mailTemplateId = fields[2].GetUInt32();
        uint32 senderEntry    = fields[3].GetUInt32();

        if (level > MAX_LEVEL)
        {
            sLog.outErrorDb("Table `mail_level_reward` have data for level %u that more supported by client (%u), ignoring.", level, MAX_LEVEL);
            continue;
        }

        if (!(raceMask & RACEMASK_ALL_PLAYABLE))
        {
            sLog.outErrorDb("Table `mail_level_reward` have raceMask (%u) for level %u that not include any player races, ignoring.", raceMask, level);
            continue;
        }

        if (!sMailTemplateStore.LookupEntry(mailTemplateId))
        {
            sLog.outErrorDb("Table `mail_level_reward` have invalid mailTemplateId (%u) for level %u that invalid not include any player races, ignoring.", mailTemplateId, level);
            continue;
        }

        if (!GetCreatureTemplate(senderEntry))
        {
            sLog.outErrorDb("Table `mail_level_reward` have nonexistent sender creature entry (%u) for level %u that invalid not include any player races, ignoring.", senderEntry, level);
            continue;
        }

        m_mailLevelRewardMap[level].push_back(MailLevelReward(raceMask, mailTemplateId, senderEntry));

        ++count;
    }
    while (result->NextRow());
    delete result;

    sLog.outString();
    sLog.outString(">> Loaded %u level dependent mail rewards,", count);
}


/* This function is supposed to take care of three things:
 *  1) Load Transports on Map or on Continents
 *  2) Load Active Npcs on Map or Continents
 *  3) Load Everything dependend on config setting LoadAllGridsOnMaps
 *
 *  This function is currently WIP, hence parts exist only as draft.
 */
ObjectMgr::LivingWorldStartupStats ObjectMgr::LoadActiveEntities(Map* _map)
{
    // Special case on startup - load continents
    if (!_map)
    {
        s_livingWorldStats = ObjectMgr::LivingWorldStartupStats();
        s_livingWorldStartupPass = true;

        uint32 continents[] = {0, 1, 369, 530};
        for (int i = 0; i < countof(continents); ++i)
        {
            _map = sMapMgr.FindMap(continents[i]);
            if (!_map)
            {
                _map = sMapMgr.CreateMap(continents[i], NULL);
            }

            if (_map)
            {
                LoadActiveEntities(_map);
            }
            else
            {
                sLog.outError("ObjectMgr::LoadActiveEntities - Unable to create Map %u", continents[i]);
            }
        }

        s_livingWorldStartupPass = false;
        return s_livingWorldStats;
    }

    bool collectLivingWorldStats = s_livingWorldStartupPass;
    bool forceLoad = sWorld.isForceLoadMap(_map->GetId());

    uint32 mapTransportCount = 0;
    uint32 forceLoadRequests = 0;
    uint32 uniqueGridCount = 0;
    uint32 newlyLoaded = 0;
    uint32 alreadyLoaded = 0;
    uint32 activeCreatureGuids = 0;

    std::set<std::pair<uint32, uint32> > uniqueGrids;

    if (collectLivingWorldStats)
    {
        MapManager::TransportMap::const_iterator transportMapItr = sMapMgr.m_TransportsByMap.find(_map->GetId());
        if (transportMapItr != sMapMgr.m_TransportsByMap.end())
        {
            mapTransportCount = uint32(transportMapItr->second.size());
        }

        std::pair<ActiveCreatureGuidsOnMap::const_iterator, ActiveCreatureGuidsOnMap::const_iterator> activeBounds = m_activeCreatures.equal_range(_map->GetId());
        for (ActiveCreatureGuidsOnMap::const_iterator itr = activeBounds.first; itr != activeBounds.second; ++itr)
        {
            ++activeCreatureGuids;
        }
    }

    // Load active objects for _map
    if (forceLoad)
    {
        for (CreatureDataMap::const_iterator itr = mCreatureDataMap.begin(); itr != mCreatureDataMap.end(); ++itr)
        {
            if (itr->second.mapid == _map->GetId())
            {
                if (collectLivingWorldStats)
                {
                    ++forceLoadRequests;
                    GridPair gridPair = MaNGOS::ComputeGridPair(itr->second.posX, itr->second.posY);
                    uniqueGrids.insert(std::make_pair(gridPair.x_coord, gridPair.y_coord));

                    if (_map->IsLoaded(itr->second.posX, itr->second.posY))
                    {
                        ++alreadyLoaded;
                    }
                    else
                    {
                        ++newlyLoaded;
                    }
                }

                _map->ForceLoadGrid(itr->second.posX, itr->second.posY);
            }
        }
    }
    else                                                    // Normal case - Load all npcs that are active
    {
        std::pair<ActiveCreatureGuidsOnMap::const_iterator, ActiveCreatureGuidsOnMap::const_iterator> bounds = m_activeCreatures.equal_range(_map->GetId());
        for (ActiveCreatureGuidsOnMap::const_iterator itr = bounds.first; itr != bounds.second; ++itr)
        {
            CreatureData const& data = mCreatureDataMap[itr->second];
            {
                if (collectLivingWorldStats)
                {
                    ++forceLoadRequests;
                    GridPair gridPair = MaNGOS::ComputeGridPair(data.posX, data.posY);
                    uniqueGrids.insert(std::make_pair(gridPair.x_coord, gridPair.y_coord));

                    if (_map->IsLoaded(data.posX, data.posY))
                    {
                        ++alreadyLoaded;
                    }
                    else
                    {
                        ++newlyLoaded;
                    }
                }

                _map->ForceLoadGrid(data.posX, data.posY);
            }
        }
    }

    if (collectLivingWorldStats)
    {
        uniqueGridCount = uint32(uniqueGrids.size());

        s_livingWorldStats.totalUniqueGrids += uniqueGridCount;
        s_livingWorldStats.totalNewlyLoaded += newlyLoaded;
        s_livingWorldStats.totalMapTransports += mapTransportCount;
        if (forceLoad)
        {
            ++s_livingWorldStats.forcedMaps;
        }

        if (forceLoad)
        {
            sLog.outString("[LivingWorld] map %u: force-load=ON, creature-rows=%u, ForceLoadGrid-requests=%u, unique-grids=%u, newly-loaded=%u (explicit-locks-set), already-loaded=%u, extra-active-creatures=%u, map-transports=%u",
                           _map->GetId(), forceLoadRequests, forceLoadRequests, uniqueGridCount, newlyLoaded, alreadyLoaded, activeCreatureGuids, mapTransportCount);
        }
        else
        {
            sLog.outString("[LivingWorld] map %u: force-load=OFF, extra-active-creatures=%u, ForceLoadGrid-requests=%u, unique-grids=%u, newly-loaded=%u (explicit-locks-set), already-loaded=%u, map-transports=%u",
                           _map->GetId(), activeCreatureGuids, forceLoadRequests, uniqueGridCount, newlyLoaded, alreadyLoaded, mapTransportCount);
        }
    }

    return ObjectMgr::LivingWorldStartupStats();
}

void ObjectMgr::AddVendorItem(uint32 entry, uint32 item, uint32 maxcount, uint32 incrtime, uint32 extendedcost)
{
    VendorItemData& vList = m_mCacheVendorItemMap[entry];
    vList.AddItem(item, maxcount, incrtime, extendedcost, 0);

    WorldDatabase.PExecuteLog("INSERT INTO `npc_vendor` (`entry`,`item`,`maxcount`,`incrtime`,`extendedcost`) VALUES('%u','%u','%u','%u','%u')", entry, item, maxcount, incrtime, extendedcost);
}

/**
 * @brief Removes a vendor item from a creature vendor list and database.
 *
 * @param entry The vendor creature entry.
 * @param item The item entry.
 * @return true if the item was removed; otherwise, false.
 */
bool ObjectMgr::RemoveVendorItem(uint32 entry, uint32 item)
{
    CacheVendorItemMap::iterator  iter = m_mCacheVendorItemMap.find(entry);
    if (iter == m_mCacheVendorItemMap.end())
    {
        return false;
    }

    if (!iter->second.RemoveItem(item))
    {
        return false;
    }

    WorldDatabase.PExecuteLog("DELETE FROM `npc_vendor` WHERE `entry`='%u' AND `item`='%u'", entry, item);
    return true;
}

/**
 * @brief Validates a vendor item definition for a vendor or vendor template.
 *
 * @param isTemplate true when validating a vendor template.
 * @param tableName The source table name.
 * @param vendor_entry The vendor or template entry id.
 * @param item_id The item entry id.
 * @param maxcount The limited stock count.
 * @param incrtime The stock replenishment interval.
 * @param conditionId The optional condition id.
 * @param pl Optional player used for command feedback.
 * @param skip_vendors Optional set used to suppress repeated vendor errors.
 * @return true if the vendor item definition is valid; otherwise, false.
 */
bool ObjectMgr::IsVendorItemValid(bool isTemplate, char const* tableName, uint32 vendor_entry, uint32 item_id, uint32 maxcount, uint32 incrtime, uint32 ExtendedCost, uint16 conditionId, Player* pl, std::set<uint32>* skip_vendors) const
{
    char const* idStr = isTemplate ? "vendor template" : "vendor";
    CreatureInfo const* cInfo = NULL;

    if (!isTemplate)
    {
        cInfo = GetCreatureTemplate(vendor_entry);
        if (!cInfo)
        {
            if (pl)
            {
                ChatHandler(pl).SendSysMessage(LANG_COMMAND_VENDORSELECTION);
            }
            else
            {
                sLog.outErrorDb("Table `%s` has data for nonexistent creature (Entry: %u), ignoring", tableName, vendor_entry);
            }
            return false;
        }

        if (!(cInfo->NpcFlags & UNIT_NPC_FLAG_VENDOR))
        {
            if (!skip_vendors || skip_vendors->count(vendor_entry) == 0)
            {
                if (pl)
                {
                    ChatHandler(pl).SendSysMessage(LANG_COMMAND_VENDORSELECTION);
                }
                else
                {
                    sLog.outErrorDb("Table `%s` has data for creature (Entry: %u) without vendor flag, ignoring", tableName, vendor_entry);
                }

                if (skip_vendors)
                {
                    skip_vendors->insert(vendor_entry);
                }
            }
            return false;
        }
    }

    if (!GetItemPrototype(item_id))
    {
        if (pl)
        {
            ChatHandler(pl).PSendSysMessage(LANG_ITEM_NOT_FOUND, item_id);
        }
        else
            sLog.outErrorDb("Table `%s` for %s %u contain nonexistent item (%u), ignoring",
                            tableName, idStr, vendor_entry, item_id);
        return false;
    }

    if (ExtendedCost && !sItemExtendedCostStore.LookupEntry(ExtendedCost))
    {
        if (pl)
        {
            ChatHandler(pl).PSendSysMessage(LANG_EXTENDED_COST_NOT_EXIST, ExtendedCost);
        }
        else
            sLog.outErrorDb("Table `%s` contain item (Entry: %u) with wrong ExtendedCost (%u) for %s %u, ignoring",
                            tableName, item_id, ExtendedCost, idStr, vendor_entry);
        return false;
    }

    if (maxcount > 0 && incrtime == 0)
    {
        if (pl)
        {
            ChatHandler(pl).PSendSysMessage("MaxCount!=0 (%u) but IncrTime==0", maxcount);
        }
        else
            sLog.outErrorDb("Table `%s` has `maxcount` (%u) for item %u of %s %u but `incrtime`=0, ignoring",
                            tableName, maxcount, item_id, idStr, vendor_entry);
        return false;
    }
    else if (maxcount == 0 && incrtime > 0)
    {
        if (pl)
        {
            ChatHandler(pl).PSendSysMessage("MaxCount==0 but IncrTime<>=0");
        }
        else
            sLog.outErrorDb("Table `%s` has `maxcount`=0 for item %u of %s %u but `incrtime`<>0, ignoring",
                            tableName, item_id, idStr, vendor_entry);
        return false;
    }

    if (conditionId && !sConditionStorage.LookupEntry<PlayerCondition>(conditionId))
    {
        sLog.outErrorDb("Table `%s` has `condition_id`=%u for item %u of %s %u but this condition is not valid, ignoring", tableName, conditionId, item_id, idStr, vendor_entry);
        return false;
    }

    VendorItemData const* vItems = isTemplate ? GetNpcVendorTemplateItemList(vendor_entry) : GetNpcVendorItemList(vendor_entry);
    VendorItemData const* tItems = isTemplate ? NULL : GetNpcVendorTemplateItemList(vendor_entry);

    if (!vItems && !tItems)
    {
        return true;                                        // later checks for non-empty lists
    }

    if (vItems && vItems->FindItem(item_id))
    {
        if (pl)
        {
            ChatHandler(pl).PSendSysMessage(LANG_ITEM_ALREADY_IN_LIST, item_id);
        }
        else
        {
            sLog.outErrorDb("Table `%s` has duplicate items %u for %s %u, ignoring",
                            tableName, item_id, idStr, vendor_entry);
        }
        return false;
    }

    if (!isTemplate)
    {
        if (tItems && tItems->GetItem(item_id))
        {
            if (pl)
            {
                ChatHandler(pl).PSendSysMessage(LANG_ITEM_ALREADY_IN_LIST, item_id);
            }
            else
            {
                if (!cInfo->VendorTemplateId)
                    sLog.outErrorDb("Table `%s` has duplicate items %u for %s %u, ignoring",
                                    tableName, item_id, idStr, vendor_entry);
                else
                    sLog.outErrorDb("Table `%s` has duplicate items %u for %s %u (or possible in vendor template %u), ignoring",
                                    tableName, item_id, idStr, vendor_entry, cInfo->VendorTemplateId);
            }
            return false;
        }
    }

    uint32 countItems = vItems ? vItems->GetItemCount() : 0;
    countItems += tItems ? tItems->GetItemCount() : 0;

    if (countItems >= MAX_VENDOR_ITEMS)
    {
        if (pl)
        {
            ChatHandler(pl).SendSysMessage(LANG_COMMAND_ADDVENDORITEMITEMS);
        }
        else
            sLog.outErrorDb("Table `%s` has too many items (%u >= %i) for %s %u, ignoring",
                            tableName, countItems, MAX_VENDOR_ITEMS, idStr, vendor_entry);
        return false;
    }

    return true;
}

/**
 * @brief Registers a loaded group in the object manager.
 *
 * @param group The group to add.
 */
void ObjectMgr::AddGroup(Group* group)
{
    mGroupMap[group->GetId()] = group ;
}

/**
 * @brief Unregisters a loaded group from the object manager.
 *
 * @param group The group to remove.
 */
void ObjectMgr::RemoveGroup(Group* group)
{
    mGroupMap.erase(group->GetId());
}

void ObjectMgr::AddArenaTeam(ArenaTeam* arenaTeam)
{
    mArenaTeamMap[arenaTeam->GetId()] = arenaTeam;
}

void ObjectMgr::RemoveArenaTeam(uint32 Id)
{
    mArenaTeamMap.erase(Id);
}

/**
 * @brief Gets localized creature name and subname strings for a locale index.
 *
 * @param entry The creature entry id.
 * @param loc_idx The internal locale index.
 * @param namePtr Receives the localized name if available.
 * @param subnamePtr Receives the localized subname if available.
 */
void ObjectMgr::GetCreatureLocaleStrings(uint32 entry, int32 loc_idx, char const** namePtr, char const** subnamePtr) const
{
    if (loc_idx >= 0)
    {
        if (CreatureLocale const *il = GetCreatureLocale(entry))
        {
            if (namePtr && il->Name.size() > size_t(loc_idx) && !il->Name[loc_idx].empty())
            {
                *namePtr = il->Name[loc_idx].c_str();
            }

            if (subnamePtr && il->SubName.size() > size_t(loc_idx) && !il->SubName[loc_idx].empty())
            {
                *subnamePtr = il->SubName[loc_idx].c_str();
            }
        }
    }
}

/**
 * @brief Gets localized item name and description strings for a locale index.
 *
 * @param entry The item entry id.
 * @param loc_idx The internal locale index.
 * @param namePtr Receives the localized item name if available.
 * @param descriptionPtr Receives the localized description if available.
 */
void ObjectMgr::GetItemLocaleStrings(uint32 entry, int32 loc_idx, std::string* namePtr, std::string* descriptionPtr) const
{
    if (loc_idx >= 0)
    {
        if (ItemLocale const *il = GetItemLocale(entry))
        {
            if (namePtr && il->Name.size() > size_t(loc_idx) && !il->Name[loc_idx].empty())
            {
                *namePtr = il->Name[loc_idx];
            }

            if (descriptionPtr && il->Description.size() > size_t(loc_idx) && !il->Description[loc_idx].empty())
            {
                *descriptionPtr = il->Description[loc_idx];
            }
        }
    }
}

/**
 * @brief Gets the localized quest title for a locale index.
 *
 * @param entry The quest entry id.
 * @param loc_idx The internal locale index.
 * @param titlePtr Receives the localized title if available.
 */
void ObjectMgr::GetQuestLocaleStrings(uint32 entry, int32 loc_idx, std::string* titlePtr) const
{
    if (loc_idx >= 0)
    {
        if (QuestLocale const *il = GetQuestLocale(entry))
        {
            if (titlePtr && il->Title.size() > size_t(loc_idx) && !il->Title[loc_idx].empty())
            {
                *titlePtr = il->Title[loc_idx];
            }
        }
    }
}

/**
 * @brief Gets all localized npc text option strings for a locale index.
 *
 * @param entry The npc text entry id.
 * @param loc_idx The internal locale index.
 * @param text0_Ptr Receives the first text column array if available.
 * @param text1_Ptr Receives the second text column array if available.
 */
void ObjectMgr::GetNpcTextLocaleStringsAll(uint32 entry, int32 loc_idx, ObjectMgr::NpcTextArray* text0_Ptr, ObjectMgr::NpcTextArray* text1_Ptr) const
{
    if (loc_idx >= 0)
    {
        if (NpcTextLocale const *nl = GetNpcTextLocale(entry))
        {
            if (text0_Ptr)
                for (int i = 0; i < MAX_GOSSIP_TEXT_OPTIONS; ++i)
                {
                    if (nl->Text_0[i].size() > (size_t)loc_idx && !nl->Text_0[i][loc_idx].empty())
                    {
                        (*text0_Ptr)[i] = nl->Text_0[i][loc_idx];
                    }
                }

            if (text1_Ptr)
                for (int i = 0; i < MAX_GOSSIP_TEXT_OPTIONS; ++i)
                {
                    if (nl->Text_1[i].size() > (size_t)loc_idx && !nl->Text_1[i][loc_idx].empty())
                    {
                        (*text1_Ptr)[i] = nl->Text_1[i][loc_idx];
                    }
                }
        }
    }
}

/**
 * @brief Gets the first localized npc text option pair for a locale index.
 *
 * @param entry The npc text entry id.
 * @param loc_idx The internal locale index.
 * @param text0_0_Ptr Receives the first localized text string.
 * @param text1_0_Ptr Receives the second localized text string.
 */
void ObjectMgr::GetNpcTextLocaleStrings0(uint32 entry, int32 loc_idx, std::string* text0_0_Ptr, std::string* text1_0_Ptr) const
{
    if (loc_idx >= 0)
    {
        if (NpcTextLocale const *nl = GetNpcTextLocale(entry))
        {
            if (text0_0_Ptr)
                if (nl->Text_0[0].size() > (size_t)loc_idx && !nl->Text_0[0][loc_idx].empty())
                {
                    *text0_0_Ptr = nl->Text_0[0][loc_idx];
                }

            if (text1_0_Ptr)
                if (nl->Text_1[0].size() > (size_t)loc_idx && !nl->Text_1[0][loc_idx].empty())
                {
                    *text1_0_Ptr = nl->Text_1[0][loc_idx];
                }
        }
    }
}

// Functions for scripting access
bool LoadMangosStrings(DatabaseType& db, char const* table, int32 start_value, int32 end_value, bool extra_content)
{
    // MAX_DB_SCRIPT_STRING_ID is max allowed negative value for scripts (scrpts can use only more deep negative values
    // start/end reversed for negative values
    if (start_value > MAX_DB_SCRIPT_STRING_ID || end_value >= start_value)
    {
        sLog.outErrorDb("Table '%s' attempt loaded with reserved by mangos range (%d - %d), strings not loaded.", table, start_value, end_value + 1);
        return false;
    }

    return sObjectMgr.LoadMangosStrings(db, table, start_value, end_value, extra_content);
}

/**
 * @brief Loads creature template spell assignments and validates their spell ids.
 */
void ObjectMgr::LoadCreatureTemplateSpells()
{
    sCreatureTemplateSpellsStorage.Load();

    for (SQLStorageBase::SQLSIterator<CreatureTemplateSpells> itr = sCreatureTemplateSpellsStorage.getDataBegin<CreatureTemplateSpells>(); itr < sCreatureTemplateSpellsStorage.getDataEnd<CreatureTemplateSpells>(); ++itr)
    {
        if (!sCreatureStorage.LookupEntry<CreatureInfo>(itr->entry))
        {
            sLog.outErrorDb("LoadCreatureTemplateSpells: Spells found for creature entry %u, but creature does not exist, skipping", itr->entry);
            sCreatureTemplateSpellsStorage.EraseEntry(itr->entry);
        }
        for (uint8 i = 0; i < CREATURE_MAX_SPELLS; ++i)
        {
            if (itr->spells[i] && !sSpellStore.LookupEntry(itr->spells[i]))
            {
                sLog.outErrorDb("LoadCreatureTemplateSpells: Spells found for creature entry %u, assigned spell %u does not exist, set to 0", itr->entry, itr->spells[i]);
                const_cast<CreatureTemplateSpells*>(*itr)->spells[i] = 0;
            }
        }
    }

    sLog.outString(">> Loaded %u creature_template_spells definitions", sCreatureTemplateSpellsStorage.GetRecordCount());
    sLog.outString();
}

/**
 * @brief Retrieves a creature template from the global creature store.
 *
 * @param entry The creature template entry.
 * @return CreatureInfo const* The matching creature template, or null if missing.
 */
CreatureInfo const* GetCreatureTemplateStore(uint32 entry)
{
    return sCreatureStorage.LookupEntry<CreatureInfo>(entry);
}

/**
 * @brief Retrieves a quest template from the object manager.
 *
 * @param entry The quest template entry.
 * @return Quest const* The matching quest template, or null if missing.
 */
Quest const* GetQuestTemplateStore(uint32 entry)
{
    return sObjectMgr.GetQuestTemplate(entry);
}

/**
 * @brief Retrieves localized MaNGOS string data by entry id.
 *
 * @param entry The localized string entry.
 * @return MangosStringLocale const* The matching localized string data, or null if missing.
 */
MangosStringLocale const* GetMangosStringData(int32 entry)
{
    return sObjectMgr.GetMangosStringLocale(entry);
}

/**
 * @brief Evaluates whether a creature spawn matches the current search criteria.
 *
 * @param dataPair The creature data pair being tested.
 * @return true if the search can stop early; otherwise, false.
 */
bool FindCreatureData::operator()(CreatureDataPair const& dataPair)
{
    // skip wrong entry ids
    if (i_id && dataPair.second.id != i_id)
    {
        return false;
    }

    if (!i_anyData)
    {
        i_anyData = &dataPair;
    }

    // without player we can't find more stricted cases, so use fouded
    if (!i_player)
    {
        return true;
    }

    // skip diff. map cases
    if (dataPair.second.mapid != i_player->GetMapId())
    {
        return false;
    }

    float new_dist = i_player->GetDistance2d(dataPair.second.posX, dataPair.second.posY);

    if (!i_mapData || new_dist < i_mapDist)
    {
        i_mapData = &dataPair;
        i_mapDist = new_dist;
    }

    // skip not spawned (in any state),
    uint16 pool_id = sPoolMgr.IsPartOfAPool<Creature>(dataPair.first);
    if (pool_id && !i_player->GetMap()->GetPersistentState()->IsSpawnedPoolObject<Creature>(dataPair.first))
    {
        return false;
    }

    if (!i_spawnedData || new_dist < i_spawnedDist)
    {
        i_spawnedData = &dataPair;
        i_spawnedDist = new_dist;
    }

    return false;
}

/**
 * @brief Gets the best matching creature spawn data found by the search.
 *
 * @return The selected creature data pair, or null if none matched.
 */
CreatureDataPair const* FindCreatureData::GetResult() const
{
    if (i_spawnedData)
    {
        return i_spawnedData;
    }

    if (i_mapData)
    {
        return i_mapData;
    }

    return i_anyData;
}

/**
 * @brief Evaluates whether a gameobject spawn matches the current search criteria.
 *
 * @param dataPair The gameobject data pair being tested.
 * @return true if the search can stop early; otherwise, false.
 */
bool FindGOData::operator()(GameObjectDataPair const& dataPair)
{
    // skip wrong entry ids
    if (i_id && dataPair.second.id != i_id)
    {
        return false;
    }

    if (!i_anyData)
    {
        i_anyData = &dataPair;
    }

    // without player we can't find more stricted cases, so use fouded
    if (!i_player)
    {
        return true;
    }

    // skip diff. map cases
    if (dataPair.second.mapid != i_player->GetMapId())
    {
        return false;
    }

    float new_dist = i_player->GetDistance2d(dataPair.second.posX, dataPair.second.posY);

    if (!i_mapData || new_dist < i_mapDist)
    {
        i_mapData = &dataPair;
        i_mapDist = new_dist;
    }

    // skip not spawned (in any state)
    uint16 pool_id = sPoolMgr.IsPartOfAPool<GameObject>(dataPair.first);
    if (pool_id && !i_player->GetMap()->GetPersistentState()->IsSpawnedPoolObject<GameObject>(dataPair.first))
    {
        return false;
    }

    if (!i_spawnedData || new_dist < i_spawnedDist)
    {
        i_spawnedData = &dataPair;
        i_spawnedDist = new_dist;
    }

    return false;
}

/**
 * @brief Gets the best matching gameobject spawn data found by the search.
 *
 * @return The selected gameobject data pair, or null if none matched.
 */
GameObjectDataPair const* FindGOData::GetResult() const
{
    if (i_mapData)
    {
        return i_mapData;
    }

    if (i_spawnedData)
    {
        return i_spawnedData;
    }

    return i_anyData;
}

/**
 * @brief Displays localized scripted text, sound, and emote output from a source object.
 *
 * @param source The speaking world object.
 * @param entry The text entry id.
 * @param target The optional target unit for whispers.
 * @return true if the text was displayed successfully; otherwise false.
 */
bool DoDisplayText(WorldObject* source, int32 entry, Unit const* target /*=NULL*/)
{
    MangosStringLocale const* data = sObjectMgr.GetMangosStringLocale(entry);

    if (!data)
    {
        _DoStringError(entry, "DoScriptText with source %s could not find text entry %i.", source->GetGuidStr().c_str(), entry);
        return false;
    }

    if (data->SoundId)
    {
        if (data->Type == CHAT_TYPE_ZONE_YELL)
        {
            source->GetMap()->PlayDirectSoundToMap(data->SoundId, source->GetZoneId());
        }
        else if (data->Type == CHAT_TYPE_WHISPER || data->Type == CHAT_TYPE_BOSS_WHISPER)
        {
            // An error will be displayed for the text
            if (target && target->GetTypeId() == TYPEID_PLAYER)
            {
                source->PlayDirectSound(data->SoundId, (Player const*)target);
            }
        }
        else
        {
            source->PlayDirectSound(data->SoundId);
        }
    }

    if (data->Emote)
    {
        if (source->GetTypeId() == TYPEID_UNIT || source->GetTypeId() == TYPEID_PLAYER)
        {
            ((Unit*)source)->HandleEmote(data->Emote);
        }
        else
        {
            _DoStringError(entry, "DoDisplayText entry %i tried to process emote for invalid source %s", entry, source->GetGuidStr().c_str());
            return false;
        }
    }

    if ((data->Type == CHAT_TYPE_WHISPER || data->Type == CHAT_TYPE_BOSS_WHISPER) && (!target || target->GetTypeId() != TYPEID_PLAYER))
    {
        _DoStringError(entry, "DoDisplayText entry %i can not whisper without target unit (TYPEID_PLAYER).", entry);
        return false;
    }

    source->MonsterText(data, target);
    return true;
}
