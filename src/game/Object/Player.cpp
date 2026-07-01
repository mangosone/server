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

enum CharacterFlags
{
    CHARACTER_FLAG_NONE                 = 0x00000000,
    CHARACTER_FLAG_UNK1                 = 0x00000001,
    CHARACTER_FLAG_UNK2                 = 0x00000002,
    CHARACTER_LOCKED_FOR_TRANSFER       = 0x00000004,
    CHARACTER_FLAG_UNK4                 = 0x00000008,
    CHARACTER_FLAG_UNK5                 = 0x00000010,
    CHARACTER_FLAG_UNK6                 = 0x00000020,
    CHARACTER_FLAG_UNK7                 = 0x00000040,
    CHARACTER_FLAG_UNK8                 = 0x00000080,
    CHARACTER_FLAG_UNK9                 = 0x00000100,
    CHARACTER_FLAG_UNK10                = 0x00000200,
    CHARACTER_FLAG_HIDE_HELM            = 0x00000400,
    CHARACTER_FLAG_HIDE_CLOAK           = 0x00000800,
    CHARACTER_FLAG_UNK13                = 0x00001000,
    CHARACTER_FLAG_GHOST                = 0x00002000,
    CHARACTER_FLAG_RENAME               = 0x00004000,
    CHARACTER_FLAG_UNK16                = 0x00008000,
    CHARACTER_FLAG_UNK17                = 0x00010000,
    CHARACTER_FLAG_UNK18                = 0x00020000,
    CHARACTER_FLAG_UNK19                = 0x00040000,
    CHARACTER_FLAG_UNK20                = 0x00080000,
    CHARACTER_FLAG_UNK21                = 0x00100000,
    CHARACTER_FLAG_UNK22                = 0x00200000,
    CHARACTER_FLAG_UNK23                = 0x00400000,
    CHARACTER_FLAG_UNK24                = 0x00800000,
    CHARACTER_FLAG_LOCKED_BY_BILLING    = 0x01000000,
    CHARACTER_FLAG_DECLINED             = 0x02000000,
    CHARACTER_FLAG_UNK27                = 0x04000000,
    CHARACTER_FLAG_UNK28                = 0x08000000,
    CHARACTER_FLAG_UNK29                = 0x10000000,
    CHARACTER_FLAG_UNK30                = 0x20000000,
    CHARACTER_FLAG_UNK31                = 0x40000000,
    CHARACTER_FLAG_UNK32                = 0x80000000
};

// corpse reclaim times
#define DEATH_EXPIRE_STEP (5*MINUTE)
#define MAX_DEATH_COUNT 3

static const uint32 corpseReclaimDelay[MAX_DEATH_COUNT] = {30, 60, 120};

//== PlayerTaxi ================================================

PlayerTaxi::PlayerTaxi()
{
    // Taxi nodes
    memset(m_taximask, 0, sizeof(m_taximask));
}

/**
 * @brief Initializes known taxi nodes for a newly created player.
 *
 * @param race The player race id.
 * @param level Unused player level.
 */
void PlayerTaxi::InitTaxiNodesForLevel(uint32 race, uint32 level)
{
    // race specific initial known nodes: capital and taxi hub masks
    switch (race)
    {
        case RACE_HUMAN:    SetTaximaskNode(2);  break;     // Human
        case RACE_ORC:      SetTaximaskNode(23); break;     // Orc
        case RACE_DWARF:    SetTaximaskNode(6);  break;     // Dwarf
        case RACE_NIGHTELF: SetTaximaskNode(26);
            SetTaximaskNode(27); break;     // Night Elf
        case RACE_UNDEAD:   SetTaximaskNode(11); break;     // Undead
        case RACE_TAUREN:   SetTaximaskNode(22); break;     // Tauren
        case RACE_GNOME:    SetTaximaskNode(6);  break;     // Gnome
        case RACE_TROLL:    SetTaximaskNode(23); break;     // Troll
        case RACE_BLOODELF: SetTaximaskNode(82); break;     // Blood Elf
        case RACE_DRAENEI:  SetTaximaskNode(94); break;     // Draenei
    }

    // new continent starting masks (It will be accessible only at new map)
    switch (Player::TeamForRace(race))
    {
        case ALLIANCE: SetTaximaskNode(100); break;
        case HORDE:    SetTaximaskNode(99);  break;
        default: break;
    }
    // level dependent taxi hubs
    if (level >= 68)
    {
        SetTaximaskNode(213);                               // Shattered Sun Staging Area
    }
}

/**
 * @brief Loads the known taxi-node bitmask from a serialized string.
 *
 * @param data The serialized taxi mask data.
 */
void PlayerTaxi::LoadTaxiMask(const char* data)
{
    Tokens tokens = StrSplit(data, " ");

    int index;
    Tokens::iterator iter;
    for (iter = tokens.begin(), index = 0; (index < TaxiMaskSize) && (iter != tokens.end()); ++iter, ++index)
    {
        // load and set bits only for existing taxi nodes
        m_taximask[index] = sTaxiNodesMask[index] & uint32(atol((*iter).c_str()));
    }
}

/**
 * @brief Appends the player's taxi-node mask to a packet buffer.
 *
 * @param data The destination packet buffer.
 * @param all true to append all existing nodes instead of only known nodes.
 */
void PlayerTaxi::AppendTaximaskTo(ByteBuffer& data, bool all)
{
    if (all)
    {
        for (uint8 i = 0; i < TaxiMaskSize; ++i)
        {
            data << uint32(sTaxiNodesMask[i]);               // all existing nodes
        }
    }
    else
    {
        for (uint8 i = 0; i < TaxiMaskSize; ++i)
        {
            data << uint32(m_taximask[i]);                   // known nodes
        }
    }
}

/**
 * @brief Loads active taxi destinations from a serialized path string.
 *
 * @param values The serialized taxi destination list.
 * @param team The player's faction team.
 * @return true if the taxi route is valid; otherwise, false.
 */
bool PlayerTaxi::LoadTaxiDestinationsFromString(const std::string& values, Team team)
{
    ClearTaxiDestinations();

    Tokens tokens = StrSplit(values, " ");

    for (Tokens::iterator iter = tokens.begin(); iter != tokens.end(); ++iter)
    {
        uint32 node = uint32(atol(iter->c_str()));
        AddTaxiDestination(node);
    }

    if (m_TaxiDestinations.empty())
    {
        return true;
    }

    // Check integrity
    if (m_TaxiDestinations.size() < 2)
    {
        return false;
    }

    for (size_t i = 1; i < m_TaxiDestinations.size(); ++i)
    {
        uint32 cost;
        uint32 path;
        sObjectMgr.GetTaxiPath(m_TaxiDestinations[i - 1], m_TaxiDestinations[i], path, cost);
        if (!path)
        {
            return false;
        }
    }

    // can't load taxi path without mount set (quest taxi path?)
    if (!sObjectMgr.GetTaxiMountDisplayId(GetTaxiSource(), team, true))
    {
        return false;
    }

    return true;
}

/**
 * @brief Serializes the current taxi destination list.
 *
 * @return The serialized taxi destination string.
 */
std::string PlayerTaxi::SaveTaxiDestinationsToString()
{
    if (m_TaxiDestinations.empty())
    {
        return "";
    }

    std::ostringstream ss;

    for (size_t i = 0; i < m_TaxiDestinations.size(); ++i)
    {
        ss << m_TaxiDestinations[i] << " ";
    }

    return ss.str();
}

/**
 * @brief Gets the taxi path id for the current first route segment.
 *
 * @return The current taxi path id, or 0 if no valid route exists.
 */
uint32 PlayerTaxi::GetCurrentTaxiPath() const
{
    if (m_TaxiDestinations.size() < 2)
    {
        return 0;
    }

    uint32 path;
    uint32 cost;

    sObjectMgr.GetTaxiPath(m_TaxiDestinations[0], m_TaxiDestinations[1], path, cost);

    return path;
}

/**
 * @brief Serializes the player's discovered taxi mask into a stream.
 *
 * @param ss The destination output stream.
 * @param taxi The taxi data to serialize.
 * @return The output stream.
 */
std::ostringstream& operator<< (std::ostringstream& ss, PlayerTaxi const& taxi)
{
    for (int i = 0; i < TaxiMaskSize; ++i)
    {
        ss << taxi.m_taximask[i] << " ";
    }
    return ss;
}

/**
 * @brief Builds a spell modifier from a spell effect definition.
 *
 * @param _op The spell modifier operation.
 * @param _type The spell modifier type.
 * @param _value The modifier value.
 * @param spellEntry The source spell entry.
 * @param eff The spell effect index.
 * @param _charges The initial charge count.
 */
SpellModifier::SpellModifier(SpellModOp _op, SpellModType _type, int32 _value, SpellEntry const* spellEntry, SpellEffectIndex eff, int16 _charges /*= 0*/) : op(_op), type(_type), charges(_charges), value(_value), spellId(spellEntry->Id), lastAffected(NULL)
{
    mask = sSpellMgr.GetSpellAffectMask(spellEntry->Id, eff);
}

/**
 * @brief Builds a spell modifier from an aura source.
 *
 * @param _op The spell modifier operation.
 * @param _type The spell modifier type.
 * @param _value The modifier value.
 * @param aura The aura providing the modifier.
 * @param _charges The initial charge count.
 */
SpellModifier::SpellModifier(SpellModOp _op, SpellModType _type, int32 _value, Aura const* aura, int16 _charges /*= 0*/) : op(_op), type(_type), charges(_charges), value(_value), spellId(aura->GetId()), lastAffected(NULL)
{
    mask = sSpellMgr.GetSpellAffectMask(aura->GetId(), aura->GetEffIndex());
}

/**
 * @brief Checks whether this modifier affects a given spell.
 *
 * @param spell The spell to test.
 * @return true if the modifier applies to the spell; otherwise, false.
 */
bool SpellModifier::isAffectedOnSpell(SpellEntry const* spell) const
{
    SpellEntry const* affect_spell = sSpellStore.LookupEntry(spellId);
    // False if affect_spell == NULL or spellFamily not equal
    if (!affect_spell || affect_spell->SpellFamilyName != spell->SpellFamilyName)
    {
        return false;
    }
    return spell->IsFitToFamilyMask(mask);
}

//== TradeData =================================================

TradeData* TradeData::GetTraderData() const
{
    return m_trader->GetTradeData();
}

/**
 * @brief Gets the item placed in a trade slot.
 *
 * @param slot The trade slot.
 * @return The item in the slot, or null if empty.
 */
Item* TradeData::GetItem(TradeSlots slot) const
{
    return m_items[slot] ? m_player->GetItemByGuid(m_items[slot]) : NULL;
}

/**
 * @brief Checks whether a specific item is part of the current trade.
 *
 * @param item_guid The item GUID to test.
 * @return true if the item is present in a trade slot; otherwise, false.
 */
bool TradeData::HasItem(ObjectGuid item_guid) const
{
    for (int i = 0; i < TRADE_SLOT_COUNT; ++i)
    {
        if (m_items[i] == item_guid)
        {
            return true;
        }
    }
    return false;
}

/**
 * @brief Gets the item used as the current trade spell reagent.
 *
 * @return The spell-cast item, or null if none is set.
 */
Item* TradeData::GetSpellCastItem() const
{
    return m_spellCastItem ?  m_player->GetItemByGuid(m_spellCastItem) : NULL;
}

/**
 * @brief Sets the item assigned to a trade slot and refreshes trade state.
 *
 * @param slot The trade slot to update.
 * @param item The item to place in the slot, or null to clear it.
 */
void TradeData::SetItem(TradeSlots slot, Item* item)
{
    ObjectGuid itemGuid = item ? item->GetObjectGuid() : ObjectGuid();

    if (m_items[slot] == itemGuid)
    {
        return;
    }

    m_items[slot] = itemGuid;

    SetAccepted(false);
    GetTraderData()->SetAccepted(false);

    Update();

    // need remove possible trader spell applied to changed item
    if (slot == TRADE_SLOT_NONTRADED)
    {
        GetTraderData()->SetSpell(0);
    }

    // need remove possible player spell applied (possible move reagent)
    SetSpell(0);
}

/**
 * @brief Sets the spell cast through the trade window.
 *
 * @param spell_id The spell identifier.
 * @param castItem The optional reagent item used for the spell.
 */
void TradeData::SetSpell(uint32 spell_id, Item* castItem /*= NULL*/)
{
    ObjectGuid itemGuid = castItem ? castItem->GetObjectGuid() : ObjectGuid();

    if (m_spell == spell_id && m_spellCastItem == itemGuid)
    {
        return;
    }

    m_spell = spell_id;
    m_spellCastItem = itemGuid;

    SetAccepted(false);
    GetTraderData()->SetAccepted(false);

    Update(true);                                           // send spell info to item owner
    Update(false);                                          // send spell info to caster self
}

/**
 * @brief Sets the trade money offer and refreshes trade state.
 *
 * @param money The offered money amount.
 */
void TradeData::SetMoney(uint32 money)
{
    if (m_money == money)
    {
        return;
    }

    m_money = money;

    SetAccepted(false);
    GetTraderData()->SetAccepted(false);

    Update();
}

/**
 * @brief Sends the current trade state to one side of the trade.
 *
 * @param for_trader true to update the trader view; false to update the player view.
 */
void TradeData::Update(bool for_trader /*= true*/)
{
    if (for_trader)
    {
        m_trader->GetSession()->SendUpdateTrade(true); // player state for trader
    }
    else
    {
        m_player->GetSession()->SendUpdateTrade(false); // player state for player
    }
}

/**
 * @brief Sets the accepted state for the trade and optionally notifies the other trader.
 *
 * @param state The new accepted state.
 * @param crosssend true to send the status to the trader instead of the owner.
 */
void TradeData::SetAccepted(bool state, bool crosssend /*= false*/)
{
    m_accepted = state;

    if (!state)
    {
        TradeStatusInfo info;
        info.Status = TRADE_STATUS_BACK_TO_TRADE;
        if (crosssend)
        {
            m_trader->GetSession()->SendTradeStatus(info);
        }
        else
        {
            m_player->GetSession()->SendTradeStatus(info);
        }
    }
}

//== Player ====================================================

UpdateMask Player::updateVisualBits;

/**
 * @brief Initializes a player instance and its runtime state.
 *
 * @param session The owning world session.
 */
Player::Player(WorldSession* session): Unit(), m_mover(this), m_camera(this), m_reputationMgr(this)
{
#ifdef ENABLE_PLAYERBOTS
    m_playerbotAI = 0;
    m_playerbotMgr = 0;
#endif

    m_transport = 0;

    m_speakTime = 0;
    m_speakCount = 0;

    m_visibilityObserverSweepTimer = World::GetVisibilityObserverSweepInterval();

    m_objectType |= TYPEMASK_PLAYER;
    m_objectTypeId = TYPEID_PLAYER;

    m_valuesCount = PLAYER_END;

    SetActiveObjectState(true);                             // player is always active object

    m_session = session;

    m_ExtraFlags = 0;
    if (GetSession()->GetSecurity() >= SEC_GAMEMASTER)
    {
        SetAcceptTicket(true);
    }

    // players always accept
    if (GetSession()->GetSecurity() == SEC_PLAYER)
    {
        SetAcceptWhispers(true);
    }

    m_comboPoints = 0;

    m_usedTalentCount = 0;

    m_regenTimer = 0;
    m_weaponChangeTimer = 0;

    m_zoneUpdateId = 0;
    m_zoneUpdateTimer = 0;
    m_positionStatusUpdateTimer = 0;

    m_areaUpdateId = 0;

    m_nextSave = sWorld.getConfig(CONFIG_UINT32_INTERVAL_SAVE);

    // randomize first save time in range [CONFIG_UINT32_INTERVAL_SAVE] around [CONFIG_UINT32_INTERVAL_SAVE]
    // this must help in case next save after mass player load after server startup
    m_nextSave = urand(m_nextSave / 2, m_nextSave * 3 / 2);

    clearResurrectRequestData();

    m_SpellModRemoveCount = 0;

    memset(m_items, 0, sizeof(Item*)*PLAYER_SLOTS_COUNT);

    m_social = NULL;

    // group is initialized in the reference constructor
    SetGroupInvite(NULL);
    m_groupUpdateMask = 0;
    m_auraUpdateMask = 0;

    duel = NULL;

    m_GuildIdInvited = 0;
    m_ArenaTeamIdInvited = 0;

    m_atLoginFlags = AT_LOGIN_NONE;

    mSemaphoreTeleport_Near = false;
    mSemaphoreTeleport_Far = false;

    m_DelayedOperations = 0;
    m_bCanDelayTeleport = false;
    m_bHasDelayedTeleport = false;
    m_bHasBeenAliveAtDelayedTeleport = true;                // overwrite always at setup teleport data, so not used infact
    m_teleport_options = 0;

    m_trade = NULL;

    m_cinematic = 0;

    PlayerTalkClass = new PlayerMenu(GetSession());
    m_currentBuybackSlot = BUYBACK_SLOT_START;

    m_DailyQuestChanged = false;

    m_lastLiquid = NULL;

    for (int i = 0; i < MAX_TIMERS; ++i)
    {
        m_MirrorTimer[i] = DISABLED_MIRROR_TIMER;
    }

    m_MirrorTimerFlags = UNDERWATER_NONE;
    m_MirrorTimerFlagsLast = UNDERWATER_NONE;

    m_isInWater = false;
    m_drunkTimer = 0;
    m_drunk = 0;
    m_restTime = 0;
    m_deathTimer = 0;
    // Initialize death expire time to 0
    m_deathExpireTime = 0;

    // Initialize swing error message to 0
    m_swingErrorMsg = 0;

    // Initialize detection invisibility timer to 1 millisecond
    m_DetectInvTimer = 1 * IN_MILLISECONDS;

    // Initialize battleground queue IDs and invited instances
    for (int j = 0; j < PLAYER_MAX_BATTLEGROUND_QUEUES; ++j)
    {
        m_bgBattleGroundQueueID[j].bgQueueTypeId  = BATTLEGROUND_QUEUE_NONE;
        m_bgBattleGroundQueueID[j].invitedToInstance = 0;
    }

    // Set login time to current time
    m_logintime = time(NULL);
    // Set last tick time to login time
    m_Last_tick = m_logintime;
    // Initialize weapon proficiency to 0
    m_WeaponProficiency = 0;
    // Initialize armor proficiency to 0
    m_ArmorProficiency = 0;
    // Initialize parry ability to false
    m_canParry = false;
    // Initialize block ability to false
    m_canBlock = false;
    // Initialize dual wield ability to false
    m_canDualWield = false;
    // Initialize ammo DPS to 0.0f
    m_ammoDPS = 0.0f;

    // Initialize temporary unsummoned pet number to 0
    m_temporaryUnsummonedPetNumber = 0;

    //////////////////// Rest System/////////////////////
    // Initialize time of entering inn to 0
    time_inn_enter = 0;
    // Initialize inn trigger ID to 0
    inn_trigger_id = 0;
    // Initialize rest bonus to 0
    m_rest_bonus = 0;
    // Initialize rest type to no rest
    rest_type = REST_TYPE_NO;
    //////////////////// Rest System/////////////////////

    // Initialize mails updated flag to false
    m_mailsUpdated = false;
    // Initialize unread mails count to 0
    unReadMails = 0;
    // Initialize next mail delivery time to 0
    m_nextMailDelivereTime = 0;

    // Initialize reset talents cost to 0
    m_resetTalentsCost = 0;
    // Initialize reset talents time to 0
    m_resetTalentsTime = 0;
    // Initialize item update queue blocked flag to false
    m_itemUpdateQueueBlocked = false;

    // Initialize forced speed changes for all move types to 0
    for (int i = 0; i < MAX_MOVE_TYPE; ++i)
    {
        m_forced_speed_changes[i] = 0;
    }

    // Initialize stable slots to 0
    m_stableSlots = 0;

    /////////////////// Instance System /////////////////////
    // Initialize homebind timer to 0
    m_HomebindTimer = 0;
    // Initialize instance validity to true
    m_InstanceValid = true;
    // Initialize dungeon difficulty to normal
    m_dungeonDifficulty = DUNGEON_DIFFICULTY_NORMAL;

    // Initialize aura base modifiers
    for (int i = 0; i < BASEMOD_END; ++i)
    {
        m_auraBaseMod[i][FLAT_MOD] = 0.0f;
        m_auraBaseMod[i][PCT_MOD] = 1.0f;
    }

    // Initialize base rating values to 0
    for (int i = 0; i < MAX_COMBAT_RATING; ++i)
    {
        m_baseRatingValue[i] = 0;
    }

    // Honor System
    // Set last honor update time to current time
    m_lastHonorUpdateTime = time(NULL);

    // Player summoning
    // Initialize summon expire time to 0
    m_summon_expire = 0;
    // Initialize summon map ID to 0
    m_summon_mapid = 0;
    // Initialize summon coordinates to (0.0f, 0.0f, 0.0f)
    m_summon_x = 0.0f;
    m_summon_y = 0.0f;
    m_summon_z = 0.0f;

    // Initialize contested PvP timer to 0
    m_contestedPvPTimer = 0;

    // Initialize declined name to NULL
    m_declinedname = NULL;

    // Initialize last fall time to 0
    m_lastFallTime = 0;
    // Initialize last fall Z coordinate to 0
    m_lastFallZ = 0;
#ifdef ENABLE_PLAYERBOTS
    // Initialize player bot AI to NULL
    m_playerbotAI = NULL;
    // Initialize player bot manager to NULL
    m_playerbotMgr = NULL;
#endif

}

/**
 * @brief Destroys the player and releases owned resources.
 */
Player::~Player()
{
    // Perform cleanup before deleting the player object
    CleanupsBeforeDelete();

    // Ensure the social object is unloaded (should already be done in PlayerLogout)
    // m_social = NULL;

    // Delete all items in the player's inventory
    for (int i = 0; i < PLAYER_SLOTS_COUNT; ++i)
    {
        delete m_items[i];
    }

    // Clean up communication channels
    CleanupChannels();

    // Delete all mailed items and deallocate mail objects
    for (PlayerMails::const_iterator itr = m_mail.begin(); itr != m_mail.end(); ++itr)
    {
        delete *itr;
    }

    // Delete all items in the player's item map
    for (ItemMap::const_iterator iter = mMitems.begin(); iter != mMitems.end(); ++iter)
    {
        delete iter->second; // Ensure no duplicated items to avoid crashes
    }

    // Delete the player's talk class
    delete PlayerTalkClass;

    // Remove the player from any transport they are on
    if (m_transport)
    {
        m_transport->RemovePassenger(this);
    }

    // Delete all item set effects
    for (size_t x = 0; x < ItemSetEff.size(); ++x)
    {
        delete ItemSetEff[x];
    }

#ifdef ENABLE_PLAYERBOTS
    // Delete player bot AI and manager if they exist
    if (m_playerbotAI)
    {
        delete m_playerbotAI;
        m_playerbotAI = 0;
    }
    if (m_playerbotMgr)
    {
        delete m_playerbotMgr;
        m_playerbotMgr = 0;
    }
#endif

    // Clean up player-instance binds and unload instance saves
    for (uint8 i = 0; i < MAX_DIFFICULTY; ++i)
    {
        for (BoundInstancesMap::iterator itr = m_boundInstances[i].begin(); itr != m_boundInstances[i].end(); ++itr)
        {
            itr->second.state->RemovePlayer(this);
        }
    }

    // Delete the declined name if it exists
    delete m_declinedname;
}

/**
 * @brief Performs pre-destruction cleanup for trade, duel, zone, and unit state.
 */
void Player::CleanupsBeforeDelete()
{
    // Stop cinematic flyover if active (must happen before camera dtor)
    if (m_cinematicFlyover && m_cinematicFlyover->IsActive())
    {
        m_cinematicFlyover->Stop();
    }
    m_cinematicFlyover.reset();

    // Perform cleanup only if the object is fully created
    if (m_uint32Values)
    {
        // Cancel any ongoing trade
        TradeCancel(false);
        // Complete any ongoing duel
        DuelComplete(DUEL_FLED);
    }

    // Notify zone scripts that the player is leaving the zone
    sOutdoorPvPMgr.HandlePlayerLeaveZone(this, m_zoneUpdateId);

    // Perform unit-specific cleanup
    Unit::CleanupsBeforeDelete();
}

/**
 * @brief Creates a new player character with starting data and equipment.
 *
 * @param guidlow The low GUID for the player.
 * @param name The player name.
 * @param race The race id.
 * @param class_ The class id.
 * @param gender The gender id.
 * @param skin The skin customization id.
 * @param face The face customization id.
 * @param hairStyle The hairstyle customization id.
 * @param hairColor The hair color customization id.
 * @param facialHair The facial hair customization id.
 * @param outfitId Unused outfit identifier.
 * @return true if the player was initialized successfully; otherwise, false.
 */
bool Player::Create(uint32 guidlow, const std::string& name, uint8 race, uint8 class_, uint8 gender, uint8 skin, uint8 face, uint8 hairStyle, uint8 hairColor, uint8 facialHair, uint8 /*outfitId */)
{
    // FIXME: outfitId not used in player creation

    // Create the player object with the given GUID
    Object::_Create(guidlow, 0, HIGHGUID_PLAYER);

    // Set the player's name
    m_name = name;

    // Get player info based on race and class
    PlayerInfo const* info = sObjectMgr.GetPlayerInfo(race, class_);
    if (!info)
    {
        sLog.outError("Player has incorrect race/class pair. Can't be loaded.");
        return false;
    }

    // Get class entry from DBC
    ChrClassesEntry const* cEntry = sChrClassesStore.LookupEntry(class_);
    if (!cEntry)
    {
        sLog.outError("Class %u not found in DBC (Wrong DBC files?)", class_);
        return false;
    }

    // Validate gender
    if (gender != uint8(GENDER_MALE) && gender != uint8(GENDER_FEMALE))
    {
        sLog.outError("Invalid gender %u at player creation", uint32(gender));
        return false;
    }

    // Initialize player items to NULL
    for (int i = 0; i < PLAYER_SLOTS_COUNT; ++i)
    {
        m_items[i] = NULL;
    }

    // Set player's initial location
    SetLocationMapId(info->mapId);
    Relocate(info->positionX, info->positionY, info->positionZ, info->orientation);

    // Set the player's map
    SetMap(sMapMgr.CreateMap(info->mapId, this));

    // Set player's power type based on class
    uint8 powertype = cEntry->powerType;

    // Set player's faction based on race
    setFactionForRace(race);

    // Set player's race, class, gender, and power type
    SetByteValue(UNIT_FIELD_BYTES_0, 0, race);
    SetByteValue(UNIT_FIELD_BYTES_0, 1, class_);
    SetByteValue(UNIT_FIELD_BYTES_0, 2, gender);
    SetByteValue(UNIT_FIELD_BYTES_0, 3, powertype);

    // Initialize player's display IDs (model, scale, and model data)
    InitDisplayIds();

    // Set player's flags and cast speed
    SetByteValue(UNIT_FIELD_BYTES_2, 1, UNIT_BYTE2_FLAG_UNK3 | UNIT_BYTE2_FLAG_UNK5);
    SetUInt32Value(UNIT_FIELD_FLAGS, UNIT_FLAG_PVP_ATTACKABLE);
    SetFloatValue(UNIT_MOD_CAST_SPEED, 1.0f); // Fix cast time shown in spell tooltip on client

    // Set default watched faction index
    SetInt32Value(PLAYER_FIELD_WATCHED_FACTION_INDEX, -1); // -1 is default value

    // Set player's appearance (skin, face, hair style, hair color, facial hair)
    SetByteValue(PLAYER_BYTES, 0, skin);
    SetByteValue(PLAYER_BYTES, 1, face);
    SetByteValue(PLAYER_BYTES, 2, hairStyle);
    SetByteValue(PLAYER_BYTES, 3, hairColor);
    SetByteValue(PLAYER_BYTES_2, 0, facialHair);
    SetByteValue(PLAYER_BYTES_2, 3, REST_STATE_NORMAL);

    // Set player's gender and battlefield arena faction
    SetUInt16Value(PLAYER_BYTES_3, 0, gender); // Only GENDER_MALE/GENDER_FEMALE (1 bit) allowed, drunk state = 0
    SetByteValue(PLAYER_BYTES_3, 3, 0); // BattlefieldArenaFaction (0 or 1)

    // Initialize player's guild information
    SetUInt32Value(PLAYER_GUILDID, 0);
    SetUInt32Value(PLAYER_GUILDRANK, 0);
    SetUInt32Value(PLAYER_GUILD_TIMESTAMP, 0);

    // Initialize player's known titles and chosen title
    SetUInt64Value(PLAYER__FIELD_KNOWN_TITLES, 0); // 0=disabled
    SetUInt32Value(PLAYER_CHOSEN_TITLE, 0);

    // Initialize player's kill counts and contributions
    SetUInt32Value(PLAYER_FIELD_KILLS, 0);
    SetUInt32Value(PLAYER_FIELD_LIFETIME_HONORABLE_KILLS, 0);
    SetUInt32Value(PLAYER_FIELD_TODAY_CONTRIBUTION, 0);
    SetUInt32Value(PLAYER_FIELD_YESTERDAY_CONTRIBUTION, 0);

    // Set player's starting level based on security level
    if (GetSession()->GetSecurity() >= SEC_MODERATOR)
    {
        SetUInt32Value(UNIT_FIELD_LEVEL, sWorld.getConfig(CONFIG_UINT32_START_GM_LEVEL));
    }
    else
    {
        SetUInt32Value(UNIT_FIELD_LEVEL, sWorld.getConfig(CONFIG_UINT32_START_PLAYER_LEVEL));
    }

    // Set player's starting money, honor points, and arena points
    SetUInt32Value(PLAYER_FIELD_COINAGE, sWorld.getConfig(CONFIG_UINT32_START_PLAYER_MONEY));
    SetHonorPoints(sWorld.getConfig(CONFIG_UINT32_START_HONOR_POINTS));
    SetArenaPoints(sWorld.getConfig(CONFIG_UINT32_START_ARENA_POINTS));

    // Initialize played time
    m_Last_tick = time(NULL);
    m_Played_time[PLAYED_TIME_TOTAL] = 0;
    m_Played_time[PLAYED_TIME_LEVEL] = 0;

    // Initialize base stats and related field values
    InitStatsForLevel();
    InitTaxiNodesForLevel();
    InitTalentForLevel();
    InitPrimaryProfessions(); // To max set before any spell added

    // Apply original stats mods before spell loading or item equipment
    UpdateMaxHealth(); // Update max Health (for add bonus from stamina)
    SetHealth(GetMaxHealth());

    if (GetPowerType() == POWER_MANA)
    {
        UpdateMaxPower(POWER_MANA); // Update max Mana (for add bonus from intellect)
        SetPower(POWER_MANA, GetMaxPower(POWER_MANA));
    }

    // Learn default spells
    learnDefaultSpells();

    // Initialize action bar with default actions
    for (PlayerCreateInfoActions::const_iterator action_itr = info->action.begin(); action_itr != info->action.end(); ++action_itr)
    {
        addActionButton(action_itr->button, action_itr->action, action_itr->type);
    }

    // Initialize player's starting items
    uint32 raceClassGender = GetUInt32Value(UNIT_FIELD_BYTES_0) & 0x00FFFFFF;

    CharStartOutfitEntry const* oEntry = NULL;
    for (uint32 i = 1; i < sCharStartOutfitStore.GetNumRows(); ++i)
    {
        if (CharStartOutfitEntry const* entry = sCharStartOutfitStore.LookupEntry(i))
        {
            if (entry->RaceClassGender == raceClassGender)
            {
                oEntry = entry;
                break;
            }
        }
    }

    if (oEntry)
    {
        for (int j = 0; j < MAX_OUTFIT_ITEMS; ++j)
        {
            if (oEntry->ItemId[j] <= 0)
            {
                continue;
            }

            uint32 item_id = oEntry->ItemId[j];

            // Just skip, reported in ObjectMgr::LoadItemPrototypes
            ItemPrototype const* iProto = ObjectMgr::GetItemPrototype(item_id);
            if (!iProto)
            {
                continue;
            }

            // BuyCount by default
            int32 count = iProto->BuyCount;

            // Special amount for food/drink
            if (iProto->Class == ITEM_CLASS_CONSUMABLE && iProto->SubClass == ITEM_SUBCLASS_FOOD)
            {
                switch (iProto->Spells[0].SpellCategory)
                {
                    case 11: // Food
                        if (iProto->Stackable > 4)
                        {
                            count = 4;
                        }
                        break;
                    case 59: // Drink
                        if (iProto->Stackable > 2)
                        {
                            count = 2;
                        }
                        break;
                }
            }

            StoreNewItemInBestSlots(item_id, count);
        }
    }

    for (PlayerCreateInfoItems::const_iterator item_id_itr = info->item.begin(); item_id_itr != info->item.end(); ++item_id_itr)
    {
        StoreNewItemInBestSlots(item_id_itr->item_id, item_id_itr->item_amount);
    }

    // Equip bags and main-hand weapon
    // Second pass for not equipped items (offhand weapon/shield if it attempted to equip before main-hand weapon)
    // or ammo not equipped in special bag
    for (int i = INVENTORY_SLOT_ITEM_START; i < INVENTORY_SLOT_ITEM_END; ++i)
    {
        if (Item* pItem = GetItemByPos(INVENTORY_SLOT_BAG_0, i))
        {
            uint16 eDest;
            // Equip offhand weapon/shield if it attempted to equip before main-hand weapon
            InventoryResult msg = CanEquipItem(NULL_SLOT, eDest, pItem, false);
            if (msg == EQUIP_ERR_OK)
            {
                RemoveItem(INVENTORY_SLOT_BAG_0, i, true);
                EquipItem(eDest, pItem, true);
            }
            // Move other items to more appropriate slots (ammo not equipped in special bag)
            else
            {
                ItemPosCountVec sDest;
                msg = CanStoreItem(NULL_BAG, NULL_SLOT, sDest, pItem, false);
                if (msg == EQUIP_ERR_OK)
                {
                    RemoveItem(INVENTORY_SLOT_BAG_0, i, true);
                    pItem = StoreItem(sDest, pItem, true);
                }

                // If this is ammo then use it
                msg = CanUseAmmo(pItem->GetEntry());
                if (msg == EQUIP_ERR_OK)
                {
                    SetAmmo(pItem->GetEntry());
                }
            }
        }
    }
    // All item positions resolved

    return true;
}

/**
 * @brief Equips or stores a newly created item in the best available slots.
 *
 * @param titem_id The item entry id.
 * @param titem_amount The amount to create.
 * @return true if all remaining items were equipped or stored; otherwise, false.
 */
bool Player::StoreNewItemInBestSlots(uint32 titem_id, uint32 titem_amount)
{
    DEBUG_LOG("STORAGE: Creating initial item, itemId = %u, count = %u", titem_id, titem_amount);

    // Attempt to equip the item one by one
    while (titem_amount > 0)
    {
        uint16 eDest;
        uint8 msg = CanEquipNewItem(NULL_SLOT, eDest, titem_id, false);
        if (msg != EQUIP_ERR_OK)
        {
            break;
        }

        // Equip the new item
        EquipNewItem(eDest, titem_id, true);
        AutoUnequipOffhandIfNeed();
        --titem_amount;
    }

    if (titem_amount == 0)
    {
        return true; // All items equipped
    }

    // Attempt to store the remaining items
    ItemPosCountVec sDest;
    // Store in the main bag to simplify the second pass (special bags may not be equipped yet)
    uint8 msg = CanStoreNewItem(INVENTORY_SLOT_BAG_0, NULL_SLOT, sDest, titem_id, titem_amount);
    if (msg == EQUIP_ERR_OK)
    {
        StoreNewItem(sDest, titem_id, true, Item::GenerateItemRandomPropertyId(titem_id));
        return true; // Items stored
    }

    // Item cannot be added
    sLog.outError("STORAGE: Can't equip or store initial item %u for race %u class %u , error msg = %u", titem_id, getRace(), getClass(), msg);
    return false;
}

// Helper function, mainly for script side, but can be used for simple tasks in MaNGOS as well
Item* Player::StoreNewItemInInventorySlot(uint32 itemEntry, uint32 amount)
{
    ItemPosCountVec vDest;

    uint8 msg = CanStoreNewItem(INVENTORY_SLOT_BAG_0, NULL_SLOT, vDest, itemEntry, amount);

    if (msg == EQUIP_ERR_OK)
    {
        if (Item* pItem = StoreNewItem(vDest, itemEntry, true, Item::GenerateItemRandomPropertyId(itemEntry)))
        {
            return pItem;
        }
    }

    return NULL;
}

/**
 * @brief Updates player state, timers, combat, saving, and delayed actions.
 *
 * @param update_diff The elapsed update time in milliseconds.
 * @param p_time The elapsed update time used by some player timers.
 */
void Player::Update(uint32 update_diff, uint32 p_time)
{
    // If the player is not in the world, return early
    if (!IsInWorld())
    {
        return;
    }

    // Handle undelivered mail
    if (m_nextMailDelivereTime && m_nextMailDelivereTime <= time(NULL))
    {
        SendNewMail();
        ++unReadMails;

        // It will be recalculate at mailbox open (for unReadMails important non-0 until mailbox open, it also will be recalculated)
        m_nextMailDelivereTime = 0;
    }

    // Used to implement delayed far teleports
    SetCanDelayTeleport(true);
    Unit::Update(update_diff, p_time);
    SetCanDelayTeleport(false);

    // Periodic observer-side visibility maintenance.
    // The owner's visible set is otherwise refreshed only when the player moves
    // (Camera::UpdateVisibilityForOwner via OnRelocated), while an object moving
    // out of range only re-notifies observers still near that object. A
    // near-stationary player therefore never gets an out-of-range update for an
    // active object that walks away, leaving it frozen on the client. Sweep the
    // owner's visible set on an interval so out-of-range objects are dropped even
    // when the player stands still. Skipped mid-teleport (visibility is rebuilt
    // by the teleport path) and gated by config.
    if (World::GetVisibilityObserverSweepEnabled() && !IsBeingTeleported())
    {
        if (m_visibilityObserverSweepTimer <= update_diff)
        {
            m_visibilityObserverSweepTimer = World::GetVisibilityObserverSweepInterval();
            GetCamera().UpdateVisibilityForOwner();
        }
        else
        {
            m_visibilityObserverSweepTimer -= update_diff;
        }
    }

    // Update cinematic flyover if active
    if (m_cinematicFlyover && m_cinematicFlyover->IsActive())
    {
        m_cinematicFlyover->Update(update_diff);
    }

    // Update player-only attacks
    if (uint32 ranged_att = getAttackTimer(RANGED_ATTACK))
    {
        setAttackTimer(RANGED_ATTACK, (update_diff >= ranged_att ? 0 : ranged_att - update_diff));
    }

    time_t now = time(NULL);

    // Update PvP flag
    UpdatePvPFlag(now);

    // Update contested PvP state
    UpdateContestedPvP(update_diff);

    // Update duel flag
    UpdateDuelFlag(now);

    // Check duel distance
    CheckDuelDistance(now);

    // Update AFK report
    UpdateAfkReport(now);

    // Update items with limited lifetime
    if (now > m_Last_tick)
    {
        UpdateItemDuration(uint32(now - m_Last_tick));
    }

    // Update timed quests
    if (!m_timedquests.empty())
    {
        QuestSet::iterator iter = m_timedquests.begin();
        while (iter != m_timedquests.end())
        {
            QuestStatusData& q_status = mQuestStatus[*iter];
            if (q_status.m_timer <= update_diff)
            {
                uint32 quest_id  = *iter;
                ++iter; // Current iter will be removed in FailQuest
                FailQuest(quest_id);
            }
            else
            {
                q_status.m_timer -= update_diff;
                if (q_status.uState != QUEST_NEW)
                {
                    q_status.uState = QUEST_CHANGED;
                }
                ++iter;
            }
        }
    }

    // Update melee attacking state
    if (hasUnitState(UNIT_STAT_MELEE_ATTACKING))
    {
        UpdateMeleeAttackingState();

        Unit* pVictim = getVictim();
        if (pVictim && !IsNonMeleeSpellCasted(false))
        {
            Player* vOwner = pVictim->GetCharmerOrOwnerPlayerOrPlayerItself();
            if (vOwner && vOwner->IsPvP() && !IsInDuelWith(vOwner))
            {
                UpdatePvP(true);
                RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_ENTER_PVP_COMBAT);
            }
        }
    }

    // Speed collect rest bonus (section/in hour)
    if (HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_RESTING))
    {
        if (GetTimeInnEnter() > 0) // Freeze update
        {
            time_t time_inn = now - GetTimeInnEnter();
            if (time_inn >= 10) // Freeze update
            {
                SetRestBonus(GetRestBonus() + ComputeRest(time_inn));
                UpdateInnerTime(now);
            }
        }
    }

    // Update regeneration timer
    if (m_regenTimer)
    {
        if (update_diff >= m_regenTimer)
        {
            m_regenTimer = 0;
        }
        else
        {
            m_regenTimer -= update_diff;
        }
    }

    // Update position status timer
    if (m_positionStatusUpdateTimer)
    {
        if (update_diff >= m_positionStatusUpdateTimer)
        {
            m_positionStatusUpdateTimer = 0;
        }
        else
        {
            m_positionStatusUpdateTimer -= update_diff;
        }
    }

    // Update weapon change timer
    if (m_weaponChangeTimer > 0)
    {
        if (update_diff >= m_weaponChangeTimer)
        {
            m_weaponChangeTimer = 0;
        }
        else
        {
            m_weaponChangeTimer -= update_diff;
        }
    }

    // Update zone timer
    if (m_zoneUpdateTimer > 0)
    {
        if (update_diff >= m_zoneUpdateTimer)
        {
            uint32 newzone, newarea;
            GetZoneAndAreaId(newzone, newarea);

            if (m_zoneUpdateId != newzone)
            {
                UpdateZone(newzone, newarea); // Also update area
            }
            else
            {
                // Use area updates as well
                // Needed for free-for-all arenas, for example
                if (m_areaUpdateId != newarea)
                {
                    UpdateArea(newarea);
                }

                m_zoneUpdateTimer = ZONE_UPDATE_INTERVAL;
            }
        }
        else
        {
            m_zoneUpdateTimer -= update_diff;
        }
    }

    // Update time sync timer
    if (m_timeSyncTimer > 0)
    {
        if (update_diff >= m_timeSyncTimer)
        {
            SendTimeSync();
        }
        else
        {
            m_timeSyncTimer -= update_diff;
        }
    }

    // Regenerate all if the player is alive
    if (IsAlive())
    {
        RegenerateAll();
    }

    // Handle player death
    if (m_deathState == JUST_DIED)
    {
        KillPlayer();
    }

    // Handle periodic saving
    if (m_nextSave > 0)
    {
        if (update_diff >= m_nextSave)
        {
            // m_nextSave reset in SaveToDB call
            // Used by Eluna
#ifdef ENABLE_ELUNA
            if (Eluna* e = GetEluna())
            {
                e->OnSave(this);
            }
#endif /* ENABLE_ELUNA */
            SaveToDB();
            DETAIL_LOG("Player '%s' (GUID: %u) saved", GetName(), GetGUIDLow());
        }
        else
        {
            m_nextSave -= update_diff;
        }
    }

    // Handle water/drowning
    HandleDrowning(update_diff);

    // Handle detect stealth players
    if (m_DetectInvTimer > 0)
    {
        if (update_diff >= m_DetectInvTimer)
        {
            HandleStealthedUnitsDetection();
            m_DetectInvTimer = 3000;
        }
        else
        {
            m_DetectInvTimer -= update_diff;
        }
    }

    // Update played time
    if (now > m_Last_tick)
    {
        uint32 elapsed = uint32(now - m_Last_tick);
        m_Played_time[PLAYED_TIME_TOTAL] += elapsed; // Total played time
        m_Played_time[PLAYED_TIME_LEVEL] += elapsed; // Level played time
        m_Last_tick = now;
    }

    // Handle sobering if the player is drunk
    if (m_drunk)
    {
        m_drunkTimer += update_diff;

        if (m_drunkTimer > 10 * IN_MILLISECONDS)
        {
            HandleSobering();
        }
    }

    // Handle ghost auto-free from body in instances
    if (m_deathTimer > 0 && !GetMap()->Instanceable())
    {
        if (p_time >= m_deathTimer)
        {
            m_deathTimer = 0;
            BuildPlayerRepop();
            RepopAtGraveyard();
        }
        else
        {
            m_deathTimer -= p_time;
        }
    }

    // Update enchant time
    UpdateEnchantTime(update_diff);

    // Update homebind time
    UpdateHomebindTime(update_diff);

    // Group update
    SendUpdateToOutOfRangeGroupMembers();

    // Handle pet unsummoning if out of range
    Pet* pet = GetPet();
    if (pet && !pet->IsWithinDistInMap(this, GetMap()->GetVisibilityDistance()) && (GetCharmGuid() && (pet->GetObjectGuid() != GetCharmGuid())))
    {
        pet->Unsummon(PET_SAVE_REAGENTS, this);
    }

    // Handle delayed teleport
    if (IsHasDelayedTeleport())
    {
        TeleportTo(m_teleport_dest, m_teleport_options);
    }

#ifdef ENABLE_PLAYERBOTS
    // Update player bot AI
    if (m_playerbotAI)
    {
        m_playerbotAI->UpdateAI(p_time);
    }
    if (m_playerbotMgr)
    {
        m_playerbotMgr->UpdateAI(p_time);
    }
#endif

}

/**
 * @brief Changes the player's death state and handles player-specific death logic.
 *
 * @param s The new death state.
 */
void Player::SetDeathState(DeathState s)
{
    uint32 ressSpellId = 0;

    bool cur = IsAlive();

    if (s == JUST_DIED && cur)
    {
        // drunken state is cleared on death
        SetDrunkValue(0);
        // lost combo points at any target (targeted combo points clear in Unit::SetDeathState)
        ClearComboPoints();

        clearResurrectRequestData();

        // remove form before other mods to prevent incorrect stats calculation
        RemoveSpellsCausingAura(SPELL_AURA_MOD_SHAPESHIFT);

        // FIXME: is pet dismissed at dying or releasing spirit? if second, add SetDeathState(DEAD) to HandleRepopRequestOpcode and define pet unsummon here with (s == DEAD)
        RemovePet(PET_SAVE_REAGENTS);

        // remove uncontrolled pets
        RemoveMiniPet();

        // save value before aura remove in Unit::SetDeathState
        ressSpellId = GetUInt32Value(PLAYER_SELF_RES_SPELL);

        // passive spell
        if (!ressSpellId)
        {
            ressSpellId = GetResurrectionSpellId();
        }

        if (InstanceData* mapInstance = GetInstanceData())
        {
            mapInstance->OnPlayerDeath(this);
        }
    }

    Unit::SetDeathState(s);

    // restore resurrection spell id for player after aura remove
    if (s == JUST_DIED && cur && ressSpellId)
    {
        SetUInt32Value(PLAYER_SELF_RES_SPELL, ressSpellId);
    }

    if (IsAlive() && !cur)
    {
        // clear aura case after resurrection by another way (spells will be applied before next death)
        SetUInt32Value(PLAYER_SELF_RES_SPELL, 0);

        // restore default warrior stance
        if (getClass() == CLASS_WARRIOR)
        {
            CastSpell(this, SPELL_ID_PASSIVE_BATTLE_STANCE, true);
        }
    }
}

/**
 * @brief Builds character-enumeration data for the character selection screen.
 *
 * @param result The database row for the character.
 * @param p_data The packet being populated.
 * @return true if the enum data was built successfully; otherwise, false.
 */
bool Player::BuildEnumData(QueryResult* result, WorldPacket* p_data)
{
    //             0               1                2                3                 4                  5                       6                        7
    //    "SELECT characters.guid, characters.name, characters.race, characters.class, characters.gender, characters.playerBytes, characters.playerBytes2, characters.level, "
    //     8                9               10                     11                     12                     13                    14
    //    "characters.zone, characters.map, characters.position_x, characters.position_y, characters.position_z, guild_member.guildid, characters.playerFlags, "
    //    15                    16                   17                     18                   19                         20
    //    "characters.at_login, character_pet.entry, character_pet.modelid, character_pet.level, characters.equipmentCache, character_declinedname.genitive "

    Field* fields = result->Fetch();

    uint32 guid = fields[0].GetUInt32();
    uint8 pRace = fields[2].GetUInt8();
    uint8 pClass = fields[3].GetUInt8();

    PlayerInfo const* info = sObjectMgr.GetPlayerInfo(pRace, pClass);
    if (!info)
    {
        sLog.outError("Player %u has incorrect race/class pair. Don't build enum.", guid);
        return false;
    }

    *p_data << ObjectGuid(HIGHGUID_PLAYER, guid);
    *p_data << fields[1].GetString();                       // name
    *p_data << uint8(pRace);                                // race
    *p_data << uint8(pClass);                               // class
    *p_data << uint8(fields[4].GetUInt8());                 // gender

    uint32 playerBytes = fields[5].GetUInt32();
    *p_data << uint8(playerBytes);                          // skin
    *p_data << uint8(playerBytes >> 8);                     // face
    *p_data << uint8(playerBytes >> 16);                    // hair style
    *p_data << uint8(playerBytes >> 24);                    // hair color

    uint32 playerBytes2 = fields[6].GetUInt32();
    *p_data << uint8(playerBytes2 & 0xFF);                  // facial hair

    *p_data << uint8(fields[7].GetUInt8());                 // level
    *p_data << uint32(fields[8].GetUInt32());               // zone
    *p_data << uint32(fields[9].GetUInt32());               // map

    *p_data << fields[10].GetFloat();                       // x
    *p_data << fields[11].GetFloat();                       // y
    *p_data << fields[12].GetFloat();                       // z

    *p_data << uint32(fields[13].GetUInt32());              // guild id

    uint32 char_flags = 0;
    uint32 playerFlags = fields[14].GetUInt32();
    uint32 atLoginFlags = fields[15].GetUInt32();
    if (playerFlags & PLAYER_FLAGS_HIDE_HELM)
    {
        char_flags |= CHARACTER_FLAG_HIDE_HELM;
    }
    if (playerFlags & PLAYER_FLAGS_HIDE_CLOAK)
    {
        char_flags |= CHARACTER_FLAG_HIDE_CLOAK;
    }
    if (playerFlags & PLAYER_FLAGS_GHOST)
    {
        char_flags |= CHARACTER_FLAG_GHOST;
    }
    if (atLoginFlags & AT_LOGIN_RENAME)
    {
        char_flags |= CHARACTER_FLAG_RENAME;
    }
    if (sWorld.getConfig(CONFIG_BOOL_DECLINED_NAMES_USED))
    {
        if (!fields[20].GetCppString().empty())
        {
            char_flags |= CHARACTER_FLAG_DECLINED;
        }
    }
    else
    {
        char_flags |= CHARACTER_FLAG_DECLINED;
    }

    *p_data << uint32(char_flags);                          // character flags

    // First login
    *p_data << uint8(atLoginFlags & AT_LOGIN_FIRST ? 1 : 0);

    // Pets info
    {
        uint32 petDisplayId = 0;
        uint32 petLevel   = 0;
        uint32 petFamily  = 0;

        // show pet at selection character in character list only for non-ghost character
        if (result && !(playerFlags & PLAYER_FLAGS_GHOST) && (pClass == CLASS_WARLOCK || pClass == CLASS_HUNTER))
        {
            uint32 entry = fields[16].GetUInt32();
            CreatureInfo const* cInfo = sCreatureStorage.LookupEntry<CreatureInfo>(entry);
            if (cInfo)
            {
                petDisplayId = fields[17].GetUInt32();
                petLevel     = fields[18].GetUInt32();
                petFamily    = cInfo->Family;
            }
        }

        *p_data << uint32(petDisplayId);
        *p_data << uint32(petLevel);
        *p_data << uint32(petFamily);
    }

    Tokens data = StrSplit(fields[19].GetCppString(), " ");
    for (uint8 slot = 0; slot < EQUIPMENT_SLOT_END; ++slot)
    {
        uint32 visualbase = slot * 2;                       // entry, perm ench., temp ench.
        uint32 item_id = GetUInt32ValueFromArray(data, visualbase);
        const ItemPrototype* proto = ObjectMgr::GetItemPrototype(item_id);
        if (!proto)
        {
            *p_data << uint32(0);
            *p_data << uint8(0);
            *p_data << uint32(0);
            continue;
        }

        SpellItemEnchantmentEntry const* enchant = NULL;

        uint32 enchants = GetUInt32ValueFromArray(data, visualbase + 1);
        for (uint8 enchantSlot = PERM_ENCHANTMENT_SLOT; enchantSlot <= TEMP_ENCHANTMENT_SLOT; ++enchantSlot)
        {
            // values stored in 2 uint16
            uint32 enchantId = 0x0000FFFF & (enchants >> enchantSlot * 16);
            if (!enchantId)
            {
                continue;
            }

            if ((enchant = sSpellItemEnchantmentStore.LookupEntry(enchantId)))
            {
                break;
            }
        }

        *p_data << uint32(proto->DisplayInfoID);
        *p_data << uint8(proto->InventoryType);
        *p_data << uint32(enchant ? enchant->aura_id : 0);
    }

    *p_data << uint32(0);                                   // first bag display id
    *p_data << uint8(0);                                    // first bag inventory type
    *p_data << uint32(0);                                   // enchant?

    return true;
}

/**
 * @brief Toggles AFK status and leaves battlegrounds when entering AFK.
 */
void Player::ToggleAFK()
{
    ToggleFlag(PLAYER_FLAGS, PLAYER_FLAGS_AFK);

    // afk player not allowed in battleground
    if (isAFK() && InBattleGround() && !InArena())
    {
        LeaveBattleground();
    }
}

/**
 * @brief Toggles DND status.
 */
void Player::ToggleDND()
{
    ToggleFlag(PLAYER_FLAGS, PLAYER_FLAGS_DND);
}

/**
 * @brief Gets the active chat tag displayed for the player.
 *
 * @return The chat tag flags for GM, AFK, DND, or none.
 */
ChatTagFlags Player::GetChatTag() const
{
    ChatTagFlags tag = CHAT_TAG_NONE;

    if (isAFK())
    {
        tag |= CHAT_TAG_AFK;
    }
    if (isDND())
    {
        tag |= CHAT_TAG_DND;
    }
    if (isGMChat())
    {
        tag |= CHAT_TAG_GM;
    }

    return tag;
}

/**
 * @brief Teleports the player to a target location, handling near and far cases.
 *
 * @param mapid The destination map id.
 * @param x The destination x coordinate.
 * @param y The destination y coordinate.
 * @param z The destination z coordinate.
 * @param orientation The destination orientation.
 * @param options Teleport option flags.
 * @param allowNoDelay true to bypass delayed-teleport deferral when possible.
 * @return true if teleport setup succeeded; otherwise, false.
 */
bool Player::TeleportTo(uint32 mapid, float x, float y, float z, float orientation, uint32 options /*=0*/, AreaTrigger const* at /*=NULL*/)
{
    // Stop cinematic flyover on teleport (body is map-bound)
    if (m_cinematicFlyover && m_cinematicFlyover->IsActive())
    {
        m_cinematicFlyover->Stop();
    }

    if (!MapManager::IsValidMapCoord(mapid, x, y, z, orientation))
    {
        sLog.outError("TeleportTo: invalid map %d or absent instance template.", mapid);
        return false;
    }

    MapEntry const* mEntry = sMapStore.LookupEntry(mapid);  // Validity checked in IsValidMapCoord

    if (!isGameMaster() && DisableMgr::IsDisabledFor(DISABLE_TYPE_MAP, mapid, this))
    {
        sLog.outDebug("Player (GUID: %u, name: %s) tried to enter a forbidden map %u", GetGUIDLow(), GetName(), mapid);
        SendTransferAbortedByLockStatus(mEntry, AREA_LOCKSTATUS_NOT_ALLOWED);
        return false;
    }

    // preparing unsummon pet if lost (we must get pet before teleportation or will not find it later)
    Pet* pet = GetPet();

    // don't let enter battlegrounds without assigned battleground id (for example through areatrigger)...
    // don't let gm level > 1 either
    if (!InBattleGround() && mEntry->IsBattleGroundOrArena())
    {
        return false;
    }

    // Get MapEntrance trigger if teleport to other -nonBG- map
    bool assignedAreaTrigger = false;
    if (GetMapId() != mapid && !mEntry->IsBattleGroundOrArena() && !at)
    {
        at = sObjectMgr.GetMapEntranceTrigger(mapid);
        assignedAreaTrigger = true;
    }

    // Check requirements for teleport
    if (at)
    {
        uint32 miscRequirement = 0;
        AreaLockStatus lockStatus = GetAreaTriggerLockStatus(at, miscRequirement);
        if (lockStatus != AREA_LOCKSTATUS_OK)
        {
            // Teleport not requested by area-trigger
            // TODO - Assume a player with expansion 0 travels from BootyBay to Ratched, and he is attempted to be teleported to outlands
            //        then he will repop near BootyBay instead of normally continuing his journey
            // This code is probably added to catch passengers on ships to northrend who shouldn't go there
            if (lockStatus == AREA_LOCKSTATUS_INSUFFICIENT_EXPANSION && !assignedAreaTrigger && GetTransport())
            {
                RepopAtGraveyard();                         // Teleport to near graveyard if on transport, looks blizz like :)
            }

            SendTransferAbortedByLockStatus(mEntry, lockStatus, miscRequirement);
            return false;
        }
    }

    // if we were on a transport, leave
    if (!(options & TELE_TO_NOT_LEAVE_TRANSPORT) && m_transport)
    {
        m_transport->RemovePassenger(this);
        m_transport = NULL;
        m_movementInfo.ClearTransportData();
    }

    // The player was ported to another map and looses the duel immediately.
    // We have to perform this check before the teleport, otherwise the
    // ObjectAccessor won't find the flag.
    if (duel && GetMapId() != mapid)
    {
        if (GetMap()->GetGameObject(GetGuidValue(PLAYER_DUEL_ARBITER)))
        {
            DuelComplete(DUEL_FLED);
        }
    }

    // reset movement flags at teleport, because player will continue move with these flags after teleport
    m_movementInfo.SetMovementFlags(MOVEFLAG_NONE);
    DisableSpline();

    if ((GetMapId() == mapid) && (!m_transport))            // TODO the !m_transport might have unexpected effects when teleporting from transport to other place on same map
    {
        // lets reset far teleport flag if it wasn't reset during chained teleports
        SetSemaphoreTeleportFar(false);
        // setup delayed teleport flag
        // if teleport spell is casted in Unit::Update() func
        // then we need to delay it until update process will be finished
        if (SetDelayedTeleportFlagIfCan())
        {
            SetSemaphoreTeleportNear(true);
            // lets save teleport destination for player
            m_teleport_dest = WorldLocation(mapid, x, y, z, orientation);
            m_teleport_options = options;
            return true;
        }

        if (!(options & TELE_TO_NOT_UNSUMMON_PET))
        {
            // same map, only remove pet if out of range for new position
            if (pet && !pet->IsWithinDist3d(x, y, z, GetMap()->GetVisibilityDistance()))
            {
                UnsummonPetTemporaryIfAny();
            }
        }

        if (!(options & TELE_TO_NOT_LEAVE_COMBAT))
        {
            CombatStop();
        }

        // this will be used instead of the current location in SaveToDB
        m_teleport_dest = WorldLocation(mapid, x, y, z, orientation);
        SetFallInformation(0, z);

        // code for finish transfer called in WorldSession::HandleMovementOpcodes()
        // at client packet MSG_MOVE_TELEPORT_ACK
        SetSemaphoreTeleportNear(true);
        // near teleport, triggering send MSG_MOVE_TELEPORT_ACK from client at landing
        if (!GetSession()->PlayerLogout())
        {
            WorldPacket data;
            BuildTeleportAckMsg(data, x, y, z, orientation);
            GetSession()->SendPacket(&data);
        }
    }
    else
    {
        // far teleport to another map
        Map* oldmap = IsInWorld() ? GetMap() : NULL;
        // check if we can enter before stopping combat / removing pet / totems / interrupting spells

        // If the map is not created, assume it is possible to enter it.
        // It will be created in the WorldPortAck.
        DungeonPersistentState* state = GetBoundInstanceSaveForSelfOrGroup(mapid);
        Map* map = sMapMgr.FindMap(mapid, state ? state->GetInstanceId() : 0);
        if (!map || map->CanEnter(this))
        {
            // lets reset near teleport flag if it wasn't reset during chained teleports
            SetSemaphoreTeleportNear(false);
            // setup delayed teleport flag
            // if teleport spell is casted in Unit::Update() func
            // then we need to delay it until update process will be finished
            if (SetDelayedTeleportFlagIfCan())
            {
                SetSemaphoreTeleportFar(true);
                // lets save teleport destination for player
                m_teleport_dest = WorldLocation(mapid, x, y, z, orientation);
                m_teleport_options = options;
                return true;
            }

            SetSelectionGuid(ObjectGuid());

            CombatStop();

            ResetContestedPvP();

            // remove player from battleground on far teleport (when changing maps)
            if (BattleGround const* bg = GetBattleGround())
            {
                // Note: at battleground join battleground id set before teleport
                // and we already will found "current" battleground
                // just need check that this is targeted map or leave
                if (bg->GetMapId() != mapid)
                {
                    LeaveBattleground(false); // don't teleport to entry point
                }
            }

            // remove pet on map change
            if (pet)
            {
                UnsummonPetTemporaryIfAny();
            }

            // remove all dyn objects
            RemoveAllDynObjects();

            // stop spellcasting
            // not attempt interrupt teleportation spell at caster teleport
            if (!(options & TELE_TO_SPELL))
            {
                if (IsNonMeleeSpellCasted(true))
                {
                    InterruptNonMeleeSpells(true);
                }
            }

            // remove auras before removing from map...
            RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_CHANGE_MAP | AURA_INTERRUPT_FLAG_MOVE | AURA_INTERRUPT_FLAG_TURNING);

            if (!GetSession()->PlayerLogout())
            {
                // send transfer packet to display load screen
                WorldPacket data(SMSG_TRANSFER_PENDING, (4 + 4 + 4));
                data << uint32(mapid);
                if (m_transport)
                {
                    data << uint32(m_transport->GetEntry());
                    data << uint32(GetMapId());
                }
                GetSession()->SendPacket(&data);
            }

            // remove from old map now
            if (oldmap)
            {
                oldmap->Remove(this, false);
            }

            // new final coordinates
            float final_x = x;
            float final_y = y;
            float final_z = z;
            float final_o = orientation;

            Position const* transportPosition = m_movementInfo.GetTransportPos();

            if (m_transport)
            {
                final_x += transportPosition->x;
                final_y += transportPosition->y;
                final_z += transportPosition->z;
                final_o += transportPosition->o;
            }

            m_teleport_dest = WorldLocation(mapid, final_x, final_y, final_z, final_o);
            SetFallInformation(0, final_z);
            // if the player is saved before worldport ack (at logout for example)
            // this will be used instead of the current location in SaveToDB

            // move packet sent by client always after far teleport
            // code for finish transfer to new map called in WorldSession::HandleMoveWorldportAckOpcode at client packet
            SetSemaphoreTeleportFar(true);

            if (!GetSession()->PlayerLogout())
            {
                // transfer finished, inform client to start load
                WorldPacket data(SMSG_NEW_WORLD, (20));
                data << uint32(mapid);
                if (m_transport)
                {
                    data << float(transportPosition->x);
                    data << float(transportPosition->y);
                    data << float(transportPosition->z);
                    data << float(transportPosition->o);
                }
                else
                {
                    data << float(final_x);
                    data << float(final_y);
                    data << float(final_z);
                    data << float(final_o);
                }

                GetSession()->SendPacket(&data);
                SendSavedInstances();
            }
        }
        else                                                // !map->CanEnter(this)
        {
            return false;
        }
    }
    return true;
}

/**
 * @brief Teleports the player back to the saved battleground entry point.
 *
 * @return true if the teleport was initiated successfully; otherwise, false.
 */
bool Player::TeleportToBGEntryPoint()
{
    return TeleportTo(m_bgData.joinPos);
}

/**
 * @brief Executes queued delayed player operations.
 */
void Player::ProcessDelayedOperations()
{
    if (m_DelayedOperations == 0)
    {
        return;
    }

    if (m_DelayedOperations & DELAYED_RESURRECT_PLAYER)
    {
        ResurrectPlayer(0.0f, false);

        if (GetMaxHealth() > m_resurrectHealth)
        {
            SetHealth(m_resurrectHealth);
        }
        else
        {
            SetHealth(GetMaxHealth());
        }

        if (GetMaxPower(POWER_MANA) > m_resurrectMana)
        {
            SetPower(POWER_MANA, m_resurrectMana);
        }
        else
        {
            SetPower(POWER_MANA, GetMaxPower(POWER_MANA));
        }

        SetPower(POWER_RAGE, 0);
        SetPower(POWER_ENERGY, GetMaxPower(POWER_ENERGY));

        SpawnCorpseBones();
    }

    if (m_DelayedOperations & DELAYED_SAVE_PLAYER)
    {
        SaveToDB();
    }

    if (m_DelayedOperations & DELAYED_SPELL_CAST_DESERTER)
    {
        CastSpell(this, 26013, true);               // Deserter
    }

    // we have executed ALL delayed ops, so clear the flag
    m_DelayedOperations = 0;
}

/**
 * @brief Adds the player and equipped items to the world.
 */
void Player::AddToWorld()
{
    ///- Do not add/remove the player from the object storage
    ///- It will crash when updating the ObjectAccessor
    ///- The player should only be added when logging in
    Unit::AddToWorld();

    for (int i = PLAYER_SLOT_START; i < PLAYER_SLOT_END; ++i)
    {
        if (m_items[i])
        {
            m_items[i]->AddToWorld();
        }
    }
}

/**
 * @brief Removes the player and equipped items from the world.
 */
void Player::RemoveFromWorld()
{
    // cleanup
    if (IsInWorld())
    {
        ///- Release charmed creatures, unsummon totems and remove pets/guardians
        UnsummonAllTotems();
        RemoveMiniPet();
    }

    for (int i = PLAYER_SLOT_START; i < PLAYER_SLOT_END; ++i)
    {
        if (m_items[i])
        {
            m_items[i]->RemoveFromWorld();
        }
    }

    // remove duel before calling Unit::RemoveFromWorld
    // otherwise there will be an existing duel flag pointer but no entry in m_gameObj
    DuelComplete(DUEL_INTERRUPTED);

    ///- Do not add/remove the player from the object storage
    ///- It will crash when updating the ObjectAccessor
    ///- The player should only be removed when logging out
    if (IsInWorld())
    {
        GetCamera().ResetView();
    }

    Unit::RemoveFromWorld();
}

/**
 * @brief Awards rage generated from dealing or receiving damage.
 *
 * @param damage The raw damage amount used for rage generation.
 * @param attacker True when the player dealt the damage; false when the player received it.
 */
void Player::RewardRage(uint32 damage, uint32 weaponSpeedHitFactor, bool attacker)
{
    float addRage;

    float rageconversion = float((0.0091107836 * getLevel() * getLevel()) + 3.225598133 * getLevel()) + 4.2652911f;

    if (attacker)
    {
        addRage = ((damage / rageconversion * 7.5f + weaponSpeedHitFactor) / 2.0f);

        // talent who gave more rage on attack
        addRage *= 1.0f + GetTotalAuraModifier(SPELL_AURA_MOD_RAGE_FROM_DAMAGE_DEALT) / 100.0f;
    }
    else
    {
        addRage = damage / rageconversion * 2.5f;

        // Berserker Rage effect
        if (HasAura(18499, EFFECT_INDEX_0))
        {
            addRage *= 1.3f;
        }
    }

    addRage *= sWorld.getConfig(CONFIG_FLOAT_RATE_POWER_RAGE_INCOME);

    ModifyPower(POWER_RAGE, uint32(addRage * 10));
}

/**
 * @brief Regenerates the player's health and power resources for the current tick.
 */
void Player::RegenerateAll()
{
    if (m_regenTimer != 0)
    {
        return;
    }

    // Not in combat or they have regeneration
    if (!IsInCombat() || HasAuraType(SPELL_AURA_MOD_REGEN_DURING_COMBAT) ||
        HasAuraType(SPELL_AURA_MOD_HEALTH_REGEN_IN_COMBAT) || IsPolymorphed())
    {
        RegenerateHealth();
        if (!IsInCombat() && !HasAuraType(SPELL_AURA_INTERRUPT_REGEN))
        {
            Regenerate(POWER_RAGE);
        }
    }

    Regenerate(POWER_ENERGY);

    Regenerate(POWER_MANA);

    m_regenTimer = REGEN_TIME_FULL;
}

/**
 * @brief Regenerates or decays a specific player power type.
 *
 * @param power The power type to update.
 */
void Player::Regenerate(Powers power)
{
    uint32 curValue = GetPower(power);
    uint32 maxValue = GetMaxPower(power);

    float addvalue = 0.0f;

    switch (power)
    {
        case POWER_MANA:
        {
            bool recentCast = IsUnderLastManaUseEffect();
            float ManaIncreaseRate = sWorld.getConfig(CONFIG_FLOAT_RATE_POWER_MANA);
            if (recentCast)
            {
                // Mangos Updates Mana in intervals of 2s, which is correct
                addvalue = GetFloatValue(PLAYER_FIELD_MOD_MANA_REGEN_INTERRUPT) *  ManaIncreaseRate * 2.00f;
            }
            else
            {
                addvalue = GetFloatValue(PLAYER_FIELD_MOD_MANA_REGEN) * ManaIncreaseRate * 2.00f;
            }
        }   break;
        case POWER_RAGE:                                    // Regenerate rage
        {
            float RageDecreaseRate = sWorld.getConfig(CONFIG_FLOAT_RATE_POWER_RAGE_LOSS);
            addvalue = 20 * RageDecreaseRate;               // 2 rage by tick (= 2 seconds => 1 rage/sec)
        }   break;
        case POWER_ENERGY:                                  // Regenerate energy (rogue)
        {
            float EnergyRate = sWorld.getConfig(CONFIG_FLOAT_RATE_POWER_ENERGY);
            addvalue = 20 * EnergyRate;
            break;
        }
        case POWER_FOCUS:
        case POWER_HAPPINESS:
        case POWER_HEALTH:
            break;
        default:
            break;
    }

    // Mana regen calculated in Player::UpdateManaRegen()
    // Exist only for POWER_MANA, POWER_ENERGY, POWER_FOCUS auras
    if (power != POWER_MANA)
    {
        AuraList const& ModPowerRegenPCTAuras = GetAurasByType(SPELL_AURA_MOD_POWER_REGEN_PERCENT);
        for (AuraList::const_iterator i = ModPowerRegenPCTAuras.begin(); i != ModPowerRegenPCTAuras.end(); ++i)
        {
            if ((*i)->GetModifier()->m_miscvalue == int32(power))
            {
                addvalue *= ((*i)->GetModifier()->m_amount + 100) / 100.0f;
            }
        }
    }

    if (power != POWER_RAGE)
    {
        curValue += uint32(addvalue);
        if (curValue > maxValue)
        {
            curValue = maxValue;
        }
    }
    else
    {
        if (curValue <= uint32(addvalue))
        {
            curValue = 0;
        }
        else
        {
            curValue -= uint32(addvalue);
        }
    }

    SetPower(power, curValue);
}

/**
 * @brief Regenerates the player's health based on state, auras, and rates.
 */
void Player::RegenerateHealth()
{
    uint32 curValue = GetHealth();
    uint32 maxValue = GetMaxHealth();

    if (curValue >= maxValue)
    {
        return;
    }

    float HealthIncreaseRate = sWorld.getConfig(CONFIG_FLOAT_RATE_HEALTH);

    float addvalue = 0.0f;

    // polymorphed case
    if (IsPolymorphed())
    {
        addvalue = (float)GetMaxHealth() / 3;
    }
    // normal regen case (maybe partly in combat case)
    else if (!IsInCombat() || HasAuraType(SPELL_AURA_MOD_REGEN_DURING_COMBAT))
    {
        addvalue = OCTRegenHPPerSpirit() * HealthIncreaseRate;
        if (!IsInCombat())
        {
            AuraList const& mModHealthRegenPct = GetAurasByType(SPELL_AURA_MOD_HEALTH_REGEN_PERCENT);
            for (AuraList::const_iterator i = mModHealthRegenPct.begin(); i != mModHealthRegenPct.end(); ++i)
            {
                addvalue *= (100.0f + (*i)->GetModifier()->m_amount) / 100.0f;
            }
        }
        else if (HasAuraType(SPELL_AURA_MOD_REGEN_DURING_COMBAT))
        {
            addvalue *= GetTotalAuraModifier(SPELL_AURA_MOD_REGEN_DURING_COMBAT) / 100.0f;
        }

        if (!IsStandState())
        {
            addvalue *= 1.5;
        }
    }

    // always regeneration bonus (including combat)
    addvalue += GetTotalAuraModifier(SPELL_AURA_MOD_HEALTH_REGEN_IN_COMBAT);

    if (addvalue < 0)
    {
        addvalue = 0;
    }

    ModifyHealth(int32(addvalue));
}

/**
 * @brief Gets an NPC the player can currently interact with.
 *
 * @param guid The target creature GUID.
 * @param npcflagmask Optional NPC flag mask that must be present on the creature.
 * @return The interactable creature, or null if interaction is not allowed.
 */
Creature* Player::GetNPCIfCanInteractWith(ObjectGuid guid, uint32 npcflagmask)
{
    // some basic checks
    if (!guid || !IsInWorld() || IsTaxiFlying())
    {
        return NULL;
    }

    // not in interactive state
    if (hasUnitState(UNIT_STAT_CAN_NOT_REACT_OR_LOST_CONTROL))
    {
        return NULL;
    }

    // exist (we need look pets also for some interaction (quest/etc)
    Creature* unit = GetMap()->GetAnyTypeCreature(guid);
    if (!unit)
    {
        return NULL;
    }

    // appropriate npc type
    if (npcflagmask && !unit->HasFlag(UNIT_NPC_FLAGS, npcflagmask))
    {
        return NULL;
    }

    if (npcflagmask & UNIT_NPC_FLAG_STABLEMASTER)
    {
        if (getClass() != CLASS_HUNTER)
        {
            return NULL;
        }
    }

    // if a dead unit should be able to talk - the creature must be alive and have special flags
    if (!unit->IsAlive())
    {
        return NULL;
    }

    if (IsAlive() && unit->IsInvisibleForAlive())
    {
        return NULL;
    }

    // not allow interaction under control, but allow with own pets
    if (unit->GetCharmerGuid())
    {
        return NULL;
    }

    // not enemy
    if (unit->IsHostileTo(this))
    {
        return NULL;
    }

    // not too far
    if (!unit->IsWithinDistInMap(this, INTERACTION_DISTANCE))
    {
        return NULL;
    }

    return unit;
}

/**
 * @brief Gets a game object the player can currently interact with.
 *
 * @param guid The target game object GUID.
 * @param gameobject_type The required game object type, or MAX_GAMEOBJECT_TYPE for any type.
 * @return The interactable game object, or null if interaction is not allowed.
 */
GameObject* Player::GetGameObjectIfCanInteractWith(ObjectGuid guid, uint32 gameobject_type) const
{
    // some basic checks
    if (!guid || !IsInWorld() || IsTaxiFlying())
    {
        return NULL;
    }

    // not in interactive state
    if (hasUnitState(UNIT_STAT_CAN_NOT_REACT_OR_LOST_CONTROL))
    {
        return NULL;
    }

    if (GameObject* go = GetMap()->GetGameObject(guid))
    {
        if (uint32(go->GetGoType()) == gameobject_type || gameobject_type == MAX_GAMEOBJECT_TYPE)
        {
            float maxdist = go->GetInteractionDistance();
            if (go->IsWithinDistInMap(this, maxdist) && go->isSpawned())
            {
                return go;
            }

            sLog.outError("GetGameObjectIfCanInteractWith: GameObject '%s' [GUID: %u] is too far away from player %s [GUID: %u] to be used by him (distance=%f, maximal %f is allowed)",
                          go->GetGOInfo()->name,  go->GetGUIDLow(), GetName(), GetGUIDLow(), go->GetDistance(this), maxdist);
        }
    }
    return NULL;
}

/**
 * @brief Checks whether the player is currently underwater.
 *
 * @return True if the player is underwater; otherwise, false.
 */
bool Player::IsUnderWater() const
{
    return GetTerrain()->IsUnderWater(GetPositionX(), GetPositionY(), GetPositionZ() + 2);
}

/**
 * @brief Updates the player's in-water state.
 *
 * @param apply True if the player is entering water; false if leaving it.
 */
void Player::SetInWater(bool apply)
{
    if (m_isInWater == apply)
    {
        return;
    }

    // define player in water by opcodes
    // move player's guid into HateOfflineList of those mobs
    // which can't swim and move guid back into ThreatList when
    // on surface.
    // TODO: exist also swimming mobs, and function must be symmetric to enter/leave water
    m_isInWater = apply;

    // remove auras that need water/land
    RemoveAurasWithInterruptFlags(apply ? AURA_INTERRUPT_FLAG_NOT_ABOVEWATER : AURA_INTERRUPT_FLAG_NOT_UNDERWATER);

    GetHostileRefManager().updateThreatTables();
}

struct SetGameMasterOnHelper
{
    explicit SetGameMasterOnHelper() {}
    void operator()(Unit* unit) const
    {
        unit->setFaction(35);
        unit->GetHostileRefManager().setOnlineOfflineState(false);
    }
};

struct SetGameMasterOffHelper
{
    explicit SetGameMasterOffHelper(uint32 _faction) : faction(_faction) {}
    void operator()(Unit* unit) const
    {
        unit->setFaction(faction);
        unit->GetHostileRefManager().setOnlineOfflineState(true);
    }
    uint32 faction;
};

/**
 * @brief Enables or disables game master mode for the player.
 *
 * @param on True to enable GM mode; false to disable it.
 */
void Player::SetGameMaster(bool on)
{
    if (on)
    {
        m_ExtraFlags |= PLAYER_EXTRA_GM_ON;
        //setFaction(35);
        SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_UNK_0);
        SetFlag(PLAYER_FLAGS, PLAYER_FLAGS_GM);
        CallForAllControlledUnits(SetGameMasterOnHelper(), CONTROLLED_PET | CONTROLLED_TOTEMS | CONTROLLED_GUARDIANS | CONTROLLED_CHARM);

        SetFFAPvP(false);
        ResetContestedPvP();

        GetHostileRefManager().setOnlineOfflineState(false);
        CombatStopWithPets();

        if (Pet* pet = GetPet())
        {
            if (m_ExtraFlags |= PLAYER_EXTRA_GM_ON)
                pet->setFaction(35);
            pet->GetHostileRefManager().setOnlineOfflineState(false);
        }
    }
    else
    {
        m_ExtraFlags &= ~ PLAYER_EXTRA_GM_ON;
        //setFactionForRace(getRace());
        RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_UNK_0);
        RemoveFlag(PLAYER_FLAGS, PLAYER_FLAGS_GM);

        if (Pet* pet = GetPet())
        {
            pet->setFaction(getFaction());
            pet->GetHostileRefManager().setOnlineOfflineState(true);
        }


        CallForAllControlledUnits(SetGameMasterOffHelper(getFaction()), CONTROLLED_PET | CONTROLLED_TOTEMS | CONTROLLED_GUARDIANS | CONTROLLED_CHARM);

        // restore FFA PvP Server state
        if (sWorld.IsFFAPvPRealm())
        {
            SetFFAPvP(true);
        }

        // restore FFA PvP area state, remove not allowed for GM mounts
        UpdateArea(m_areaUpdateId);

        GetHostileRefManager().setOnlineOfflineState(true);
    }

    m_camera.UpdateVisibilityForOwner();
    UpdateObjectVisibility();
    UpdateForQuestWorldObjects();
}

/**
 * @brief Sets whether a game master is visible to other players.
 *
 * @param on True to make the GM visible; false to hide them.
 */
void Player::SetGMVisible(bool on)
{
    if (on)
    {
        m_ExtraFlags &= ~PLAYER_EXTRA_GM_INVISIBLE;         // remove flag

        // Reapply stealth/invisibility if active or show if not any
        if (HasAuraType(SPELL_AURA_MOD_STEALTH))
        {
            SetVisibility(VISIBILITY_GROUP_STEALTH);
        }
        else if (HasAuraType(SPELL_AURA_MOD_INVISIBILITY))
        {
            SetVisibility(VISIBILITY_GROUP_INVISIBILITY);
        }
        else
        {
            SetVisibility(VISIBILITY_ON);
        }
    }
    else
    {
        m_ExtraFlags |= PLAYER_EXTRA_GM_INVISIBLE;          // add flag

        SetAcceptWhispers(false);
        SetGameMaster(true);

        SetVisibility(VISIBILITY_OFF);
    }
}

/**
 * @brief Checks whether another player should be visible through group visibility rules.
 *
 * @param p The player to test visibility against.
 * @return True if the player is group-visible; otherwise, false.
 */
bool Player::IsGroupVisibleFor(Player* p) const
{
    switch (sWorld.getConfig(CONFIG_UINT32_GROUP_VISIBILITY))
    {
        default:
            return IsInSameGroupWith(p);
        case 1:
            return IsInSameRaidWith(p);
        case 2:
            return GetTeam() == p->GetTeam();
    }
}

/**
 * @brief Checks whether this player and another player are in the same subgroup.
 *
 * @param p The other player to compare.
 * @return True if both players share the same group context; otherwise, false.
 */
bool Player::IsInSameGroupWith(Player const* p) const
{
    return (p == this || (GetGroup() != NULL &&
                          GetGroup()->SameSubGroup(this, p)));
}

///- If the player is invited, remove him. If the group if then only 1 person, disband the group.
/// \todo Shouldn't we also check if there is no other invitees before disbanding the group?
void Player::UninviteFromGroup()
{
    Group* group = GetGroupInvite();
    if (!group)
    {
        return;
    }

    group->RemoveInvite(this);

    if (group->GetMembersCount() <= 1)                      // group has just 1 member => disband
    {
        if (group->IsCreated())
        {
            group->Disband(true);
            sObjectMgr.RemoveGroup(group);
        }
        else
        {
            group->RemoveAllInvites();
        }

        delete group;
    }
}

/**
 * @brief Removes a member from a group and disposes of the group if it becomes empty.
 *
 * @param group The group to remove the member from.
 * @param guid The GUID of the member to remove.
 * @param removeMethod The group removal method to apply.
 */
void Player::RemoveFromGroup(Group* group, ObjectGuid guid)
{
    if (group)
    {
        if (group->RemoveMember(guid, 0) <= 1)
        {
            // group->Disband(); already disbanded in RemoveMember
            sObjectMgr.RemoveGroup(group);
            delete group;
            // RemoveMember sets the player's group pointer to NULL
        }
    }
}

/**
 * @brief Sends the experience gain log packet to the client.
 *
 * @param GivenXP The base amount of experience awarded.
 * @param victim The kill source, or null for non-kill experience.
 * @param RestXP The rested bonus experience amount.
 */
void Player::SendLogXPGain(uint32 GivenXP, Unit* victim, uint32 RestXP)
{
    WorldPacket data(SMSG_LOG_XPGAIN, 21);
    data << (victim ? victim->GetObjectGuid() : ObjectGuid());// guid
    data << uint32(GivenXP + RestXP);                       // given experience
    data << uint8(victim ? 0 : 1);                          // 00-kill_xp type, 01-non_kill_xp type
    if (victim)
    {
        data << uint32(GivenXP);                            // experience without rested bonus
        data << float(1);                                   // 1 - none 0 - 100% group bonus output
    }
    data << uint8(0);                                       // new 2.4.0
    GetSession()->SendPacket(&data);
}

/**
 * @brief Awards experience to the player and handles level-ups.
 *
 * @param xp The experience amount to award.
 * @param victim The unit responsible for kill-based experience, if any.
 */
void Player::GiveXP(uint32 xp, Unit* victim)
{
    if (xp < 1)
    {
        return;
    }

    if (!IsAlive())
    {
        return;
    }

    uint32 level = getLevel();

    // Used by Eluna
#ifdef ENABLE_ELUNA
    if (Eluna* e = GetEluna())
    {
        e->OnGiveXP(this, xp, victim);
    }
#endif /* ENABLE_ELUNA */

    // XP to money conversion processed in Player::RewardQuest
    if (level >= sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL))
    {
        return;
    }

    // handle SPELL_AURA_MOD_XP_PCT auras
    Unit::AuraList const& ModXPPctAuras = GetAurasByType(SPELL_AURA_MOD_XP_PCT);
    for (Unit::AuraList::const_iterator i = ModXPPctAuras.begin(); i != ModXPPctAuras.end(); ++i)
    {
        xp = uint32(xp * (1.0f + (*i)->GetModifier()->m_amount / 100.0f));
    }

    // XP resting bonus for kill
    uint32 rested_bonus_xp = victim ? GetXPRestBonus(xp) : 0;

    SendLogXPGain(xp, victim, rested_bonus_xp);

    uint32 curXP = GetUInt32Value(PLAYER_XP);
    uint32 nextLvlXP = GetUInt32Value(PLAYER_NEXT_LEVEL_XP);
    uint32 newXP = curXP + xp + rested_bonus_xp;

    while (newXP >= nextLvlXP && level < sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL))
    {
        newXP -= nextLvlXP;

        if (level < sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL))
        {
            GiveLevel(level + 1);
        }

        level = getLevel();
        nextLvlXP = GetUInt32Value(PLAYER_NEXT_LEVEL_XP);
    }

    SetUInt32Value(PLAYER_XP, newXP);
}

// Update player to next level
// Current player experience not update (must be update by caller)
/**
 * @brief Advances the player to a new level and reapplies level-based stats.
 *
 * @param level The new level to assign.
 */
void Player::GiveLevel(uint32 level)
{
    uint8 oldLevel = getLevel();
    if (level == getLevel())
    {
        return;
    }

    PlayerLevelInfo info;
    sObjectMgr.GetPlayerLevelInfo(getRace(), getClass(), level, &info);

    PlayerClassLevelInfo classInfo;
    sObjectMgr.GetPlayerClassLevelInfo(getClass(), level, &classInfo);

    // send levelup info to client
    WorldPacket data(SMSG_LEVELUP_INFO, (4 + 4 + MAX_POWERS * 4 + MAX_STATS * 4));
    data << uint32(level);
    data << uint32(int32(classInfo.basehealth) - int32(GetCreateHealth()));
    // for(int i = 0; i < MAX_POWERS; ++i)                  // Powers loop (0-6)
    data << uint32(int32(classInfo.basemana)   - int32(GetCreateMana()));
    data << uint32(0);
    data << uint32(0);
    data << uint32(0);
    data << uint32(0);
    // end for
    for (int i = STAT_STRENGTH; i < MAX_STATS; ++i)         // Stats loop (0-4)
    {
        data << uint32(int32(info.stats[i]) - GetCreateStat(Stats(i)));
    }

    GetSession()->SendPacket(&data);

    SetUInt32Value(PLAYER_NEXT_LEVEL_XP, sObjectMgr.GetXPForLevel(level));

    // update level, max level of skills
    if (getLevel() != level)
    {
        m_Played_time[PLAYED_TIME_LEVEL] = 0; // Level Played Time reset
    }
    SetLevel(level);
    UpdateSkillsForLevel();

    // save base values (bonuses already included in stored stats
    for (int i = STAT_STRENGTH; i < MAX_STATS; ++i)
    {
        SetCreateStat(Stats(i), info.stats[i]);
    }

    SetCreateHealth(classInfo.basehealth);
    SetCreateMana(classInfo.basemana);

    InitTalentForLevel();
    InitTaxiNodesForLevel();

    UpdateAllStats();

    // set current level health and mana/energy to maximum after applying all mods.
    if (IsAlive())
    {
        SetHealth(GetMaxHealth());
    }
    SetPower(POWER_MANA, GetMaxPower(POWER_MANA));
    SetPower(POWER_ENERGY, GetMaxPower(POWER_ENERGY));
    if (GetPower(POWER_RAGE) > GetMaxPower(POWER_RAGE))
    {
        SetPower(POWER_RAGE, GetMaxPower(POWER_RAGE));
    }
    SetPower(POWER_FOCUS, 0);
    SetPower(POWER_HAPPINESS, 0);

    // update level to hunter/summon pet
    if (Pet* pet = GetPet())
    {
        pet->SynchronizeLevelWithOwner();
    }

    // Used by Eluna
#ifdef ENABLE_ELUNA
    if (Eluna* e = GetEluna())
    {
        e->OnLevelChanged(this, oldLevel);
    }
#endif /* ENABLE_ELUNA */

    if (MailLevelReward const* mailReward = sObjectMgr.GetMailLevelReward(level, getRaceMask()))
    {
        MailDraft(mailReward->mailTemplateId).SendMailTo(this, MailSender(MAIL_CREATURE, mailReward->senderEntry));
    }
}

/**
 * @brief Sets the number of free talent points available to the player.
 *
 * @param points The free talent point count.
 */
void Player::SetFreeTalentPoints(uint32 points)
{
    // Used by Eluna
#ifdef ENABLE_ELUNA
    if (Eluna* e = GetEluna())
    {
        e->OnFreeTalentPointsChanged(this, points);
    }
#endif /* ENABLE_ELUNA */

    SetUInt32Value(PLAYER_CHARACTER_POINTS1, points);
}

/**
 * @brief Recalculates the player's free talent points for the current level.
 *
 * @param resetIfNeed True to reset talents when the allocation is invalid.
 */
void Player::UpdateFreeTalentPoints(bool resetIfNeed)
{
    uint32 level = getLevel();
    // talents base at level diff ( talents = level - 9 but some can be used already)
    if (level < 10)
    {
        // Remove all talent points
        if (m_usedTalentCount > 0)                          // Free any used talents
        {
            if (resetIfNeed)
            {
                resetTalents(true);
            }
            SetFreeTalentPoints(0);
        }
    }
    else
    {
        uint32 talentPointsForLevel = CalculateTalentsPoints();

        // if used more that have then reset
        if (m_usedTalentCount > talentPointsForLevel)
        {
            if (resetIfNeed && GetSession()->GetSecurity() < SEC_ADMINISTRATOR)
            {
                resetTalents(true);
            }
            else
            {
                SetFreeTalentPoints(0);
            }
        }
        // else update amount of free points
        else
        {
            SetFreeTalentPoints(talentPointsForLevel - m_usedTalentCount);
        }
    }
}

/**
 * @brief Initializes level-based talent availability for the player.
 */
void Player::InitTalentForLevel()
{
    UpdateFreeTalentPoints();
}

/**
 * @brief Initializes the player's base stats and resources for the current level.
 *
 * @param reapplyMods True to remove and reapply stat modifiers during initialization.
 */
void Player::InitStatsForLevel(bool reapplyMods)
{
    if (reapplyMods)                                        // reapply stats values only on .reset stats (level) command
    {
        _RemoveAllStatBonuses();
    }

    PlayerClassLevelInfo classInfo;
    sObjectMgr.GetPlayerClassLevelInfo(getClass(), getLevel(), &classInfo);

    PlayerLevelInfo info;
    sObjectMgr.GetPlayerLevelInfo(getRace(), getClass(), getLevel(), &info);

    SetUInt32Value(PLAYER_FIELD_MAX_LEVEL, sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL));
    SetUInt32Value(PLAYER_NEXT_LEVEL_XP, sObjectMgr.GetXPForLevel(getLevel()));

    // reset before any aura state sources (health set/aura apply)
    SetUInt32Value(UNIT_FIELD_AURASTATE, 0);

    UpdateSkillsForLevel();

    // set default cast time multiplier
    SetFloatValue(UNIT_MOD_CAST_SPEED, 1.0f);

    // save base values (bonuses already included in stored stats
    for (int i = STAT_STRENGTH; i < MAX_STATS; ++i)
    {
        SetCreateStat(Stats(i), info.stats[i]);
    }

    for (int i = STAT_STRENGTH; i < MAX_STATS; ++i)
    {
        SetStat(Stats(i), info.stats[i]);
    }

    SetCreateHealth(classInfo.basehealth);

    // set create powers
    SetCreateMana(classInfo.basemana);

    SetArmor(int32(m_createStats[STAT_AGILITY] * 2));

    InitStatBuffMods();

    // reset rating fields values
    for (uint16 index = PLAYER_FIELD_COMBAT_RATING_1; index < PLAYER_FIELD_COMBAT_RATING_1 + MAX_COMBAT_RATING; ++index)
    {
        SetUInt32Value(index, 0);
    }

    SetUInt32Value(PLAYER_FIELD_MOD_HEALING_DONE_POS, 0);
    for (int i = 0; i < MAX_SPELL_SCHOOL; ++i)
    {
        SetUInt32Value(PLAYER_FIELD_MOD_DAMAGE_DONE_NEG + i, 0);
        SetUInt32Value(PLAYER_FIELD_MOD_DAMAGE_DONE_POS + i, 0);
        SetFloatValue(PLAYER_FIELD_MOD_DAMAGE_DONE_PCT + i, 1.00f);
    }

    // reset attack power, damage and attack speed fields
    SetFloatValue(UNIT_FIELD_BASEATTACKTIME, 2000.0f);
    SetFloatValue(UNIT_FIELD_BASEATTACKTIME + 1, 2000.0f);  // offhand attack time
    SetFloatValue(UNIT_FIELD_RANGEDATTACKTIME, 2000.0f);

    SetFloatValue(UNIT_FIELD_MINDAMAGE, 0.0f);
    SetFloatValue(UNIT_FIELD_MAXDAMAGE, 0.0f);
    SetFloatValue(UNIT_FIELD_MINOFFHANDDAMAGE, 0.0f);
    SetFloatValue(UNIT_FIELD_MAXOFFHANDDAMAGE, 0.0f);
    SetFloatValue(UNIT_FIELD_MINRANGEDDAMAGE, 0.0f);
    SetFloatValue(UNIT_FIELD_MAXRANGEDDAMAGE, 0.0f);

    SetInt32Value(UNIT_FIELD_ATTACK_POWER,            0);
    SetInt32Value(UNIT_FIELD_ATTACK_POWER_MODS,       0);
    SetFloatValue(UNIT_FIELD_ATTACK_POWER_MULTIPLIER, 0.0f);
    SetInt32Value(UNIT_FIELD_RANGED_ATTACK_POWER,     0);
    SetInt32Value(UNIT_FIELD_RANGED_ATTACK_POWER_MODS, 0);
    SetFloatValue(UNIT_FIELD_RANGED_ATTACK_POWER_MULTIPLIER, 0.0f);

    // Base crit values (will be recalculated in UpdateAllStats() at loading and in _ApplyAllStatBonuses() at reset
    SetFloatValue(PLAYER_CRIT_PERCENTAGE, 0.0f);
    SetFloatValue(PLAYER_OFFHAND_CRIT_PERCENTAGE, 0.0f);
    SetFloatValue(PLAYER_RANGED_CRIT_PERCENTAGE, 0.0f);

    // Init spell schools (will be recalculated in UpdateAllStats() at loading and in _ApplyAllStatBonuses() at reset
    for (uint8 i = 0; i < MAX_SPELL_SCHOOL; ++i)
    {
        SetFloatValue(PLAYER_SPELL_CRIT_PERCENTAGE1 + i, 0.0f);
    }

    SetFloatValue(PLAYER_PARRY_PERCENTAGE, 0.0f);
    SetFloatValue(PLAYER_BLOCK_PERCENTAGE, 0.0f);
    SetUInt32Value(PLAYER_SHIELD_BLOCK, 0);

    // Dodge percentage
    SetFloatValue(PLAYER_DODGE_PERCENTAGE, 0.0f);

    // set armor (resistance 0) to original value (create_agility*2)
    SetArmor(int32(m_createStats[STAT_AGILITY] * 2));
    SetResistanceBuffMods(SpellSchools(0), true, 0.0f);
    SetResistanceBuffMods(SpellSchools(0), false, 0.0f);
    // set other resistance to original value (0)
    for (int i = 1; i < MAX_SPELL_SCHOOL; ++i)
    {
        SetResistance(SpellSchools(i), 0);
        SetResistanceBuffMods(SpellSchools(i), true, 0.0f);
        SetResistanceBuffMods(SpellSchools(i), false, 0.0f);
    }

    SetUInt32Value(PLAYER_FIELD_MOD_TARGET_RESISTANCE, 0);
    SetUInt32Value(PLAYER_FIELD_MOD_TARGET_PHYSICAL_RESISTANCE, 0);
    for (int i = 0; i < MAX_SPELL_SCHOOL; ++i)
    {
        SetUInt32Value(UNIT_FIELD_POWER_COST_MODIFIER + i, 0);
        SetFloatValue(UNIT_FIELD_POWER_COST_MULTIPLIER + i, 0.0f);
    }
    // Init data for form but skip reapply item mods for form
    InitDataForForm(reapplyMods);

    // save new stats
    for (int i = POWER_MANA; i < MAX_POWERS; ++i)
    {
        SetMaxPower(Powers(i),  GetCreatePowers(Powers(i)));
    }

    SetMaxHealth(classInfo.basehealth);                     // stamina bonus will applied later

    // cleanup mounted state (it will set correctly at aura loading if player saved at mount.
    SetUInt32Value(UNIT_FIELD_MOUNTDISPLAYID, 0);

    // cleanup unit flags (will be re-applied if need at aura load).
    RemoveFlag(UNIT_FIELD_FLAGS,
               UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_DISABLE_MOVE | UNIT_FLAG_NOT_ATTACKABLE_1 |
               UNIT_FLAG_OOC_NOT_ATTACKABLE | UNIT_FLAG_PASSIVE  | UNIT_FLAG_LOOTING          |
               UNIT_FLAG_PET_IN_COMBAT  | UNIT_FLAG_SILENCED     | UNIT_FLAG_PACIFIED         |
               UNIT_FLAG_STUNNED        | UNIT_FLAG_IN_COMBAT    | UNIT_FLAG_DISARMED         |
               UNIT_FLAG_CONFUSED       | UNIT_FLAG_FLEEING      | UNIT_FLAG_NOT_SELECTABLE   |
               UNIT_FLAG_SKINNABLE      | UNIT_FLAG_MOUNT        | UNIT_FLAG_TAXI_FLIGHT);
    SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PVP_ATTACKABLE);    // must be set

    // cleanup player flags (will be re-applied if need at aura load), to avoid have ghost flag without ghost aura, for example.
    RemoveFlag(PLAYER_FLAGS, PLAYER_FLAGS_AFK | PLAYER_FLAGS_DND | PLAYER_FLAGS_GM | PLAYER_FLAGS_GHOST | PLAYER_FLAGS_FFA_PVP);

    RemoveStandFlags(UNIT_STAND_FLAGS_ALL);                 // one form stealth modified bytes

    // restore if need some important flags
    SetUInt32Value(PLAYER_FIELD_BYTES2, 0);                 // flags empty by default

    if (reapplyMods)                                        // reapply stats values only on .reset stats (level) command
    {
        _ApplyAllStatBonuses();
    }

    // set current level health and mana/energy to maximum after applying all mods.
    SetHealth(GetMaxHealth());
    SetPower(POWER_MANA, GetMaxPower(POWER_MANA));
    SetPower(POWER_ENERGY, GetMaxPower(POWER_ENERGY));
    if (GetPower(POWER_RAGE) > GetMaxPower(POWER_RAGE))
    {
        SetPower(POWER_RAGE, GetMaxPower(POWER_RAGE));
    }
    SetPower(POWER_FOCUS, 0);
    SetPower(POWER_HAPPINESS, 0);

    // update level to hunter/summon pet
    if (Pet* pet = GetPet())
    {
        pet->SynchronizeLevelWithOwner();
    }
}

/* Used during Player::SendInitialPacketsBeforeAddToMap */
/**
 * @brief Sends the initial spellbook and cooldown state to the client.
 */
void Player::SendInitialSpells()
{
    time_t curTime = time(NULL);
    time_t infTime = curTime + infinityCooldownDelayCheck;

    /* * * * * * * * * * * * * * * * *
     * * START OF PACKET STRUCTURE * *
     * * * * * * * * * * * * * * * * */
    uint16 spellCount = 0;

    WorldPacket data(SMSG_INITIAL_SPELLS, (1 + 2 + 4 * m_spells.size() + 2 + m_spellCooldowns.size() * (2 + 2 + 2 + 4 + 4)));
    data << uint8(0);

    /* * * * * * * * * * * * * * * * *
     * *  END OF PACKET STRUCTURE  * *
     * * * * * * * * * * * * * * * * */
    size_t countPos = data.wpos();
    data << uint16(spellCount);                             // spell count placeholder

    /* For each spell the player knows */
    for (PlayerSpellMap::const_iterator itr = m_spells.begin(); itr != m_spells.end(); ++itr)
    {
        /* If the spell is marked as removed, don't send it */
        PlayerSpell const& playerSpell = itr->second;

        if (playerSpell.state == PLAYERSPELL_REMOVED)
        {
            continue;
        }

        if (!playerSpell.active || playerSpell.disabled)
        {
            continue;
        }

        /* Insert spell into vector for insertion into packet */
        data << uint16(itr->first);
        data << uint16(0);                                  // it's not slot id

        /* Increase spell counter by 1 (sent in packet) */
        spellCount += 1;
    }

    data.put<uint16>(countPos, spellCount);                 // write real count value

    /* For each spell the player has on cooldown */
    uint16 spellCooldowns = m_spellCooldowns.size();
    data << uint16(spellCooldowns);
    for (SpellCooldowns::const_iterator itr = m_spellCooldowns.begin(); itr != m_spellCooldowns.end(); ++itr)
    {
        /* If the spell doesn't exist in the spellbook, just ignore it */
        SpellEntry const* sEntry = sSpellStore.LookupEntry(itr->first);
        if (!sEntry)
        {
            continue;
        }

        SpellCooldown const& spellCooldown = itr->second;

        data << uint16(itr->first);

        data << uint16(spellCooldown.itemid);               // cast item id
        data << uint16(sEntry->Category);                   // spell category

        /* send infinity cooldown in special format */
        if (spellCooldown.end >= infTime)
        {
            data << uint32(1);                              // cooldown
            data << uint32(0x80000000);                     // category cooldown
            continue;
        }

        time_t cooldown = spellCooldown.end > curTime ? (spellCooldown.end - curTime) * IN_MILLISECONDS : 0;

        if (sEntry->Category)                               // may be wrong, but anyway better than nothing...
        {
            data << uint32(0);                              // cooldown
            data << uint32(cooldown);                       // category cooldown
        }
        else
        {
            data << uint32(cooldown);                       // cooldown
            data << uint32(0);                              // category cooldown
        }
    }

    GetSession()->SendPacket(&data);

    DETAIL_LOG("CHARACTER: Sent Initial Spells");
}

/**
 * @brief Populates the create update mask for a target player.
 *
 * @param updateMask The update mask being built.
 * @param target The player receiving the create data.
 */
void Player::_SetCreateBits(UpdateMask* updateMask, Player* target) const
{
    if (target == this)
    {
        Object::_SetCreateBits(updateMask, target);
    }
    else
    {
        for (uint16 index = 0; index < m_valuesCount; ++index)
        {
            if (GetUInt32Value(index) != 0 && updateVisualBits.GetBit(index))
            {
                updateMask->SetBit(index);
            }
        }
    }
}

/**
 * @brief Populates the incremental update mask for a target player.
 *
 * @param updateMask The update mask being built.
 * @param target The player receiving the update data.
 */
void Player::_SetUpdateBits(UpdateMask* updateMask, Player* target) const
{
    if (target == this)
    {
        Object::_SetUpdateBits(updateMask, target);
    }
    else
    {
        Object::_SetUpdateBits(updateMask, target);
        *updateMask &= updateVisualBits;
    }
}

/**
 * @brief Initializes the set of player fields visible to other clients.
 */
void Player::InitVisibleBits()
{
    updateVisualBits.SetCount(PLAYER_END);

    // TODO: really implement OWNER_ONLY and GROUP_ONLY. Flags can be found in UpdateFields.h

    updateVisualBits.SetBit(OBJECT_FIELD_GUID);
    updateVisualBits.SetBit(OBJECT_FIELD_TYPE);
    updateVisualBits.SetBit(OBJECT_FIELD_SCALE_X);
    updateVisualBits.SetBit(UNIT_FIELD_CHARM + 0);
    updateVisualBits.SetBit(UNIT_FIELD_CHARM + 1);
    updateVisualBits.SetBit(UNIT_FIELD_SUMMON + 0);
    updateVisualBits.SetBit(UNIT_FIELD_SUMMON + 1);
    updateVisualBits.SetBit(UNIT_FIELD_CHARMEDBY + 0);
    updateVisualBits.SetBit(UNIT_FIELD_CHARMEDBY + 1);
    updateVisualBits.SetBit(UNIT_FIELD_TARGET + 0);
    updateVisualBits.SetBit(UNIT_FIELD_TARGET + 1);
    updateVisualBits.SetBit(UNIT_FIELD_CHANNEL_OBJECT + 0);
    updateVisualBits.SetBit(UNIT_FIELD_CHANNEL_OBJECT + 1);
    updateVisualBits.SetBit(UNIT_FIELD_HEALTH);
    updateVisualBits.SetBit(UNIT_FIELD_POWER1);
    updateVisualBits.SetBit(UNIT_FIELD_POWER2);
    updateVisualBits.SetBit(UNIT_FIELD_POWER3);
    updateVisualBits.SetBit(UNIT_FIELD_POWER4);
    updateVisualBits.SetBit(UNIT_FIELD_POWER5);
    updateVisualBits.SetBit(UNIT_FIELD_MAXHEALTH);
    updateVisualBits.SetBit(UNIT_FIELD_MAXPOWER1);
    updateVisualBits.SetBit(UNIT_FIELD_MAXPOWER2);
    updateVisualBits.SetBit(UNIT_FIELD_MAXPOWER3);
    updateVisualBits.SetBit(UNIT_FIELD_MAXPOWER4);
    updateVisualBits.SetBit(UNIT_FIELD_MAXPOWER5);
    updateVisualBits.SetBit(UNIT_FIELD_LEVEL);
    updateVisualBits.SetBit(UNIT_FIELD_FACTIONTEMPLATE);
    updateVisualBits.SetBit(UNIT_FIELD_BYTES_0);
    updateVisualBits.SetBit(UNIT_FIELD_FLAGS);
    updateVisualBits.SetBit(UNIT_FIELD_FLAGS_2);
    for (uint16 i = UNIT_FIELD_AURA; i < UNIT_FIELD_AURASTATE; ++i)
    {
        updateVisualBits.SetBit(i);
    }
    updateVisualBits.SetBit(UNIT_FIELD_AURASTATE);
    updateVisualBits.SetBit(UNIT_FIELD_BASEATTACKTIME + 0);
    updateVisualBits.SetBit(UNIT_FIELD_BASEATTACKTIME + 1);
    updateVisualBits.SetBit(UNIT_FIELD_BOUNDINGRADIUS);
    updateVisualBits.SetBit(UNIT_FIELD_COMBATREACH);
    updateVisualBits.SetBit(UNIT_FIELD_DISPLAYID);
    updateVisualBits.SetBit(UNIT_FIELD_NATIVEDISPLAYID);
    updateVisualBits.SetBit(UNIT_FIELD_MOUNTDISPLAYID);
    updateVisualBits.SetBit(UNIT_FIELD_BYTES_1);
    updateVisualBits.SetBit(UNIT_FIELD_PETNUMBER);
    updateVisualBits.SetBit(UNIT_FIELD_PET_NAME_TIMESTAMP);
    updateVisualBits.SetBit(UNIT_DYNAMIC_FLAGS);
    updateVisualBits.SetBit(UNIT_CHANNEL_SPELL);
    updateVisualBits.SetBit(UNIT_MOD_CAST_SPEED);
    updateVisualBits.SetBit(UNIT_FIELD_BYTES_2);

    updateVisualBits.SetBit(PLAYER_DUEL_ARBITER + 0);
    updateVisualBits.SetBit(PLAYER_DUEL_ARBITER + 1);
    updateVisualBits.SetBit(PLAYER_FLAGS);
    updateVisualBits.SetBit(PLAYER_GUILDID);
    updateVisualBits.SetBit(PLAYER_GUILDRANK);
    updateVisualBits.SetBit(PLAYER_BYTES);
    updateVisualBits.SetBit(PLAYER_BYTES_2);
    updateVisualBits.SetBit(PLAYER_BYTES_3);
    updateVisualBits.SetBit(PLAYER_DUEL_TEAM);
    updateVisualBits.SetBit(PLAYER_GUILD_TIMESTAMP);

    // PLAYER_QUEST_LOG_x also visible bit on official (but only on party/raid)...
    for (uint16 i = PLAYER_QUEST_LOG_1_1; i < PLAYER_QUEST_LOG_25_2; i += MAX_QUEST_OFFSET)
    {
        updateVisualBits.SetBit(i);
    }

    // Players visible items are not inventory stuff
    // 431) = 884 (0x374) = main weapon
    for (uint16 i = 0; i < EQUIPMENT_SLOT_END; i++)
    {
        // item creator
        updateVisualBits.SetBit(PLAYER_VISIBLE_ITEM_1_CREATOR + (i * MAX_VISIBLE_ITEM_OFFSET) + 0);
        updateVisualBits.SetBit(PLAYER_VISIBLE_ITEM_1_CREATOR + (i * MAX_VISIBLE_ITEM_OFFSET) + 1);

        uint16 visual_base = PLAYER_VISIBLE_ITEM_1_0 + (i * MAX_VISIBLE_ITEM_OFFSET);

        // item entry
        updateVisualBits.SetBit(visual_base + 0);

        // item enchantment IDs
        for (uint8 j = 0; j < MAX_INSPECTED_ENCHANTMENT_SLOT; ++j)
        {
            updateVisualBits.SetBit(visual_base + 1 + j);
        }

        // random properties
        updateVisualBits.SetBit(PLAYER_VISIBLE_ITEM_1_PROPERTIES + 0 + (i * MAX_VISIBLE_ITEM_OFFSET));
        updateVisualBits.SetBit(PLAYER_VISIBLE_ITEM_1_PROPERTIES + 1 + (i * MAX_VISIBLE_ITEM_OFFSET));
    }

    updateVisualBits.SetBit(PLAYER_CHOSEN_TITLE);
}

/**
 * @brief Builds the create update block for a player observer.
 *
 * @param data The update data buffer to append to.
 * @param target The player receiving the create block.
 */
void Player::BuildCreateUpdateBlockForPlayer(UpdateData* data, Player* target) const
{
    if (target == this)
    {
        for (int i = 0; i < EQUIPMENT_SLOT_END; ++i)
        {
            if (m_items[i] == NULL)
            {
                continue;
            }

            m_items[i]->BuildCreateUpdateBlockForPlayer(data, target);
        }
        for (int i = INVENTORY_SLOT_BAG_START; i < BANK_SLOT_BAG_END; ++i)
        {
            if (m_items[i] == NULL)
            {
                continue;
            }

            m_items[i]->BuildCreateUpdateBlockForPlayer(data, target);
        }
        for (int i = KEYRING_SLOT_START; i < KEYRING_SLOT_END; ++i)
        {
            if (m_items[i] == NULL)
            {
                continue;
            }

            m_items[i]->BuildCreateUpdateBlockForPlayer(data, target);
        }
    }

    Unit::BuildCreateUpdateBlockForPlayer(data, target);
}

/**
 * @brief Builds destroy updates for the player and visible inventory objects.
 *
 * @param target The player that should receive the destroy updates.
 */
void Player::DestroyForPlayer(Player* target) const
{
    Unit::DestroyForPlayer(target);

    for (int i = 0; i < INVENTORY_SLOT_BAG_END; ++i)
    {
        if (m_items[i] == NULL)
        {
            continue;
        }

        m_items[i]->DestroyForPlayer(target);
    }

    if (target == this)
    {
        for (int i = INVENTORY_SLOT_BAG_START; i < BANK_SLOT_BAG_END; ++i)
        {
            if (m_items[i] == NULL)
            {
                continue;
            }

            m_items[i]->DestroyForPlayer(target);
        }
        for (int i = KEYRING_SLOT_START; i < KEYRING_SLOT_END; ++i)
        {
            if (m_items[i] == NULL)
            {
                continue;
            }

            m_items[i]->DestroyForPlayer(target);
        }
    }
}

/**
 * @brief Checks whether the player knows a spell.
 *
 * @param spell The spell identifier to test.
 * @return True if the spell exists and is not removed or disabled; otherwise, false.
 */
bool Player::HasSpell(uint32 spell) const
{
    PlayerSpellMap::const_iterator itr = m_spells.find(spell);
    if (itr == m_spells.end())
    {
        return false;
    }

    PlayerSpell const& playerSpell = itr->second;
    return playerSpell.state != PLAYERSPELL_REMOVED && !playerSpell.disabled;
}

/**
 * @brief Checks whether the player has a spell active in the spellbook.
 *
 * @param spell The spell identifier to test.
 * @return True if the spell is known, active, and not disabled; otherwise, false.
 */
bool Player::HasActiveSpell(uint32 spell) const
{
    PlayerSpellMap::const_iterator itr = m_spells.find(spell);
    if (itr == m_spells.end())
    {
        return false;
    }

    PlayerSpell const& playerSpell = itr->second;
    return playerSpell.state != PLAYERSPELL_REMOVED && playerSpell.active && !playerSpell.disabled;
}

/**
 * @brief Evaluates whether a trainer spell can be learned by the player.
 *
 * @param trainer_spell The trainer spell entry being checked.
 * @param reqLevel An optional override for the required level.
 * @return The trainer spell state to display to the client.
 */
TrainerSpellState Player::GetTrainerSpellState(TrainerSpell const* trainer_spell, uint32 reqLevel) const
{
    if (!trainer_spell)
    {
        return TRAINER_SPELL_RED;
    }

    if (!trainer_spell->spell)
    {
        return TRAINER_SPELL_RED;
    }

    // known spell
    if (HasSpell(trainer_spell->spell))
    {
        return TRAINER_SPELL_GRAY;
    }

    // check race/class requirement
    if (!IsSpellFitByClassAndRace(trainer_spell->spell))
    {
        return TRAINER_SPELL_RED;
    }

    bool prof = SpellMgr::IsProfessionSpell(trainer_spell->spell);

    // check level requirement
    if (!prof || GetSession()->GetSecurity() < AccountTypes(sWorld.getConfig(CONFIG_UINT32_TRADE_SKILL_GMIGNORE_LEVEL)))
    {
        if (getLevel() < reqLevel)
        {
            return TRAINER_SPELL_RED;
        }
    }

    if (SpellChainNode const* spell_chain = sSpellMgr.GetSpellChainNode(trainer_spell->spell))
    {
        // check prev.rank requirement
        if (spell_chain->prev && !HasSpell(spell_chain->prev))
        {
            return TRAINER_SPELL_RED;
        }

        // check additional spell requirement
        if (spell_chain->req && !HasSpell(spell_chain->req))
        {
            return TRAINER_SPELL_RED;
        }
    }

    // check skill requirement
    if (!prof || GetSession()->GetSecurity() < AccountTypes(sWorld.getConfig(CONFIG_UINT32_TRADE_SKILL_GMIGNORE_SKILL)))
        if (trainer_spell->reqSkill && GetBaseSkillValue(trainer_spell->reqSkill) < trainer_spell->reqSkillValue)
        {
            return TRAINER_SPELL_RED;
        }

    // exist, already checked at loading
    SpellEntry const* spell = sSpellStore.LookupEntry(trainer_spell->spell);

    // secondary prof. or not prof. spell
    uint32 skill = spell->EffectMiscValue[1];

    if (spell->Effect[1] != SPELL_EFFECT_SKILL || !IsPrimaryProfessionSkill(skill))
    {
        return TRAINER_SPELL_GREEN;
    }

    // check primary prof. limit
    if (sSpellMgr.IsPrimaryProfessionFirstRankSpell(spell->Id) && GetFreePrimaryProfessionPoints() == 0)
    {
        return TRAINER_SPELL_GREEN_DISABLED;
    }

    return TRAINER_SPELL_GREEN;
}

/**
 * Deletes a character from the database
 *
 * The way, how the characters will be deleted is decided based on the config option.
 *
 * @see Player::DeleteOldCharacters
 *
 * @param playerguid       the low-GUID from the player which should be deleted
 * @param accountId        the account id from the player
 * @param updateRealmChars when this flag is set, the amount of characters on that realm will be updated in the realmlist
 * @param deleteFinally    if this flag is set, the config option will be ignored and the character will be permanently removed from the database
 */
void Player::DeleteFromDB(ObjectGuid playerguid, uint32 accountId, bool updateRealmChars, bool deleteFinally)
{
    //Make sure to delete unresolved tickets so they don't take up place in the open tickets list
    CharacterDatabase.PExecute("DELETE FROM `character_ticket` "
                               "WHERE `resolved` = 0 AND `guid` = %u",
                               playerguid.GetCounter());

    // for nonexistent account avoid update realm
    if (accountId == 0)
    {
        updateRealmChars = false;
    }

    uint32 charDelete_method = sWorld.getConfig(CONFIG_UINT32_CHARDELETE_METHOD);
    uint32 charDelete_minLvl = sWorld.getConfig(CONFIG_UINT32_CHARDELETE_MIN_LEVEL);

    // if we want to finally delete the character or the character does not meet the level requirement, we set it to mode 0
    if (deleteFinally || Player::GetLevelFromDB(playerguid) < charDelete_minLvl)
    {
        charDelete_method = 0;
    }

    uint32 lowguid = playerguid.GetCounter();

    // convert corpse to bones if exist (to prevent exiting Corpse in World without DB entry)
    // bones will be deleted by corpse/bones deleting thread shortly
    sObjectAccessor.ConvertCorpseForPlayer(playerguid);

    // remove from guild
    if (uint32 guildId = GetGuildIdFromDB(playerguid))
    {
        if (Guild* guild = sGuildMgr.GetGuildById(guildId))
        {
            if (guild->DelMember(playerguid))
            {
                guild->Disband();
                delete guild;
            }
        }
    }

    // remove from arena teams
    LeaveAllArenaTeams(playerguid);

    // the player was uninvited already on logout so just remove from group
    QueryResult* resultGroup = CharacterDatabase.PQuery("SELECT `groupId` FROM `group_member` WHERE `memberGuid`='%u'", lowguid);
    if (resultGroup)
    {
        uint32 groupId = (*resultGroup)[0].GetUInt32();
        delete resultGroup;
        if (Group* group = sObjectMgr.GetGroupById(groupId))
        {
            RemoveFromGroup(group, playerguid);
        }
    }

    // remove signs from petitions (also remove petitions if owner);
    RemovePetitionsAndSigns(playerguid, 10);

    switch (charDelete_method)
    {
            // completely remove from the database
        case 0:
        {
            // return back all mails with COD and Item                  0    1             2                3        4         5      6       7
            QueryResult* resultMail = CharacterDatabase.PQuery("SELECT `id`,`messageType`,`mailTemplateId`,`sender`,`subject`,`body`,`money`,`has_items` FROM `mail` WHERE `receiver`='%u' AND `has_items`<>0 AND `cod`<>0", lowguid);
            if (resultMail)
            {
                do
                {
                    Field* fields = resultMail->Fetch();

                    uint32 mail_id       = fields[0].GetUInt32();
                    uint16 mailType      = fields[1].GetUInt16();
                    uint16 mailTemplateId = fields[2].GetUInt16();
                    uint32 sender        = fields[3].GetUInt32();
                    std::string subject  = fields[4].GetCppString();
                    std::string body     = fields[5].GetCppString();
                    uint32 money         = fields[6].GetUInt32();
                    bool has_items       = fields[7].GetBool();

                    // we can return mail now
                    // so firstly delete the old one
                    CharacterDatabase.PExecute("DELETE FROM `mail` WHERE `id` = '%u'", mail_id);

                    // mail not from player
                    if (mailType != MAIL_NORMAL)
                    {
                        if (has_items)
                        {
                            CharacterDatabase.PExecute("DELETE FROM `mail_items` WHERE `mail_id` = '%u'", mail_id);
                        }
                        continue;
                    }

                    MailDraft draft(subject, body);
                    if (mailTemplateId)
                    {
                        draft.SetMailTemplate(mailTemplateId, false); // items already included
                    }
                    else
                    {
                        draft.SetSubjectAndBody(subject, body);
                    }

                    if (has_items)
                    {
                        // data needs to be at first place for Item::LoadFromDB
                        //                                                           0      1      2           3
                        QueryResult* resultItems = CharacterDatabase.PQuery("SELECT `data`,`text`,`item_guid`,`item_template` FROM `mail_items` JOIN `item_instance` ON `item_guid` = `guid` WHERE `mail_id`='%u'", mail_id);
                        if (resultItems)
                        {
                            do
                            {
                                Field* fields2 = resultItems->Fetch();

                                uint32 item_guidlow = fields2[2].GetUInt32();
                                uint32 item_template = fields2[3].GetUInt32();

                                ItemPrototype const* itemProto = ObjectMgr::GetItemPrototype(item_template);
                                if (!itemProto)
                                {
                                    CharacterDatabase.PExecute("DELETE FROM `item_instance` WHERE `guid` = '%u'", item_guidlow);
                                    continue;
                                }

                                Item* pItem = NewItemOrBag(itemProto);
                                if (!pItem->LoadFromDB(item_guidlow, fields2, playerguid))
                                {
                                    pItem->FSetState(ITEM_REMOVED);
                                    pItem->SaveToDB();      // it also deletes item object !
                                    continue;
                                }

                                draft.AddItem(pItem);
                            }
                            while (resultItems->NextRow());

                            delete resultItems;
                        }
                    }

                    CharacterDatabase.PExecute("DELETE FROM `mail_items` WHERE `mail_id` = '%u'", mail_id);

                    uint32 pl_account = sObjectMgr.GetPlayerAccountIdByGUID(playerguid);

                    draft.SetMoney(money).SendReturnToSender(pl_account, playerguid, ObjectGuid(HIGHGUID_PLAYER, sender));
                }
                while (resultMail->NextRow());

                delete resultMail;
            }

            // unsummon and delete for pets in world is not required: player deleted from CLI or character list with not loaded pet.
            // Get guids of character's pets, will deleted in transaction
            QueryResult* resultPets = CharacterDatabase.PQuery("SELECT `id` FROM `character_pet` WHERE `owner` = '%u'", lowguid);

            // delete char from friends list when selected chars is online (non existing - error)
            QueryResult* resultFriend = CharacterDatabase.PQuery("SELECT DISTINCT `guid` FROM `character_social` WHERE `friend` = '%u'", lowguid);

            // NOW we can finally clear other DB data related to character
            CharacterDatabase.BeginTransaction();
            if (resultPets)
            {
                do
                {
                    Field* fields3 = resultPets->Fetch();
                    uint32 petguidlow = fields3[0].GetUInt32();
                    // do not create separate transaction for pet delete otherwise we will get fatal error!
                    Pet::DeleteFromDB(petguidlow, false);
                }
                while (resultPets->NextRow());
                delete resultPets;
            }

            // cleanup friends for online players, offline case will cleanup later in code
            if (resultFriend)
            {
                do
                {
                    Field* fieldsFriend = resultFriend->Fetch();
                    if (Player* sFriend = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, fieldsFriend[0].GetUInt32())))
                    {
                        if (sFriend->IsInWorld())
                        {
                            sFriend->GetSocial()->RemoveFromSocialList(playerguid, false);
                            sSocialMgr.SendFriendStatus(sFriend, FRIEND_REMOVED, playerguid, false);
                        }
                    }
                }
                while (resultFriend->NextRow());
                delete resultFriend;
            }

            CharacterDatabase.PExecute("DELETE FROM `characters` WHERE `guid` = '%u'", lowguid);
            CharacterDatabase.PExecute("DELETE FROM `character_declinedname` WHERE `guid` = '%u'", lowguid);
            CharacterDatabase.PExecute("DELETE FROM `character_action` WHERE `guid` = '%u'", lowguid);
            CharacterDatabase.PExecute("DELETE FROM `character_aura` WHERE `guid` = '%u'", lowguid);
            CharacterDatabase.PExecute("DELETE FROM `character_battleground_data` WHERE `guid` = '%u'", lowguid);
            CharacterDatabase.PExecute("DELETE FROM `character_gifts` WHERE `guid` = '%u'", lowguid);
            CharacterDatabase.PExecute("DELETE FROM `character_homebind` WHERE `guid` = '%u'", lowguid);
            CharacterDatabase.PExecute("DELETE FROM `character_instance` WHERE `guid` = '%u'", lowguid);
            CharacterDatabase.PExecute("DELETE FROM `group_instance` WHERE `leaderGuid` = '%u'", lowguid);
            CharacterDatabase.PExecute("DELETE FROM `character_inventory` WHERE `guid` = '%u'", lowguid);
            CharacterDatabase.PExecute("DELETE FROM `character_queststatus` WHERE `guid` = '%u'", lowguid);
            CharacterDatabase.PExecute("DELETE FROM `character_queststatus_daily` WHERE `guid` = '%u'", lowguid);
            CharacterDatabase.PExecute("DELETE FROM `character_reputation` WHERE `guid` = '%u'", lowguid);
            CharacterDatabase.PExecute("DELETE FROM `character_skills` WHERE `guid` = '%u'", lowguid);
            CharacterDatabase.PExecute("DELETE FROM `character_spell` WHERE `guid` = '%u'", lowguid);
            CharacterDatabase.PExecute("DELETE FROM `character_spell_cooldown` WHERE `guid` = '%u'", lowguid);
            CharacterDatabase.PExecute("DELETE FROM `character_ticket` WHERE `guid` = '%u'", lowguid);
            CharacterDatabase.PExecute("DELETE FROM `item_instance` WHERE `owner_guid` = '%u'", lowguid);
            CharacterDatabase.PExecute("DELETE FROM `character_social` WHERE `guid` = '%u' OR `friend`='%u'", lowguid, lowguid);
            CharacterDatabase.PExecute("DELETE FROM `mail` WHERE `receiver` = '%u'", lowguid);
            CharacterDatabase.PExecute("DELETE FROM `mail_items` WHERE `receiver` = '%u'", lowguid);
            CharacterDatabase.PExecute("DELETE FROM `character_pet` WHERE `owner` = '%u'", lowguid);
            CharacterDatabase.PExecute("DELETE FROM `character_pet_declinedname` WHERE `owner` = '%u'", lowguid);
            CharacterDatabase.PExecute("DELETE FROM `guild_eventlog` WHERE `PlayerGuid1` = '%u' OR `PlayerGuid2` = '%u'", lowguid, lowguid);
            CharacterDatabase.PExecute("DELETE FROM `guild_bank_eventlog` WHERE `PlayerGuid` = '%u'", lowguid);
            CharacterDatabase.CommitTransaction();
            break;
        }
        // The character gets unlinked from the account, the name gets freed up and appears as deleted ingame
        case 1:
            CharacterDatabase.PExecute("UPDATE `characters` SET `deleteInfos_Name`=`name`, `deleteInfos_Account`=`account`, `deleteDate`='" UI64FMTD "', `name`='', `account`=0 WHERE `guid`=%u", uint64(time(NULL)), lowguid);
            break;
        default:
            sLog.outError("Player::DeleteFromDB: Unsupported delete method: %u.", charDelete_method);
    }

    if (updateRealmChars)
    {
        sWorld.UpdateRealmCharCount(accountId);
    }
}

/**
 * Characters which were kept back in the database after being deleted and are now too old (see config option "CharDelete.KeepDays"), will be completely deleted.
 *
 * @see Player::DeleteFromDB
 */
void Player::DeleteOldCharacters()
{
    uint32 keepDays = sWorld.getConfig(CONFIG_UINT32_CHARDELETE_KEEP_DAYS);
    if (!keepDays)
    {
        return;
    }

    Player::DeleteOldCharacters(keepDays);
}

/**
 * Characters which were kept back in the database after being deleted and are older than the specified amount of days, will be completely deleted.
 *
 * @see Player::DeleteFromDB
 *
 * @param keepDays overrite the config option by another amount of days
 */
void Player::DeleteOldCharacters(uint32 keepDays)
{
    sLog.outString("Player::DeleteOldChars: Deleting all characters which have been deleted %u days before...", keepDays);

    QueryResult* resultChars = CharacterDatabase.PQuery("SELECT `guid`, `deleteInfos_Account` FROM `characters` WHERE `deleteDate` IS NOT NULL AND `deleteDate` < '" UI64FMTD "'", uint64(time(NULL) - time_t(keepDays * DAY)));
    if (resultChars)
    {
        sLog.outString("Player::DeleteOldChars: Found %u character(s) to delete", uint32(resultChars->GetRowCount()));
        do
        {
            Field* charFields = resultChars->Fetch();
            ObjectGuid guid = ObjectGuid(HIGHGUID_PLAYER, charFields[0].GetUInt32());
            Player::DeleteFromDB(guid, charFields[1].GetUInt32(), true, true);
        }
        while (resultChars->NextRow());
        delete resultChars;
    }
    sLog.outString();
}

/**
 * @brief Forces or clears rooted movement for the player.
 *
 * @param enable True to root the player; false to unroot them.
 */
void Player::SetRoot(bool enable)
{
    WorldPacket data(enable ? SMSG_FORCE_MOVE_ROOT : SMSG_FORCE_MOVE_UNROOT, GetPackGUID().size() + 4);
    data << GetPackGUID();
    data << uint32(0);
    SendMessageToSet(&data, true);
}

/**
 * @brief Enables or disables water walking for the player.
 *
 * @param enable True to enable water walking; false to restore normal movement.
 */
void Player::SetWaterWalk(bool enable)
{
    WorldPacket data(enable ? SMSG_MOVE_WATER_WALK : SMSG_MOVE_LAND_WALK, GetPackGUID().size() + 4);
    data << GetPackGUID();
    data << uint32(0);
    GetSession()->SendPacket(&data);
}

/**
 * @brief Placeholder for levitation support on this client version.
 *
 * @param enable Unused levitation state flag.
 */
void Player::SetLevitate(bool enable)
{
    // TODO: check if there is something similar for 2.4.3.
    // WorldPacket data;
    // if (enable)
    //    data.Initialize(SMSG_MOVE_GRAVITY_DISABLE, 12);
    // else
    //    data.Initialize(SMSG_MOVE_GRAVITY_ENABLE, 12);
    //
    // data << GetPackGUID();
    // data << uint32(0);                                      // unk
    // SendMessageToSet(&data, true);

    // data.Initialize(MSG_MOVE_GRAVITY_CHNG, 64);
    // data << GetPackGUID();
    // m_movementInfo.Write(data);
    // SendMessageToSet(&data, false);
}

/**
 * @brief Enables or disables flying movement flags for the player.
 *
 * @param enable True to enable flight-related movement flags; false to clear them.
 */
void Player::SetCanFly(bool enable)
{
    WorldPacket data;
    if (enable)
    {
        data.Initialize(SMSG_MOVE_SET_CAN_FLY, 12);
    }
    else
    {
        data.Initialize(SMSG_MOVE_UNSET_CAN_FLY, 12);
    }

    data << GetPackGUID();
    data << uint32(0);                                      // unk
    SendMessageToSet(&data, true);

    data.Initialize(MSG_MOVE_UPDATE_CAN_FLY, 64);
    data << GetPackGUID();
    m_movementInfo.Write(data);
    SendMessageToSet(&data, false);
}

/**
 * @brief Enables or disables feather fall movement for the player.
 *
 * @param enable True to enable feather fall; false to restore normal falling.
 */
void Player::SetFeatherFall(bool enable)
{
    WorldPacket data;
    if (enable)
    {
        data.Initialize(SMSG_MOVE_FEATHER_FALL, 8 + 4);
    }
    else
    {
        data.Initialize(SMSG_MOVE_NORMAL_FALL, 8 + 4);
    }

    data << GetPackGUID();
    data << uint32(0);
    SendMessageToSet(&data, true);

    // start fall from current height
    if (!enable)
    {
        SetFallInformation(0, GetPositionZ());
    }
}

/**
 * @brief Enables or disables hover movement for the player.
 *
 * @param enable True to enable hovering; false to disable it.
 */
void Player::SetHover(bool enable)
{
    WorldPacket data;
    if (enable)
    {
        data.Initialize(SMSG_MOVE_SET_HOVER, 8 + 4);
    }
    else
    {
        data.Initialize(SMSG_MOVE_UNSET_HOVER, 8 + 4);
    }

    data << GetPackGUID();
    data << uint32(0);
    SendMessageToSet(&data, true);
}

/* Preconditions:
  - a resurrectable corpse must not be loaded for the player (only bones)
  - the player must be in world
*/
void Player::BuildPlayerRepop()
{
    if (getRace() == RACE_NIGHTELF)
    {
        CastSpell(this, 20584, true); // auras SPELL_AURA_INCREASE_SPEED(+speed in wisp form), SPELL_AURA_INCREASE_SWIM_SPEED(+swim speed in wisp form), SPELL_AURA_TRANSFORM (to wisp form)
    }
    CastSpell(this, 8326, true);                            // auras SPELL_AURA_GHOST, SPELL_AURA_INCREASE_SPEED(why?), SPELL_AURA_INCREASE_SWIM_SPEED(why?)

    // the player can not have a corpse already, only bones which are not returned by GetCorpse
    if (GetCorpse())
    {
        sLog.outError("BuildPlayerRepop: player %s(%d) already has a corpse", GetName(), GetGUIDLow());
        MANGOS_ASSERT(false);
    }

    // create a corpse and place it at the player's location
    Corpse* corpse = CreateCorpse();
    if (!corpse)
    {
        sLog.outError("Error creating corpse for Player %s [%u]", GetName(), GetGUIDLow());
        return;
    }
    GetMap()->Add(corpse);

    // convert player body to ghost
    SetHealth(1);

    SetWaterWalk(true);
    if (!GetSession()->isLogingOut())
    {
        SetRoot(false);
    }

    // BG - remove insignia related
    RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_SKINNABLE);

    SendCorpseReclaimDelay();

    // to prevent cheating
    corpse->ResetGhostTime();

    StopMirrorTimers();                                     // disable timers(bars)

    // set and clear other
    SetByteValue(UNIT_FIELD_BYTES_1, 3, UNIT_BYTE1_FLAG_ALWAYS_STAND);
}

/**
 * @brief Restores the player to life and reapplies post-resurrection effects.
 *
 * @param restore_percent The fraction of health and resources to restore.
 * @param applySickness True to apply resurrection sickness when appropriate.
 */
void Player::ResurrectPlayer(float restore_percent, bool applySickness)
{
    WorldPacket data(SMSG_DEATH_RELEASE_LOC, 4 * 4);        // remove spirit healer position
    data << uint32(-1);
    data << float(0);
    data << float(0);
    data << float(0);
    GetSession()->SendPacket(&data);

    // speed change, land walk

    // remove death flag + set aura
    SetByteValue(UNIT_FIELD_BYTES_1, 3, 0x00);

    SetDeathState(ALIVE);

    if (getRace() == RACE_NIGHTELF)
    {
        RemoveAurasDueToSpell(20584); // speed bonuses
    }
    RemoveAurasDueToSpell(8326);                            // SPELL_AURA_GHOST

    SetWaterWalk(false);
    SetRoot(false);

    // set health/powers (0- will be set in caller)
    if (restore_percent > 0.0f)
    {
        SetHealth(uint32(GetMaxHealth()*restore_percent));
        SetPower(POWER_MANA, uint32(GetMaxPower(POWER_MANA)*restore_percent));
        SetPower(POWER_RAGE, 0);
        SetPower(POWER_ENERGY, uint32(GetMaxPower(POWER_ENERGY)*restore_percent));
    }

    // trigger update zone for alive state zone updates
    uint32 newzone, newarea;
    GetZoneAndAreaId(newzone, newarea);
    UpdateZone(newzone, newarea);

    m_deathTimer = 0;

    // update visibility of world around viewpoint
    m_camera.UpdateVisibilityForOwner();
    // update visibility of player for nearby cameras
    UpdateObjectVisibility();

#ifdef ENABLE_ELUNA
    if (Eluna* e = GetEluna())
    {
        e->OnResurrect(this);
    }
#endif /* ENABLE_ELUNA */

    if (!applySickness)
    {
        return;
    }

    // Characters from level 1-10 are not affected by resurrection sickness.
    // Characters from level 11-19 will suffer from one minute of sickness
    // for each level they are above 10.
    // Characters level 20 and up suffer from ten minutes of sickness.
    int32 startLevel = sWorld.getConfig(CONFIG_INT32_DEATH_SICKNESS_LEVEL);

    if (int32(getLevel()) >= startLevel)
    {
        // set resurrection sickness
        CastSpell(this, SPELL_ID_PASSIVE_RESURRECTION_SICKNESS, true);

        // not full duration
        if (int32(getLevel()) < startLevel + 9)
        {
            int32 delta = (int32(getLevel()) - startLevel + 1) * MINUTE;

            if (SpellAuraHolder* holder = GetSpellAuraHolder(SPELL_ID_PASSIVE_RESURRECTION_SICKNESS))
            {
                holder->SetAuraDuration(delta * IN_MILLISECONDS);
                holder->UpdateAuraDuration();
            }
        }
    }
}

/**
 * @brief Transitions the player into the corpse state after death.
 */
void Player::KillPlayer()
{
    SetRoot(true);

    StopMirrorTimers();                                     // disable timers(bars)

    SetDeathState(CORPSE);
    // SetFlag( UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_IN_PVP );

    SetUInt32Value(UNIT_DYNAMIC_FLAGS, UNIT_DYNFLAG_NONE);
    ApplyModByteFlag(PLAYER_FIELD_BYTES, 0, PLAYER_FIELD_BYTE_RELEASE_TIMER, !sMapStore.LookupEntry(GetMapId())->Instanceable());

    // 6 minutes until repop at graveyard
    m_deathTimer = 6 * MINUTE * IN_MILLISECONDS;

    UpdateCorpseReclaimDelay();                             // dependent at use SetDeathPvP() call before kill

    // don't create corpse at this moment, player might be falling

    // update visibility
    UpdateObjectVisibility();
}

/**
 * @brief Creates a corpse object for the player's current death state.
 *
 * @return The created corpse, or null if creation failed.
 */
Corpse* Player::CreateCorpse()
{
    // prevent existence 2 corpse for player
    SpawnCorpseBones();

    Corpse* corpse = new Corpse((m_ExtraFlags & PLAYER_EXTRA_PVP_DEATH) ? CORPSE_RESURRECTABLE_PVP : CORPSE_RESURRECTABLE_PVE);
    SetPvPDeath(false);

    if (!corpse->Create(sObjectMgr.GenerateCorpseLowGuid(), this))
    {
        delete corpse;
        return NULL;
    }

    uint8 skin       = GetByteValue(PLAYER_BYTES, 0);
    uint8 face       = GetByteValue(PLAYER_BYTES, 1);
    uint8 hairstyle  = GetByteValue(PLAYER_BYTES, 2);
    uint8 haircolor  = GetByteValue(PLAYER_BYTES, 3);
    uint8 facialhair = GetByteValue(PLAYER_BYTES_2, 0);

    corpse->SetByteValue(CORPSE_FIELD_BYTES_1, 1, getRace());
    corpse->SetByteValue(CORPSE_FIELD_BYTES_1, 2, getGender());
    corpse->SetByteValue(CORPSE_FIELD_BYTES_1, 3, skin);

    corpse->SetByteValue(CORPSE_FIELD_BYTES_2, 0, face);
    corpse->SetByteValue(CORPSE_FIELD_BYTES_2, 1, hairstyle);
    corpse->SetByteValue(CORPSE_FIELD_BYTES_2, 2, haircolor);
    corpse->SetByteValue(CORPSE_FIELD_BYTES_2, 3, facialhair);

    uint32 flags = CORPSE_FLAG_UNK2;
    if (HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_HIDE_HELM))
    {
        flags |= CORPSE_FLAG_HIDE_HELM;
    }
    if (HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_HIDE_CLOAK))
    {
        flags |= CORPSE_FLAG_HIDE_CLOAK;
    }
    if (InBattleGround() && !InArena())
    {
        flags |= CORPSE_FLAG_LOOTABLE; // to be able to remove insignia
    }
    corpse->SetUInt32Value(CORPSE_FIELD_FLAGS, flags);

    corpse->SetUInt32Value(CORPSE_FIELD_DISPLAY_ID, GetNativeDisplayId());

    corpse->SetUInt32Value(CORPSE_FIELD_GUILD, GetGuildId());

    uint32 iDisplayID;
    uint32 iIventoryType;
    uint32 _cfi;
    for (int i = 0; i < EQUIPMENT_SLOT_END; ++i)
    {
        if (m_items[i])
        {
            iDisplayID = m_items[i]->GetProto()->DisplayInfoID;
            iIventoryType = m_items[i]->GetProto()->InventoryType;

            _cfi =  iDisplayID | (iIventoryType << 24);
            corpse->SetUInt32Value(CORPSE_FIELD_ITEM + i, _cfi);
        }
    }

    // we not need saved corpses for BG/arenas
    if (!GetMap()->IsBattleGroundOrArena())
    {
        corpse->SaveToDB();
    }

    // register for player, but not show
    sObjectAccessor.AddCorpse(corpse);
    return corpse;
}

/**
 * @brief Converts an existing corpse into bones and persists the ghost state if needed.
 */
void Player::SpawnCorpseBones()
{
    if (sObjectAccessor.ConvertCorpseForPlayer(GetObjectGuid()))
        if (!GetSession()->PlayerLogoutWithSave())          // at logout we will already store the player
        {
            SaveToDB(); // prevent loading as ghost without corpse
        }
}

/**
 * @brief Gets the player's current corpse object.
 *
 * @return The player's corpse, or null if none exists.
 */
Corpse* Player::GetCorpse() const
{
    return sObjectAccessor.GetCorpseForPlayerGUID(GetObjectGuid());
}

/**
 * @brief Moves the player to the nearest valid graveyard.
 */
void Player::RepopAtGraveyard()
{
    // note: this can be called also when the player is alive
    // for example from WorldSession::HandleMovementOpcodes

    AreaTableEntry const* zone = GetAreaEntryByAreaID(GetAreaId());

    // Such zones are considered unreachable as a ghost and the player must be automatically revived
    if ((!IsAlive() && zone && zone->flags & AREA_FLAG_NEED_FLY) || GetTransport())
    {
        ResurrectPlayer(0.5f);
        SpawnCorpseBones();
    }

    WorldSafeLocsEntry const* ClosestGrave = NULL;

    // Special handle for battleground maps
    if (BattleGround* bg = GetBattleGround())
    {
        ClosestGrave = bg->GetClosestGraveYard(this);
    }
    else
    {
        ClosestGrave = sObjectMgr.GetClosestGraveYard(GetPositionX(), GetPositionY(), GetPositionZ(), GetMapId(), GetTeam());
    }

    // stop countdown until repop
    m_deathTimer = 0;

    // if no grave found, stay at the current location
    // and don't show spirit healer location
    if (ClosestGrave)
    {
        bool updateVisibility = IsInWorld() && GetMapId() == ClosestGrave->map_id;
        TeleportTo(ClosestGrave->map_id, ClosestGrave->x, ClosestGrave->y, ClosestGrave->z, GetOrientation());
        if (IsDead())                                       // not send if alive, because it used in TeleportTo()
        {
            WorldPacket data(SMSG_DEATH_RELEASE_LOC, 4 * 4);// show spirit healer position on minimap
            data << ClosestGrave->map_id;
            data << ClosestGrave->x;
            data << ClosestGrave->y;
            data << ClosestGrave->z;
            GetSession()->SendPacket(&data);
        }
        if (updateVisibility && IsInWorld())
        {
            UpdateVisibilityAndView();
        }
    }
}

/**
 * @brief Registers a joined chat channel on the player.
 *
 * @param c The channel that was joined.
 */
void Player::JoinedChannel(Channel* c)
{
    m_channels.push_back(c);
}

/**
 * @brief Removes a chat channel from the player's joined channel list.
 *
 * @param c The channel that was left.
 */
void Player::LeftChannel(Channel* c)
{
    m_channels.remove(c);
}

/**
 * @brief Leaves and cleans up all channels currently joined by the player.
 */
void Player::CleanupChannels()
{
    while (!m_channels.empty())
    {
        Channel* ch = *m_channels.begin();
        m_channels.erase(m_channels.begin());               // remove from player's channel list
        ch->Leave(this, false);                             // not send to client, not remove from player's channel list
        if (ChannelMgr* cMgr = channelMgr(GetTeam()))
        {
            cMgr->LeftChannel(ch->GetName()); // deleted channel if empty
        }
    }
    DEBUG_LOG("Player: channels cleaned up!");
}

/**
 * @brief Updates built-in local channels after a zone change.
 *
 * @param newZone The new area identifier used for localized channel names.
 */
void Player::UpdateLocalChannels(uint32 newZone)
{
    if (m_channels.empty())
    {
        return;
    }

    AreaTableEntry const* current_zone = GetAreaEntryByAreaID(newZone);
    if (!current_zone)
    {
        return;
    }

    ChannelMgr* cMgr = channelMgr(GetTeam());
    if (!cMgr)
    {
        return;
    }

    std::string current_zone_name = current_zone->area_name[GetSession()->GetSessionDbcLocale()];

    for (JoinedChannelsList::iterator i = m_channels.begin(), next; i != m_channels.end(); i = next)
    {
        next = i; ++next;

        // skip non built-in channels
        if (!(*i)->IsConstant())
        {
            continue;
        }

        ChatChannelsEntry const* ch = GetChannelEntryFor((*i)->GetChannelId());
        if (!ch)
        {
            continue;
        }

        if ((ch->flags & 4) == 4)                           // global channel without zone name in pattern
        {
            continue;
        }

        //  new channel
        char new_channel_name_buf[100];
        snprintf(new_channel_name_buf, 100, ch->pattern[m_session->GetSessionDbcLocale()], current_zone_name.c_str());
        Channel* new_channel = cMgr->GetJoinChannel(new_channel_name_buf, ch->ChannelID);

        if ((*i) != new_channel)
        {
            new_channel->Join(this, "");                    // will output Changed Channel: N. Name

            // leave old channel
            (*i)->Leave(this, false);                       // not send leave channel, it already replaced at client
            std::string name = (*i)->GetName();             // store name, (*i)erase in LeftChannel
            LeftChannel(*i);                                // remove from player's channel list
            cMgr->LeftChannel(name);                        // delete if empty
        }
    }
    DEBUG_LOG("Player: channels cleaned up!");
}

/**
 * @brief Leaves the currently joined looking-for-group channel, if any.
 */
void Player::LeaveLFGChannel()
{
    for (JoinedChannelsList::iterator i = m_channels.begin(); i != m_channels.end(); ++i)
    {
        if ((*i)->IsLFG())
        {
            (*i)->Leave(this);
            break;
        }
    }
}

/**
 * @brief Attempts to improve defense skill and refresh defense-derived bonuses.
 */
void Player::UpdateDefense()
{
    uint32 defense_skill_gain = sWorld.getConfig(CONFIG_UINT32_SKILL_GAIN_DEFENSE);

    if (UpdateSkill(SKILL_DEFENSE, defense_skill_gain))
    {
        // update dependent from defense skill part
        UpdateDefenseBonusesMod();
    }
}

/**
 * @brief Computes the player's current shield block value.
 *
 * @return The effective shield block amount.
 */
uint32 Player::GetShieldBlockValue() const
{
    float value = (m_auraBaseMod[SHIELD_BLOCK_VALUE][FLAT_MOD] + GetStat(STAT_STRENGTH) / 20 - 1) * m_auraBaseMod[SHIELD_BLOCK_VALUE][PCT_MOD];

    value = (value < 0) ? 0 : value;

    return uint32(value);
}

uint32 Player::GetMeleeCritDamageReduction(uint32 damage) const
{
    float melee  = GetRatingBonusValue(CR_CRIT_TAKEN_MELEE) * 2.0f;
    if (melee > 25.0f) melee = 25.0f;
    {
        return uint32(melee * damage / 100.0f);
    }
}

uint32 Player::GetRangedCritDamageReduction(uint32 damage) const
{
    float ranged = GetRatingBonusValue(CR_CRIT_TAKEN_RANGED) * 2.0f;
    if (ranged > 25.0f) ranged = 25.0f;
    {
        return uint32(ranged * damage / 100.0f);
    }
}

uint32 Player::GetSpellCritDamageReduction(uint32 damage) const
{
    float spell = GetRatingBonusValue(CR_CRIT_TAKEN_SPELL) * 2.0f;
    // In wow script resilience limited to 25%
    if (spell > 25.0f)
    {
        spell = 25.0f;
    }
    return uint32(spell * damage / 100.0f);
}

uint32 Player::GetDotDamageReduction(uint32 damage) const
{
    float spellDot = GetRatingBonusValue(CR_CRIT_TAKEN_SPELL);
    // Dot resilience not limited (limit it by 100%)
    if (spellDot > 100.0f)
    {
        spellDot = 100.0f;
    }
    return uint32(spellDot * damage / 100.0f);
}

/**
 * @brief Calculates health regeneration per spirit tick.
 *
 * @return The health regeneration value based on spirit and class.
 */
float Player::OCTRegenHPPerSpirit()
{
    uint32 level = getLevel();
    uint32 pclass = getClass();

    if (level > GT_MAX_LEVEL) level = GT_MAX_LEVEL;

    GtOCTRegenHPEntry     const* baseRatio = sGtOCTRegenHPStore.LookupEntry((pclass - 1) * GT_MAX_LEVEL + level - 1);
    GtRegenHPPerSptEntry  const* moreRatio = sGtRegenHPPerSptStore.LookupEntry((pclass - 1) * GT_MAX_LEVEL + level - 1);
    if (baseRatio == NULL || moreRatio == NULL)
    {
        return 0.0f;
    }

    // Formula from PaperDollFrame script
    float spirit = GetStat(STAT_SPIRIT);
    float baseSpirit = spirit;
    if (baseSpirit > 50)
    {
        baseSpirit = 50;
    }
    float moreSpirit = spirit - baseSpirit;
    float regen = baseSpirit * baseRatio->ratio + moreSpirit * moreRatio->ratio;
    return regen;
}

/**
 * @brief Attempts to improve the player's weapon skill for an attack type.
 *
 * @param attType The attack type whose weapon skill should be updated.
 */
void Player::UpdateWeaponSkill(WeaponAttackType attType)
{
    // no skill gain in pvp
    Unit* pVictim = getVictim();
    if (pVictim && pVictim->IsCharmerOrOwnerPlayerOrPlayerItself())
    {
        return;
    }

    if (IsInFeralForm())
    {
        return; // always maximized SKILL_FERAL_COMBAT in fact
    }

    if (GetShapeshiftForm() == FORM_TREE)
    {
        return; // use weapon but not skill up
    }

    uint32 weaponSkillGain = sWorld.getConfig(CONFIG_UINT32_SKILL_GAIN_WEAPON);

    Item* pWeapon = GetWeaponForAttack(attType, true, true);
    if (pWeapon && pWeapon->GetProto()->SubClass != ITEM_SUBCLASS_WEAPON_FISHING_POLE)
    {
        UpdateSkill(pWeapon->GetSkill(), weaponSkillGain);
    }
    else if (!pWeapon && attType == BASE_ATTACK)
    {
        UpdateSkill(SKILL_UNARMED, weaponSkillGain);
    }

    UpdateAllCritPercentages();
}

/**
 * @brief Attempts to improve weapon or defense skills from combat.
 *
 * @param pVictim The opposing unit involved in combat.
 * @param attType The attack type used for offensive skill checks.
 * @param defence True to evaluate defense gain; false for weapon skill gain.
 */
void Player::UpdateCombatSkills(Unit* pVictim, WeaponAttackType attType, bool defence)
{
    uint32 plevel = getLevel();                             // if defense than pVictim == attacker
    uint32 greylevel = MaNGOS::XP::GetGrayLevel(plevel);
    uint32 moblevel = pVictim->GetLevelForTarget(this);
    if (moblevel < greylevel)
    {
        return;
    }

    if (moblevel > plevel + 5)
    {
        moblevel = plevel + 5;
    }

    uint32 lvldif = moblevel - greylevel;
    if (lvldif < 3)
    {
        lvldif = 3;
    }

    int32 skilldif = 5 * plevel - (defence ? GetBaseDefenseSkillValue() : GetBaseWeaponSkillValue(attType));

    // Max skill reached for level.
    // Can in some cases be less than 0: having max skill and then .level -1 as example.
    if (skilldif <= 0)
    {
        return;
    }

    float chance = float(3 * lvldif * skilldif) / plevel;
    if (!defence)
    {
        chance *= 0.1f * GetStat(STAT_INTELLECT);
    }

    chance = chance < 1.0f ? 1.0f : chance;                 // minimum chance to increase skill is 1%

    if (roll_chance_f(chance))
    {
        if (defence)
        {
            UpdateDefense();
        }
        else
        {
            UpdateWeaponSkill(attType);
        }
    }
    else
    {
        return;
    }
}

/* Called from Player::SendInitialPacketsBeforeAddToMap */
/**
 * @brief Sends the player's initial action bar state to the client.
 */
void Player::SendInitialActionButtons() const
{
    DETAIL_LOG("Initializing Action Buttons for '%u'", GetGUIDLow());

    /* Initiate packet with size 4 bytes per action button */
    WorldPacket data(SMSG_ACTION_BUTTONS, (MAX_ACTION_BUTTONS * 4));

    /* For each possible action button the player could have */
    for (uint8 button = 0; button < MAX_ACTION_BUTTONS; ++button)
    {
        /* Try and get each action button the player could have */
        ActionButtonList::const_iterator itr = m_actionButtons.find(button);

        /* If the button is valid and not deleted */
        if (itr != m_actionButtons.end() && itr->second.uState != ACTIONBUTTON_DELETED)
        {
            /* Send the data */
            data << uint32(itr->second.packedData);
        }
        else
        {
            /* Nothing to send, so just send 0 */
            data << uint32(0);
        }
    }

    GetSession()->SendPacket(&data);
    DETAIL_LOG("Action Buttons for '%u' Initialized", GetGUIDLow());
}

/**
 * @brief Validates action bar data before it is stored or loaded.
 *
 * @param button The action bar button index.
 * @param action The action identifier assigned to the button.
 * @param type The action button type.
 * @param player The player being validated for, or null for template data.
 * @return True if the action button data is valid; otherwise, false.
 */
bool Player::IsActionButtonDataValid(uint8 button, uint32 action, uint8 type, Player* player)
{
    if (button >= MAX_ACTION_BUTTONS)
    {
        if (player)
        {
            sLog.outError("Action %u not added into button %u for player %s: button must be < %u", action, button, player->GetName(), MAX_ACTION_BUTTONS);
        }
        else
        {
            sLog.outError("Table `playercreateinfo_action` have action %u into button %u : button must be < %u", action, button, MAX_ACTION_BUTTONS);
        }
        return false;
    }

    if (action >= MAX_ACTION_BUTTON_ACTION_VALUE)
    {
        if (player)
        {
            sLog.outError("Action %u not added into button %u for player %s: action must be < %u", action, button, player->GetName(), MAX_ACTION_BUTTON_ACTION_VALUE);
        }
        else
        {
            sLog.outError("Table `playercreateinfo_action` have action %u into button %u : action must be < %u", action, button, MAX_ACTION_BUTTON_ACTION_VALUE);
        }
        return false;
    }

    switch (type)
    {
        case ACTION_BUTTON_SPELL:
        {
            SpellEntry const* spellProto = sSpellStore.LookupEntry(action);
            if (!spellProto)
            {
                if (player)
                {
                    sLog.outError("Spell action %u not added into button %u for player %s: spell not exist", action, button, player->GetName());
                }
                else
                {
                    sLog.outError("Table `playercreateinfo_action` have spell action %u into button %u: spell not exist", action, button);
                }
                return false;
            }

            if (player)
            {
                if (!player->HasSpell(spellProto->Id))
                {
                    sLog.outError("Spell action %u not added into button %u for player %s: player don't known this spell", action, button, player->GetName());
                    return false;
                }
                else if (IsPassiveSpell(spellProto))
                {
                    sLog.outError("Spell action %u not added into button %u for player %s: spell is passive", action, button, player->GetName());
                    return false;
                }
            }
            break;
        }
        case ACTION_BUTTON_ITEM:
        {
            if (!ObjectMgr::GetItemPrototype(action))
            {
                if (player)
                {
                    sLog.outError("Item action %u not added into button %u for player %s: item not exist", action, button, player->GetName());
                }
                else
                {
                    sLog.outError("Table `playercreateinfo_action` have item action %u into button %u: item not exist", action, button);
                }
                return false;
            }
            break;
        }
        default:
            break;                                          // other cases not checked at this moment
    }

    return true;
}

/**
 * @brief Adds or updates an action bar button entry.
 *
 * @param button The action bar button index.
 * @param action The action identifier to bind.
 * @param type The action button type.
 * @return The updated action button, or null if validation fails.
 */
ActionButton* Player::addActionButton(uint8 button, uint32 action, uint8 type)
{
    if (!IsActionButtonDataValid(button, action, type, this))
    {
        return NULL;
    }

    // it create new button (NEW state) if need or return existing
    ActionButton& ab = m_actionButtons[button];

    // set data and update to CHANGED if not NEW
    ab.SetActionAndType(action, ActionButtonType(type));

    DETAIL_LOG("Player '%u' Added Action '%u' (type %u) to Button '%u'", GetGUIDLow(), action, uint32(type), button);
    return &ab;
}

/**
 * @brief Removes an action bar button entry.
 *
 * @param button The action bar button index to remove.
 */
void Player::removeActionButton(uint8 button)
{
    ActionButtonList::iterator buttonItr = m_actionButtons.find(button);
    if (buttonItr == m_actionButtons.end())
    {
        return;
    }

    if (buttonItr->second.uState == ACTIONBUTTON_NEW)
    {
        m_actionButtons.erase(buttonItr); // new and not saved
    }
    else
    {
        buttonItr->second.uState = ACTIONBUTTON_DELETED; // saved, will deleted at next save
    }

    DETAIL_LOG("Action Button '%u' Removed from Player '%u'", button, GetGUIDLow());
}

/**
 * @brief Moves the player to a new position and updates related state.
 *
 * @param x The destination X coordinate.
 * @param y The destination Y coordinate.
 * @param z The destination Z coordinate.
 * @param orientation The destination facing angle.
 * @param teleport True if the move should be treated as a teleport.
 * @return True if the position update succeeded; otherwise, false.
 */
bool Player::SetPosition(float x, float y, float z, float orientation, bool teleport)
{
    // prevent crash when a bad coord is sent by the client
    if (!MaNGOS::IsValidMapCoord(x, y, z, orientation))
    {
        DEBUG_LOG("Player::SetPosition(%f, %f, %f, %f, %d) .. bad coordinates for player %d!", x, y, z, orientation, teleport, GetGUIDLow());
        return false;
    }

    Map* m = GetMap();

    const float old_x = GetPositionX();
    const float old_y = GetPositionY();
    const float old_z = GetPositionZ();
    const float old_r = GetOrientation();

    if (teleport || old_x != x || old_y != y || old_z != z || old_r != orientation)
    {
        if (teleport || old_x != x || old_y != y || old_z != z)
        {
            RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_MOVE | AURA_INTERRUPT_FLAG_TURNING);
        }
        else
        {
            RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_TURNING);
        }

        RemoveSpellsCausingAura(SPELL_AURA_FEIGN_DEATH);

        // move and update visible state if need
        m->PlayerRelocation(this, x, y, z, orientation);

        // reread after Map::Relocation
        m = GetMap();
        x = GetPositionX();
        y = GetPositionY();
        z = GetPositionZ();

        // group update
        if (GetGroup() && (old_x != x || old_y != y))
        {
            SetGroupUpdateFlag(GROUP_UPDATE_FLAG_POSITION);
        }
        if (GetTrader() && !IsWithinDistInMap(GetTrader(), INTERACTION_DISTANCE))
        {
            GetSession()->SendCancelTrade(); // will close both side trade windows
        }
    }

    if (m_positionStatusUpdateTimer)                        // Update position's state only on interval
    {
        return true;
    }
    m_positionStatusUpdateTimer = 100;

    // code block for underwater state update
    UpdateUnderwaterState(m, x, y, z);

    // code block for outdoor state and area-explore check
    CheckAreaExploreAndOutdoor();

    return true;
}

/**
 * @brief Saves the player's current position as the recall location.
 */
void Player::SaveRecallPosition()
{
    m_recallMap = GetMapId();
    m_recallX = GetPositionX();
    m_recallY = GetPositionY();
    m_recallZ = GetPositionZ();
    m_recallO = GetOrientation();
}

/**
 * @brief Broadcasts a packet to nearby clients and optionally to the player.
 *
 * @param data The packet to send.
 * @param self True to also send the packet to the player session.
 */
void Player::SendMessageToSet(WorldPacket* data, bool self) const
{
    if (IsInWorld())
    {
        GetMap()->MessageBroadcast(this, data, false);
    }

    // if player is not in world and map in not created/already destroyed
    // no need to create one, just send packet for itself!
    if (self)
    {
        GetSession()->SendPacket(data);
    }
}

/**
 * @brief Broadcasts a packet to nearby clients within a distance and optionally to the player.
 *
 * @param data The packet to send.
 * @param dist The maximum broadcast distance.
 * @param self True to also send the packet to the player session.
 */
void Player::SendMessageToSetInRange(WorldPacket* data, float dist, bool self) const
{
    if (IsInWorld())
    {
        GetMap()->MessageDistBroadcast(this, data, dist, false);
    }

    if (self)
    {
        GetSession()->SendPacket(data);
    }
}

/**
 * @brief Broadcasts a packet within range with optional team filtering.
 *
 * @param data The packet to send.
 * @param dist The maximum broadcast distance.
 * @param self True to also send the packet to the player session.
 * @param own_team_only True to restrict delivery to the player's team.
 */
void Player::SendMessageToSetInRange(WorldPacket* data, float dist, bool self, bool own_team_only) const
{
    if (IsInWorld())
    {
        GetMap()->MessageDistBroadcast(this, data, dist, false, own_team_only);
    }

    if (self)
    {
        GetSession()->SendPacket(data);
    }
}

/**
 * @brief Sends a packet directly to the player's session.
 *
 * @param data The packet to send.
 */
void Player::SendDirectMessage(WorldPacket* data) const
{
    GetSession()->SendPacket(data);
}

/**
 * @brief Starts a cinematic sequence for the player client.
 *
 * @param CinematicSequenceId The cinematic sequence identifier.
 */
void Player::SendCinematicStart(uint32 CinematicSequenceId)
{
    WorldPacket data(SMSG_TRIGGER_CINEMATIC, 4);
    data << uint32(CinematicSequenceId);
    SendDirectMessage(&data);
}

#if defined (WOTLK) || defined (CATA) || defined (MISTS)
/**
 * @brief Starts a movie sequence for the player client.
 *
 * @param MovieId The movie identifier.
 */
void Player::SendMovieStart(uint32 MovieId)
{
    WorldPacket data(SMSG_TRIGGER_MOVIE, 4);
    data << uint32(MovieId);
    SendDirectMessage(&data);
}
#endif

/**
 * @brief Updates outdoor-only effects and exploration discovery for the current position.
 */
void Player::CheckAreaExploreAndOutdoor()
{
    if (!IsAlive())
    {
        return;
    }

    if (IsTaxiFlying())
    {
        return;
    }

    bool isOutdoor;
    uint16 areaFlag = GetTerrain()->GetAreaFlag(GetPositionX(), GetPositionY(), GetPositionZ(), &isOutdoor);

    if (isOutdoor)
    {
        if (HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_RESTING) && GetRestType() == REST_TYPE_IN_TAVERN)
        {
            AreaTriggerEntry const* at = sAreaTriggerStore.LookupEntry(inn_trigger_id);
            if (!at || !IsPointInAreaTriggerZone(at, GetMapId(), GetPositionX(), GetPositionY(), GetPositionZ()))
            {
                // Player left inn (REST_TYPE_IN_CITY overrides REST_TYPE_IN_TAVERN, so just clear rest)
                SetRestType(REST_TYPE_NO);
            }
        }
        // Check if we need to reaply outdoor only passive spells
        const PlayerSpellMap& sp_list = GetSpellMap();
        for (PlayerSpellMap::const_iterator itr = sp_list.begin(); itr != sp_list.end(); ++itr)
        {
            if (itr->second.state == PLAYERSPELL_REMOVED)
            {
                continue;
            }
            SpellEntry const* spellInfo = sSpellStore.LookupEntry(itr->first);
            if (!spellInfo || !IsNeedCastSpellAtOutdoor(spellInfo) || HasAura(itr->first))
            {
                continue;
            }
            if ((spellInfo->Stances || spellInfo->StancesNot) && !IsNeedCastSpellAtFormApply(spellInfo, GetShapeshiftForm()))
            {
                continue;
            }
            CastSpell(this, itr->first, true, NULL);
        }
    }
    else if (sWorld.getConfig(CONFIG_BOOL_VMAP_INDOOR_CHECK) && !isGameMaster())
    {
        RemoveAurasWithAttribute(SPELL_ATTR_OUTDOORS_ONLY);
    }

    if (areaFlag == 0xffff)
    {
        return;
    }
    int offset = areaFlag / 32;

    if (offset >= PLAYER_EXPLORED_ZONES_SIZE)
    {
        sLog.outError("Wrong area flag %u in map data for (X: %f Y: %f) point to field PLAYER_EXPLORED_ZONES_1 + %u ( %u must be < %u ).", areaFlag, GetPositionX(), GetPositionY(), offset, offset, PLAYER_EXPLORED_ZONES_SIZE);
        return;
    }

    uint32 val = (uint32)(1 << (areaFlag % 32));
    uint32 currFields = GetUInt32Value(PLAYER_EXPLORED_ZONES_1 + offset);

    if (!(currFields & val))
    {
        SetUInt32Value(PLAYER_EXPLORED_ZONES_1 + offset, (uint32)(currFields | val));

        AreaTableEntry const* p = GetAreaEntryByAreaFlagAndMap(areaFlag, GetMapId());
        if (!p)
        {
            sLog.outError("PLAYER: Player %u discovered unknown area (x: %f y: %f map: %u", GetGUIDLow(), GetPositionX(), GetPositionY(), GetMapId());
        }
        else if (p->area_level > 0)
        {
            uint32 area = p->ID;
            if (getLevel() >= sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL))
            {
                SendExplorationExperience(area, 0);
            }
            else
            {
                int32 diff = int32(getLevel()) - p->area_level;
                uint32 XP = 0;
                if (diff < -5)
                {
                    XP = uint32(sObjectMgr.GetBaseXP(getLevel() + 5) * sWorld.getConfig(CONFIG_FLOAT_RATE_XP_EXPLORE));
                }
                else if (diff > 5)
                {
                    int32 exploration_percent = (100 - ((diff - 5) * 5));
                    if (exploration_percent > 100)
                    {
                        exploration_percent = 100;
                    }
                    else if (exploration_percent < 0)
                    {
                        exploration_percent = 0;
                    }

                    XP = uint32(sObjectMgr.GetBaseXP(p->area_level) * exploration_percent / 100 * sWorld.getConfig(CONFIG_FLOAT_RATE_XP_EXPLORE));
                }
                else
                {
                    XP = uint32(sObjectMgr.GetBaseXP(p->area_level) * sWorld.getConfig(CONFIG_FLOAT_RATE_XP_EXPLORE));
                }

                GiveXP(XP, NULL);
                SendExplorationExperience(area, XP);
            }
            DETAIL_LOG("PLAYER: Player %u discovered a new area: %u", GetGUIDLow(), area);
        }
    }
}

/**
 * @brief Gets the faction team associated with a race.
 *
 * @param race The race identifier to evaluate.
 * @return The team assigned to the race.
 */
Team Player::TeamForRace(uint8 race)
{
    ChrRacesEntry const* rEntry = sChrRacesStore.LookupEntry(race);
    if (!rEntry)
    {
        sLog.outError("Race %u not found in DBC: wrong DBC files?", uint32(race));
        return ALLIANCE;
    }

    switch (rEntry->TeamID)
    {
        case 7: return ALLIANCE;
        case 1: return HORDE;
    }

    sLog.outError("Race %u have wrong teamid %u in DBC: wrong DBC files?", uint32(race), rEntry->TeamID);
    return TEAM_NONE;
}

/**
 * @brief Gets the faction template associated with a race.
 *
 * @param race The race identifier to evaluate.
 * @return The faction template identifier for the race.
 */
uint32 Player::getFactionForRace(uint8 race)
{
    ChrRacesEntry const* rEntry = sChrRacesStore.LookupEntry(race);
    if (!rEntry)
    {
        sLog.outError("Race %u not found in DBC: wrong DBC files?", uint32(race));
        return 0;
    }

    return rEntry->FactionID;
}

/**
 * @brief Sets the player's team and faction from the specified race.
 *
 * @param race The race identifier to apply.
 */
void Player::setFactionForRace(uint8 race)
{
    m_team = TeamForRace(race);
    setFaction(getFactionForRace(race));
}

void Player::UpdateArenaFields(void)
{
    /* arena calcs go here */
}

void Player::UpdateHonorFields()
{
    /// called when rewarding honor and at each save
    time_t now = time(NULL);
    time_t today = (time(NULL) / DAY) * DAY;

    if (m_lastHonorUpdateTime < today)
    {
        time_t yesterday = today - DAY;

        uint16 kills_today = PAIR32_LOPART(GetUInt32Value(PLAYER_FIELD_KILLS));

        // update yesterday's contribution
        if (m_lastHonorUpdateTime >= yesterday)
        {
            SetUInt32Value(PLAYER_FIELD_YESTERDAY_CONTRIBUTION, GetUInt32Value(PLAYER_FIELD_TODAY_CONTRIBUTION));

            // this is the first update today, reset today's contribution
            SetUInt32Value(PLAYER_FIELD_TODAY_CONTRIBUTION, 0);
            SetUInt32Value(PLAYER_FIELD_KILLS, MAKE_PAIR32(0, kills_today));
        }
        else
        {
            // no honor/kills yesterday or today, reset
            SetUInt32Value(PLAYER_FIELD_YESTERDAY_CONTRIBUTION, 0);
            SetUInt32Value(PLAYER_FIELD_KILLS, 0);
        }
    }

    m_lastHonorUpdateTime = now;
}

/// Calculate the amount of honor gained based on the victim
/// and the size of the group for which the honor is divided
/// An exact honor value can also be given (overriding the calcs)
bool Player::RewardHonor(Unit* uVictim, uint32 groupsize, float honor)
{
    // do not reward honor in arenas, but enable onkill spellproc
    if (InArena())
    {
        if (!uVictim || uVictim == this || uVictim->GetTypeId() != TYPEID_PLAYER)
        {
            return false;
        }

        if (GetBGTeam() == ((Player*)uVictim)->GetBGTeam())
        {
            return false;
        }

        return true;
    }

    // 'Inactive' this aura prevents the player from gaining honor points and battleground tokens
    if (GetDummyAura(SPELL_AURA_PLAYER_INACTIVE))
    {
        return false;
    }

    ObjectGuid victim_guid;
    uint32 victim_rank = 0;

    // need call before fields update to have chance move yesterday data to appropriate fields before today data change.
    UpdateHonorFields();

    if (honor <= 0)
    {
        if (!uVictim || uVictim == this || uVictim->HasAuraType(SPELL_AURA_NO_PVP_CREDIT))
        {
            return false;
        }

        victim_guid = uVictim->GetObjectGuid();

        if (uVictim->GetTypeId() == TYPEID_PLAYER)
        {
            Player* pVictim = (Player*)uVictim;

            if (GetTeam() == pVictim->GetTeam() && !sWorld.IsFFAPvPRealm())
            {
                return false;
            }

            float f = 1;                                    // need for total kills (?? need more info)
            uint32 k_grey = 0;
            uint32 k_level = getLevel();
            uint32 v_level = pVictim->getLevel();

            {
                // PLAYER_CHOSEN_TITLE VALUES DESCRIPTION
                //  [0]      Just name
                //  [1..14]  Alliance honor titles and player name
                //  [15..28] Horde honor titles and player name
                //  [29..38] Other title and player name
                //  [39+]    Nothing
                uint32 victim_title = pVictim->GetUInt32Value(PLAYER_CHOSEN_TITLE);
                // Get Killer titles, CharTitlesEntry::bit_index
                // Ranks:
                //  title[1..14]  -> rank[5..18]
                //  title[15..28] -> rank[5..18]
                //  title[other]  -> 0
                if (victim_title == 0)
                {
                    victim_guid.Clear();                    // Don't show HK: <rank> message, only log.
                }
                else if (victim_title < 15)
                {
                    victim_rank = victim_title + 4;
                }
                else if (victim_title < 29)
                {
                    victim_rank = victim_title - 14 + 4;
                }
                else
                {
                    victim_guid.Clear();                    // Don't show HK: <rank> message, only log.
                }
            }

            k_grey = MaNGOS::XP::GetGrayLevel(k_level);

            if (v_level <= k_grey)
            {
                return false;
            }

            float diff_level = (k_level == k_grey) ? 1 : ((float(v_level) - float(k_grey)) / (float(k_level) - float(k_grey)));

            int32 v_rank = 1;                               // need more info

            honor = ((f * diff_level * (190 + v_rank * 10)) / 6);
            honor *= float(k_level) / 70.0f;                // factor of dependence on levels of the killer

            // count the number of playerkills in one day
            ApplyModUInt32Value(PLAYER_FIELD_KILLS, 1, true);
            // and those in a lifetime
            ApplyModUInt32Value(PLAYER_FIELD_LIFETIME_HONORABLE_KILLS, 1, true);
        }
        else
        {
            Creature* cVictim = (Creature*)uVictim;

            if (!cVictim->IsRacialLeader())
            {
                return false;
            }

            honor = 100;                                    // ??? need more info
            victim_rank = 19;                               // HK: Leader
        }
    }

    if (uVictim != NULL)
    {
        honor *= sWorld.getConfig(CONFIG_FLOAT_RATE_HONOR);

        if (groupsize > 1)
        {
            honor /= groupsize;
        }

        honor *= (((float)urand(8, 12)) / 10);              // approx honor: 80% - 120% of real honor
    }

    // honor - for show honor points in log
    // victim_guid - for show victim name in log
    // victim_rank [1..4]  HK: <dishonored rank>
    // victim_rank [5..19] HK: <alliance\horde rank>
    // victim_rank [0,20+] HK: <>
    WorldPacket data(SMSG_PVP_CREDIT, 4 + 8 + 4);
    data << uint32(honor);
    data << ObjectGuid(victim_guid);
    data << uint32(victim_rank);
    GetSession()->SendPacket(&data);

    // add honor points
    ModifyHonorPoints(int32(honor));

    ApplyModUInt32Value(PLAYER_FIELD_TODAY_CONTRIBUTION, uint32(honor), true);
    return true;
}

void Player::SetHonorPoints(uint32 value)
{
    if (value > sWorld.getConfig(CONFIG_UINT32_MAX_HONOR_POINTS))
    {
        value = sWorld.getConfig(CONFIG_UINT32_MAX_HONOR_POINTS);
    }

    SetUInt32Value(PLAYER_FIELD_HONOR_CURRENCY, value);
}

void Player::SetArenaPoints(uint32 value)
{
    if (value > sWorld.getConfig(CONFIG_UINT32_MAX_ARENA_POINTS))
    {
        value = sWorld.getConfig(CONFIG_UINT32_MAX_ARENA_POINTS);
    }

    SetUInt32Value(PLAYER_FIELD_ARENA_CURRENCY, value);
}

void Player::ModifyHonorPoints(int32 value)
{
    int32 newValue = (int32)GetHonorPoints() + value;

    if (newValue < 0)
    {
        newValue = 0;
    }

    SetHonorPoints(newValue);
}

void Player::ModifyArenaPoints(int32 value)
{
    int32 newValue = (int32)GetArenaPoints() + value;

    if (newValue < 0)
    {
        newValue = 0;
    }

    SetArenaPoints(newValue);
}

/**
 * @brief Loads a player's guild identifier from the database.
 *
 * @param guid The player GUID to query.
 * @return The guild identifier, or zero if none is found.
 */
uint32 Player::GetGuildIdFromDB(ObjectGuid guid)
{
    uint32 lowguid = guid.GetCounter();

    QueryResult* result = CharacterDatabase.PQuery("SELECT `guildid` FROM `guild_member` WHERE `guid`='%u'", lowguid);
    if (!result)
    {
        return 0;
    }

    uint32 id = result->Fetch()[0].GetUInt32();
    delete result;
    return id;
}

/**
 * @brief Loads a player's guild rank from the database.
 *
 * @param guid The player GUID to query.
 * @return The guild rank, or zero if none is found.
 */
uint32 Player::GetRankFromDB(ObjectGuid guid)
{
    QueryResult* result = CharacterDatabase.PQuery("SELECT `rank` FROM `guild_member` WHERE `guid`='%u'", guid.GetCounter());
    if (result)
    {
        uint32 v = result->Fetch()[0].GetUInt32();
        delete result;
        return v;
    }
    else
    {
        return 0;
    }
}

uint32 Player::GetArenaTeamIdFromDB(ObjectGuid guid, ArenaType type)
{
    QueryResult* result = CharacterDatabase.PQuery("SELECT `arena_team_member`.`arenateamid` FROM `arena_team_member` JOIN `arena_team` ON `arena_team_member`.`arenateamid` = `arena_team`.`arenateamid` WHERE `guid`='%u' AND `type`='%u' LIMIT 1", guid.GetCounter(), type);
    if (!result)
    {
        return 0;
    }

    uint32 id = (*result)[0].GetUInt32();
    delete result;
    return id;
}

/**
 * @brief Loads or derives a player's saved zone identifier from the database.
 *
 * @param guid The player GUID to query.
 * @return The resolved zone identifier, or zero on failure.
 */
uint32 Player::GetZoneIdFromDB(ObjectGuid guid)
{
    uint32 lowguid = guid.GetCounter();
    QueryResult* result = CharacterDatabase.PQuery("SELECT `zone` FROM `characters` WHERE `guid`='%u'", lowguid);
    if (!result)
    {
        return 0;
    }
    Field* fields = result->Fetch();
    uint32 zone = fields[0].GetUInt32();
    delete result;

    if (!zone)
    {
        // stored zone is zero, use generic and slow zone detection
        result = CharacterDatabase.PQuery("SELECT `map`,`position_x`,`position_y`,`position_z` FROM `characters` WHERE `guid`='%u'", lowguid);
        if (!result)
        {
            return 0;
        }
        fields = result->Fetch();
        uint32 map = fields[0].GetUInt32();
        float posx = fields[1].GetFloat();
        float posy = fields[2].GetFloat();
        float posz = fields[3].GetFloat();
        delete result;

        zone = sTerrainMgr.GetZoneId(map, posx, posy, posz);

        if (zone > 0)
        {
            CharacterDatabase.PExecute("UPDATE `characters` SET `zone`='%u' WHERE `guid`='%u'", zone, lowguid);
        }
    }

    return zone;
}

/**
 * @brief Loads a player's level from the database.
 *
 * @param guid The player GUID to query.
 * @return The stored level, or zero on failure.
 */
uint32 Player::GetLevelFromDB(ObjectGuid guid)
{
    uint32 lowguid = guid.GetCounter();

    QueryResult* result = CharacterDatabase.PQuery("SELECT `level` FROM `characters` WHERE `guid`='%u'", lowguid);
    if (!result)
    {
        return 0;
    }

    Field* fields = result->Fetch();
    uint32 level = fields[0].GetUInt32();
    delete result;

    return level;
}

/**
 * @brief Updates area-specific player state and auras.
 *
 * @param newArea The new area identifier.
 */
void Player::UpdateArea(uint32 newArea)
{
    m_areaUpdateId    = newArea;

    AreaTableEntry const* area = GetAreaEntryByAreaID(newArea);

    // FFA_PVP flags are area and not zone id dependent
    // so apply them accordingly
    if (area && (area->flags & AREA_FLAG_ARENA))
    {
        if (!isGameMaster())
        {
            SetFFAPvP(true);
        }
    }
    else
    {
        // remove ffa flag only if not ffapvp realm
        // removal in sanctuaries and capitals is handled in zone update
        if (IsFFAPvP() && !sWorld.IsFFAPvPRealm())
        {
            SetFFAPvP(false);
        }
    }

    UpdateAreaDependentAuras();
}

/**
 * @brief Checks whether the player is eligible to interact with a capture point.
 *
 * @return True if the player can use the capture point; otherwise, false.
 */
bool Player::CanUseCapturePoint()
{
    return IsAlive() &&                                     // living
           !HasStealthAura() &&                             // not stealthed
           !HasInvisibilityAura() &&                        // visible
           (IsPvP() || sWorld.IsPvPRealm()) &&
           !HasMovementFlag(MOVEFLAG_FLYING) &&
           !IsTaxiFlying() &&
           !isGameMaster();
}

/**
 * @brief Updates zone and area state after the player changes location.
 *
 * @param newZone The new zone identifier.
 * @param newArea The new area identifier.
 */
void Player::UpdateZone(uint32 newZone, uint32 newArea)
{
    /* If we're trying to update into a zone that doesn't exist, just return */
    AreaTableEntry const* zone = GetAreaEntryByAreaID(newZone);
    if (!zone)
    {
        return;
    }

    /* If we're moving into a different zone */
    if (m_zoneUpdateId != newZone)
    {
        // handle outdoor pvp zones
        sOutdoorPvPMgr.HandlePlayerLeaveZone(this, m_zoneUpdateId);
        sOutdoorPvPMgr.HandlePlayerEnterZone(this, newZone);

        SendInitWorldStates(newZone, newArea);              // only if really enters to new zone, not just area change, works strange...

        if (sWorld.getConfig(CONFIG_BOOL_WEATHER))
        {
            Weather* wth = GetMap()->GetWeatherSystem()->FindOrCreateWeather(newZone);
            wth->SendWeatherUpdateToPlayer(this);
        }
    }

    // Used by Eluna
#ifdef ENABLE_ELUNA
    if (Eluna* e = GetEluna())
    {
        e->OnUpdateZone(this, newZone, newArea);
    }
#endif /* ENABLE_ELUNA */

    m_zoneUpdateId    = newZone;
    m_zoneUpdateTimer = ZONE_UPDATE_INTERVAL;

    // zone changed, so area changed as well, update it
    UpdateArea(newArea);

    // in PvP, any not controlled zone (except zone->team == 6, default case)
    // in PvE, only opposition team capital
    switch (zone->team)
    {
        case AREATEAM_ALLY:
            pvpInfo.inHostileArea = GetTeam() != ALLIANCE && (sWorld.IsPvPRealm() || zone->flags & AREA_FLAG_CAPITAL);
            break;
        case AREATEAM_HORDE:
            pvpInfo.inHostileArea = GetTeam() != HORDE && (sWorld.IsPvPRealm() || zone->flags & AREA_FLAG_CAPITAL);
            break;
        case AREATEAM_NONE:
            // overwrite for battlegrounds, maybe batter some zone flags but current known not 100% fit to this
            pvpInfo.inHostileArea = sWorld.IsPvPRealm() || InBattleGround();
            break;
        default:                                            // 6 in fact
            pvpInfo.inHostileArea = false;
            break;
    }

    if (pvpInfo.inHostileArea)                              // in hostile area
    {
        if (!IsPvP() || pvpInfo.endTimer != 0)
        {
            UpdatePvP(true, true);
        }
    }
    else                                                    // in friendly area
    {
        if (IsPvP() && !HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_IN_PVP) && pvpInfo.endTimer == 0)
        {
            pvpInfo.endTimer = time(0); // start toggle-off
        }
    }

    if (zone->flags & AREA_FLAG_SANCTUARY)                  // in sanctuary
    {
        SetFlag(PLAYER_FLAGS, PLAYER_FLAGS_SANCTUARY);
        if (sWorld.IsFFAPvPRealm())
        {
            SetFFAPvP(false);
        }
    }
    else
    {
        RemoveFlag(PLAYER_FLAGS, PLAYER_FLAGS_SANCTUARY);
    }

    if (zone->flags & AREA_FLAG_CAPITAL)                    // in capital city
    {
        SetRestType(REST_TYPE_IN_CITY);
    }
    else if (HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_RESTING) && GetRestType() != REST_TYPE_IN_TAVERN)
    {
        // resting and not in tavern (leave city then); tavern leave handled in CheckAreaExploreAndOutdoor
        SetRestType(REST_TYPE_NO);
    }

    // remove items with area/map limitations (delete only for alive player to allow back in ghost mode)
    // if player resurrected at teleport this will be applied in resurrect code
    if (IsAlive())
    {
        DestroyZoneLimitedItem(true, newZone);
    }

    // check some item equip limitations (in result lost CanTitanGrip at talent reset, for example)
    AutoUnequipOffhandIfNeed();

    // recent client version not send leave/join channel packets for built-in local channels
    UpdateLocalChannels(newZone);

    // group update
    if (GetGroup())
    {
        SetGroupUpdateFlag(GROUP_UPDATE_FLAG_ZONE);
    }

    UpdateZoneDependentAuras();
    UpdateZoneDependentPets();
}

// If players are too far way of duel flag... then player loose the duel
void Player::CheckDuelDistance(time_t currTime)
{
    if (!duel)
    {
        return;
    }

    GameObject* obj = GetMap()->GetGameObject(GetGuidValue(PLAYER_DUEL_ARBITER));
    if (!obj)
    {
        // player not at duel start map
        DuelComplete(DUEL_FLED);
        return;
    }

    if (duel->outOfBound == 0)
    {
        if (!IsWithinDistInMap(obj, 50))
        {
            duel->outOfBound = currTime;

            WorldPacket data(SMSG_DUEL_OUTOFBOUNDS, 0);
            GetSession()->SendPacket(&data);
        }
    }
    else
    {
        if (IsWithinDistInMap(obj, 40))
        {
            duel->outOfBound = 0;

            WorldPacket data(SMSG_DUEL_INBOUNDS, 0);
            GetSession()->SendPacket(&data);
        }
        else if (currTime >= (duel->outOfBound + 10))
        {
            DuelComplete(DUEL_FLED);
        }
    }
}

/**
 * @brief Finalizes an active duel and cleans up all duel state.
 *
 * @param type The duel completion result.
 */
void Player::DuelComplete(DuelCompleteType type)
{
    // duel not requested
    if (!duel)
    {
        return;
    }

    WorldPacket data(SMSG_DUEL_COMPLETE, (1));
    data << (uint8)((type != DUEL_INTERRUPTED) ? 1 : 0);
    GetSession()->SendPacket(&data);

    if (duel->opponent->GetSession())
    {
        duel->opponent->GetSession()->SendPacket(&data);
    }

    if (type != DUEL_INTERRUPTED)
    {
        data.Initialize(SMSG_DUEL_WINNER, (1 + 20));          // we guess size
        data << (uint8)((type == DUEL_WON) ? 0 : 1);          // 0 = just won; 1 = fled
        data << duel->opponent->GetName();
        data << GetName();
        SendMessageToSet(&data, true);
    }

    // Used by Eluna
#ifdef ENABLE_ELUNA
    if (Eluna* e = GetEluna())
    {
        e->OnDuelEnd(duel->opponent, this, type);
    }
#endif /* ENABLE_ELUNA */

    // Remove Duel Flag object
    if (GameObject* obj = GetMap()->GetGameObject(GetGuidValue(PLAYER_DUEL_ARBITER)))
    {
        duel->initiator->RemoveGameObject(obj, true);
    }

    /* remove auras */
    // TODO: Needs a simpler method
    std::vector<uint32> auras2remove;
    SpellAuraHolderMap const& vAuras = duel->opponent->GetSpellAuraHolderMap();
    for (SpellAuraHolderMap::const_iterator i = vAuras.begin(); i != vAuras.end(); ++i)
    {
        SpellAuraHolder const* aura = i->second;
        if (!aura->IsPositive() && aura->GetCasterGuid() == GetObjectGuid() && aura->GetAuraApplyTime() >= duel->startTime)
        {
            auras2remove.push_back(aura->GetId());
        }
    }

    for (size_t i = 0; i < auras2remove.size(); ++i)
    {
        duel->opponent->RemoveAurasDueToSpell(auras2remove[i]);
    }

    auras2remove.clear();
    SpellAuraHolderMap const& auras = GetSpellAuraHolderMap();
    for (SpellAuraHolderMap::const_iterator i = auras.begin(); i != auras.end(); ++i)
    {
        SpellAuraHolder const* aura = i->second;
        if (!aura->IsPositive() && aura->GetCasterGuid() == duel->opponent->GetObjectGuid() && aura->GetAuraApplyTime() >= duel->startTime)
        {
            auras2remove.push_back(aura->GetId());
        }
    }
    for (size_t i = 0; i < auras2remove.size(); ++i)
    {
        RemoveAurasDueToSpell(auras2remove[i]);
    }

    // cleanup combo points
    if (GetComboTargetGuid() == duel->opponent->GetObjectGuid())
    {
        ClearComboPoints();
    }
    else if (GetComboTargetGuid() == duel->opponent->GetPetGuid())
    {
        ClearComboPoints();
    }

    if (duel->opponent->GetComboTargetGuid() == GetObjectGuid())
    {
        duel->opponent->ClearComboPoints();
    }
    else if (duel->opponent->GetComboTargetGuid() == GetPetGuid())
    {
        duel->opponent->ClearComboPoints();
    }

    // cleanups
    SetGuidValue(PLAYER_DUEL_ARBITER, ObjectGuid());
    SetUInt32Value(PLAYER_DUEL_TEAM, 0);
    duel->opponent->SetGuidValue(PLAYER_DUEL_ARBITER, ObjectGuid());
    duel->opponent->SetUInt32Value(PLAYER_DUEL_TEAM, 0);

    delete duel->opponent->duel;
    duel->opponent->duel = NULL;
    delete duel;
    duel = NULL;
}

/**
 * @brief Sends a single world state update to the client.
 *
 * @param Field The world state field identifier.
 * @param Value The new field value.
 */
void Player::SendUpdateWorldState(uint32 Field, uint32 Value)
{
    WorldPacket data(SMSG_UPDATE_WORLD_STATE, 8);
    data << Field;
    data << Value;
    GetSession()->SendPacket(&data);
}

/**
 * @brief Sends the initial world state set for the player's current zone.
 *
 * @param zoneid The zone identifier used to select world states.
 */
void Player::SendInitWorldStates(uint32 zoneid, uint32 areaid)
{
    // data depends on zoneid/mapid...
    BattleGround* bg = GetBattleGround();
    uint32 mapid = GetMapId();

    DEBUG_LOG("Sending SMSG_INIT_WORLD_STATES to Map:%u, Zone: %u", mapid, zoneid);

    uint32 count = 0;                                       // count of world states in packet

    WorldPacket data(SMSG_INIT_WORLD_STATES, (4 + 4 + 4 + 2 + 8 * 8)); // guess
    data << uint32(mapid);                                  // mapid
    data << uint32(zoneid);                                 // zone id
    data << uint32(areaid);                                 // area id, new 2.1.0
    size_t count_pos = data.wpos();
    data << uint16(0);                                      // count of uint64 blocks, placeholder

    switch (zoneid)
    {
        case 139:                                           // Eastern Plaguelands
        case 1377:                                          // Silithus
        case 3483:                                          // Hellfire Peninsula
        case 3518:                                          // Nagrand
        case 3519:                                          // Terokkar Forest
        case 3521:                                          // Zangarmarsh
            if (OutdoorPvP* outdoorPvP = sOutdoorPvPMgr.GetScript(zoneid))
            {
                outdoorPvP->FillInitialWorldStates(data, count);
            }
            break;
        case 2597:                                          // AV
            if (bg && bg->GetTypeID() == BATTLEGROUND_AV)
            {
                bg->FillInitialWorldStates(data, count);
            }
            break;
        case 3277:                                          // WS
            if (bg && bg->GetTypeID() == BATTLEGROUND_WS)
            {
                bg->FillInitialWorldStates(data, count);
            }
            break;
        case 3358:                                          // AB
            if (bg && bg->GetTypeID() == BATTLEGROUND_AB)
            {
                bg->FillInitialWorldStates(data, count);
            }
            break;
        case 3820:                                          // EY
            if (bg && bg->GetTypeID() == BATTLEGROUND_EY)
            {
                bg->FillInitialWorldStates(data, count);
            }
            break;
        case 3698:                                          // Nagrand Arena
            if (bg && bg->GetTypeID() == BATTLEGROUND_NA)
            {
                bg->FillInitialWorldStates(data, count);
            }
            break;
        case 3702:                                          // Blade's Edge Arena
            if (bg && bg->GetTypeID() == BATTLEGROUND_BE)
            {
                bg->FillInitialWorldStates(data, count);
            }
            break;
        case 3968:                                          // Ruins of Lordaeron
            if (bg && bg->GetTypeID() == BATTLEGROUND_RL)
            {
                bg->FillInitialWorldStates(data, count);
            }
            break;
    }

    data.put<uint16>(count_pos, count);                 // set actual world state amount

    GetSession()->SendPacket(&data);
}

/**
 * @brief Consumes and returns the rested experience bonus for an XP award.
 *
 * @param xp The base experience amount being awarded.
 * @return The rested bonus experience amount.
 */
uint32 Player::GetXPRestBonus(uint32 xp)
{
    uint32 rested_bonus = (uint32)GetRestBonus();           // xp for each rested bonus

    if (rested_bonus > xp)                                  // max rested_bonus == xp or (r+x) = 200% xp
    {
        rested_bonus = xp;
    }

    SetRestBonus(GetRestBonus() - rested_bonus);

    DETAIL_LOG("Player gain %u xp (+ %u Rested Bonus). Rested points=%f", xp + rested_bonus, rested_bonus, GetRestBonus());
    return rested_bonus;
}

/**
 * @brief Sends a bind point confirmation prompt to the client.
 *
 * @param guid The binder NPC GUID.
 */
void Player::SetBindPoint(ObjectGuid guid)
{
    WorldPacket data(SMSG_BINDER_CONFIRM, 8);
    data << ObjectGuid(guid);
    GetSession()->SendPacket(&data);
}

/**
 * @brief Sends a talent reset confirmation prompt to the client.
 *
 * @param guid The trainer or source GUID for the confirmation.
 */
void Player::SendTalentWipeConfirm(ObjectGuid guid)
{
    WorldPacket data(MSG_TALENT_WIPE_CONFIRM, (8 + 4));
    data << ObjectGuid(guid);
    data << uint32(resetTalentsCost());
    GetSession()->SendPacket(&data);
}

/**
 * @brief Sends a pet talent reset confirmation prompt to the client.
 */
void Player::SendPetSkillWipeConfirm()
{
    Pet* pet = GetPet();
    if (!pet)
    {
        return;
    }
    WorldPacket data(SMSG_PET_UNLEARN_CONFIRM, (8 + 4));
    data << ObjectGuid(pet->GetObjectGuid());
    data << uint32(pet->resetTalentsCost());
    GetSession()->SendPacket(&data);
}


/**
 * @brief Checks whether the player has an item from a required totem category.
 *
 * @param TotemCategory The required totem category identifier.
 * @return True if a matching item is present; otherwise, false.
 */
bool Player::HasItemTotemCategory(uint32 TotemCategory) const
{
    Item* pItem;
    for (uint8 i = EQUIPMENT_SLOT_START; i < INVENTORY_SLOT_ITEM_END; ++i)
    {
        pItem = GetItemByPos(INVENTORY_SLOT_BAG_0, i);
        if (pItem && IsTotemCategoryCompatiableWith(pItem->GetProto()->TotemCategory, TotemCategory))
        {
            return true;
        }
    }
    for (uint8 i = KEYRING_SLOT_START; i < KEYRING_SLOT_END; ++i)
    {
        pItem = GetItemByPos(INVENTORY_SLOT_BAG_0, i);
        if (pItem && IsTotemCategoryCompatiableWith(pItem->GetProto()->TotemCategory, TotemCategory))
        {
            return true;
        }
    }
    for (uint8 i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; ++i)
    {
        if (Bag* pBag = (Bag*)GetItemByPos(INVENTORY_SLOT_BAG_0, i))
        {
            for (uint32 j = 0; j < pBag->GetBagSize(); ++j)
            {
                pItem = GetItemByPos(i, j);
                if (pItem && IsTotemCategoryCompatiableWith(pItem->GetProto()->TotemCategory, TotemCategory))
                {
                    return true;
                }
            }
        }
    }
    return false;
}


/**
 * @brief Sends a quest item progress update to the client.
 *
 * @param pQuest The quest being updated.
 * @param item_idx The item objective index.
 * @param count The updated collected count.
 */
void Player::SendQuestUpdateAddItem(Quest const* pQuest, uint32 item_idx, uint32 count)
{
    DEBUG_LOG("WORLD: Sent SMSG_QUESTUPDATE_ADD_ITEM");
    WorldPacket data(SMSG_QUESTUPDATE_ADD_ITEM, (4 + 4));
    data << pQuest->ReqItemId[item_idx];
    data << count;
    GetSession()->SendPacket(&data);
}

/**
 * @brief Checks whether a creature is tapped by this player or the player's group.
 *
 * @param creature The creature to test.
 * @return True if the tap belongs to this player or group; otherwise, false.
 */
bool Player::IsTappedByMeOrMyGroup(Creature* creature)
{
    /* Nobody tapped the monster (solo kill by another NPC) */
    if (!creature->HasFlag(UNIT_DYNAMIC_FLAGS, UNIT_DYNFLAG_TAPPED))
    {
        return false;
    }

    /* If there is a loot recipient, assign it to recipient */
    if (Player* recipient = creature->GetLootRecipient())
    {
        /* See if we're in a group */
        if (Group* plr_group = recipient->GetGroup())
        {
            /* Recipient is in a group... but is it ours? */
            if (Group* my_group = GetGroup())
            {
                /* Check groups are the same */
                if (plr_group != my_group)
                {
                    return false; // Cheater, deny loot
                }
            }
            else
            {
                return false; // We're not in a group, probably cheater
            }

            /* We're in the looters group, so mob is tapped by us */
            return true;
        }
        /* We're not in a group, check to make sure we're the recipient (prevent cheaters) */
        else if (recipient == this)
        {
            return true;
        }
    }
    else
        /* Don't know what happened to the recipient, probably disconnected
         * Either way, it isn't us, so mark as tapped */
         {
             return false;
         }

    return false;
}

/* Checks to see if the current player can loot the creature specified in arg1
 * Called from Object::BuildValuesUpdate */
/**
 * @brief Checks whether the player is currently allowed to loot a creature.
 *
 * @param creature The creature to test.
 * @return True if the player may loot the creature; otherwise, false.
 */
bool Player::isAllowedToLoot(Creature* creature)
{
    /* Nobody tapped the monster (kill either solo or mostly by another NPC) */
    if (!creature->HasFlag(UNIT_DYNAMIC_FLAGS, UNIT_DYNFLAG_TAPPED) || !creature->IsDamageEnoughForLootingAndReward())
    {
        return false;
    }

    /* If we there is a loot recipient, assign it to recipient */
    if (Player* recipient = creature->GetLootRecipient())
    {
        /* See if we're in a group */
        if (Group* plr_group = recipient->GetGroup())
        {
            /* Recipient is in a group... but is it ours? */
            if (Group* my_group = GetGroup())
            {
                /* Check groups are the same */
                if (plr_group != my_group)
                {
                    return false; // Cheater, deny loot
                }
            }
            else
            {
                return false; // We're not in a group, probably cheater
            }

            /* If the player has joined the group after the creature has been killed, doesn't show up. */
            if (creature->GetKilledTime() < plr_group->GetMemberSlotJoinedTime(GetObjectGuid()))
            {
                return false;
            }

            /* We're in a group, get the loot type */
            switch (plr_group->GetLootMethod())
            {
                /* Free for all or Master Loot let everyone loot it */
                case MASTER_LOOT:
                case FREE_FOR_ALL:
                    return true;

                /* These 3 systems all use the same kind of check to display loot,
                    which is what we're doing here. Threshold checks are done elsewhere. */
                case GROUP_LOOT:
                case ROUND_ROBIN:
                case NEED_BEFORE_GREED:
                {
                    uint32 loot_id = creature->GetCreatureInfo()->LootId;

                    /* Checking there's some loot defined. */
                    bool hasLoot = loot_id;
                    /* Checking if the creature may drop any quest loot for us. */
                    bool hasSharedLoot = LootTemplates_Creature.HaveSharedQuestLootForPlayer(loot_id, this);
                    /* Checking if there's any starting quest item available for this player. */
                    bool hasStartingQuestLoot = LootTemplates_Creature.HaveStartingQuestLootForPlayer(loot_id,  this);

                    /* If there's no loot, we return false. */
                    if (!hasLoot)
                    {
                        return false;
                    }
                    /* This is set to true after the looter (chosen below) has closed their loot window
                    * If this is true, allow everyone else in the group to loot the corpse */
                    else if (creature->hasBeenLootedOnce)
                    {
                        return true;
                    }
                    /* If the assigned looter's GUID is equal to ours */
                    else if (creature->assignedLooter == GetGUIDLow())
                    {
                        return true;
                    }
                    /* If the creature already has an assigned looter and that looter isn't us */
                    else if (creature->assignedLooter != 0 && !hasSharedLoot && !hasStartingQuestLoot)
                    {
                        return false;
                    }

                    /* If we've reached here, there is only one exclusive, undecided looter */

                    /* This is the player that will be given permission to loot */
                    Player* final_looter = recipient;

                    /* Iterate through the valid party members */
                    Group::MemberSlotList slots = plr_group->GetMemberSlots();

                    for (Group::MemberSlotList::iterator itr = slots.begin(); itr != slots.end(); ++itr)
                    {
                        /* Get the player data */
                        if (Player* grp_plr = sObjectMgr.GetPlayer(itr->guid))
                        {
                            /* Player is disconnected */
                            if (!grp_plr->IsInWorld())
                            {
                                continue;
                            }

                            /* Player is too far from the creature. */
                            if (!grp_plr->IsWithinDist(creature, sWorld.getConfig(CONFIG_FLOAT_GROUP_XP_DISTANCE), false))
                            {
                                continue;
                            }

                            /* Check if the last time the player looted is less than the current final looter
                             * If the value is lower, it means it happened longer ago */
                            if (final_looter->lastTimeLooted > grp_plr->lastTimeLooted)
                            {
                                final_looter = grp_plr;
                            }
                        }
                    }

                    /* We have our looter, update their loot time */
                    final_looter->lastTimeLooted = time(NULL);

                    /* Update the creature with the looter that has been assigned to them */
                    creature->assignedLooter = final_looter->GetGUIDLow();
                    final_looter->GetGroup()->SetLooterGuid(final_looter->GetGUID());

                    /* Finally, return if we are the assigned looter */
                    return (final_looter->GetGUIDLow() == GetGUIDLow() || hasSharedLoot || hasStartingQuestLoot);
                    /* End of switch statement */
                }
                default:
                    // Something went wrong, avoid crash
                    return false;
            }
        }
        /* We're not in a group, check to make sure we're the recipient (prevent cheaters) */
        else if (recipient == this)
        {
            return true;
        }
    }
    else
        // prevent other players from looting if the recipient got disconnected
    {
        return !creature->HasLootRecipient();
    }

    return false;
}

/**
 * @brief Writes the player's current combat and stat values to the debug log.
 */
void Player::outDebugStatsValues() const
{
    // optimize disabled debug output
    if (!sLog.HasLogLevelOrHigher(LOG_LVL_DEBUG) || sLog.HasLogFilter(LOG_FILTER_PLAYER_STATS))
    {
        return;
    }

    sLog.outDebug("HP is: \t\t\t%u\t\tMP is: \t\t\t%u", GetMaxHealth(), GetMaxPower(POWER_MANA));
    sLog.outDebug("AGILITY is: \t\t%f\t\tSTRENGTH is: \t\t%f", GetStat(STAT_AGILITY), GetStat(STAT_STRENGTH));
    sLog.outDebug("INTELLECT is: \t\t%f\t\tSPIRIT is: \t\t%f", GetStat(STAT_INTELLECT), GetStat(STAT_SPIRIT));
    sLog.outDebug("STAMINA is: \t\t%f", GetStat(STAT_STAMINA));
    sLog.outDebug("Armor is: \t\t%u\t\tBlock is: \t\t%f", GetArmor(), GetFloatValue(PLAYER_BLOCK_PERCENTAGE));
    sLog.outDebug("HolyRes is: \t\t%u\t\tFireRes is: \t\t%u", GetResistance(SPELL_SCHOOL_HOLY), GetResistance(SPELL_SCHOOL_FIRE));
    sLog.outDebug("NatureRes is: \t\t%u\t\tFrostRes is: \t\t%u", GetResistance(SPELL_SCHOOL_NATURE), GetResistance(SPELL_SCHOOL_FROST));
    sLog.outDebug("ShadowRes is: \t\t%u\t\tArcaneRes is: \t\t%u", GetResistance(SPELL_SCHOOL_SHADOW), GetResistance(SPELL_SCHOOL_ARCANE));
    sLog.outDebug("MIN_DAMAGE is: \t\t%f\tMAX_DAMAGE is: \t\t%f", GetFloatValue(UNIT_FIELD_MINDAMAGE), GetFloatValue(UNIT_FIELD_MAXDAMAGE));
    sLog.outDebug("MIN_OFFHAND_DAMAGE is: \t%f\tMAX_OFFHAND_DAMAGE is: \t%f", GetFloatValue(UNIT_FIELD_MINOFFHANDDAMAGE), GetFloatValue(UNIT_FIELD_MAXOFFHANDDAMAGE));
    sLog.outDebug("MIN_RANGED_DAMAGE is: \t%f\tMAX_RANGED_DAMAGE is: \t%f", GetFloatValue(UNIT_FIELD_MINRANGEDDAMAGE), GetFloatValue(UNIT_FIELD_MAXRANGEDDAMAGE));
    sLog.outDebug("ATTACK_TIME is: \t%u\t\tRANGE_ATTACK_TIME is: \t%u", GetAttackTime(BASE_ATTACK), GetAttackTime(RANGED_ATTACK));
}

/*********************************************************/
/***               FLOOD FILTER SYSTEM                 ***/
/*********************************************************/

void Player::UpdateSpeakTime()
{
    // ignore chat spam protection for GMs in any mode
    if (GetSession()->GetSecurity() > SEC_PLAYER)
    {
        return;
    }

    time_t current = time(NULL);
    if (m_speakTime > current)
    {
        uint32 max_count = sWorld.getConfig(CONFIG_UINT32_CHATFLOOD_MESSAGE_COUNT);
        if (!max_count)
        {
            return;
        }

        ++m_speakCount;
        if (m_speakCount >= max_count)
        {
            // prevent overwrite mute time, if message send just before mutes set, for example.
            time_t new_mute = current + sWorld.getConfig(CONFIG_UINT32_CHATFLOOD_MUTE_TIME);
            if (GetSession()->m_muteTime < new_mute)
            {
                GetSession()->m_muteTime = new_mute;
            }

            m_speakCount = 0;
        }
    }
    else
    {
        m_speakCount = 0;
    }

    m_speakTime = current + sWorld.getConfig(CONFIG_UINT32_CHATFLOOD_MESSAGE_DELAY);
}

/**
 * @brief Checks whether the player is currently allowed to send chat messages.
 *
 * @return True if the player's mute timer has expired; otherwise, false.
 */
bool Player::CanSpeak() const
{
    return  GetSession()->m_muteTime <= time(NULL);
}

/*********************************************************/
/***              LOW LEVEL FUNCTIONS:Notifiers        ***/
/*********************************************************/

void Player::SendAttackSwingNotInRange()
{
    WorldPacket data(SMSG_ATTACKSWING_NOTINRANGE, 0);
    GetSession()->SendPacket(&data);
}

/**
 * @brief Replaces a tokenized uint32 field value in a serialized array.
 *
 * @param tokens The token array to modify.
 * @param index The token index to replace.
 * @param value The new uint32 value.
 */
void Player::SetUInt32ValueInArray(Tokens& tokens, uint16 index, uint32 value)
{
    char buf[11];
    snprintf(buf, 11, "%u", value);

    if (index >= tokens.size())
    {
        return;
    }

    tokens[index] = buf;
}

/**
 * @brief Sends the error packet for attempting to attack while not standing.
 */
void Player::SendAttackSwingNotStanding()
{
    WorldPacket data(SMSG_ATTACKSWING_NOTSTANDING, 0);
    GetSession()->SendPacket(&data);
}

/**
 * @brief Sends the error packet for attempting to attack a dead target.
 */
void Player::SendAttackSwingDeadTarget()
{
    WorldPacket data(SMSG_ATTACKSWING_DEADTARGET, 0);
    GetSession()->SendPacket(&data);
}

/**
 * @brief Sends the error packet for a general inability to attack the target.
 */
void Player::SendAttackSwingCantAttack()
{
    WorldPacket data(SMSG_ATTACKSWING_CANT_ATTACK, 0);
    GetSession()->SendPacket(&data);
}

/**
 * @brief Sends the packet that cancels the player's current attack.
 */
void Player::SendAttackSwingCancelAttack()
{
    WorldPacket data(SMSG_CANCEL_COMBAT, 0);
    GetSession()->SendPacket(&data);
}

/**
 * @brief Sends the error packet for attempting to attack while facing the wrong direction.
 */
void Player::SendAttackSwingBadFacingAttack()
{
    WorldPacket data(SMSG_ATTACKSWING_BADFACING, 0);
    GetSession()->SendPacket(&data);
}

/**
 * @brief Sends the packet that cancels auto-repeat attacks for the client.
 */
void Player::SendAutoRepeatCancel()
{
    WorldPacket data(SMSG_CANCEL_AUTO_REPEAT, 0);
    GetSession()->SendPacket(&data);
}

/**
 * @brief Sends an exploration experience reward packet to the client.
 *
 * @param Area The explored area identifier.
 * @param Experience The awarded experience amount.
 */
void Player::SendExplorationExperience(uint32 Area, uint32 Experience)
{
    WorldPacket data(SMSG_EXPLORATION_EXPERIENCE, 8);
    data << uint32(Area);
    data << uint32(Experience);
    GetSession()->SendPacket(&data);
}

void Player::SendDungeonDifficulty(bool IsInGroup)
{
    uint8 val = 0x00000001;
    WorldPacket data(MSG_SET_DUNGEON_DIFFICULTY, 12);
    data << (uint32)GetDifficulty();
    data << uint32(val);
    data << uint32(IsInGroup);
    GetSession()->SendPacket(&data);
}

/**
 * @brief Sends a reset-failed notification for an instance map.
 *
 * @param mapid The map identifier that failed to reset.
 */
void Player::SendResetFailedNotify(uint32 mapid)
{
    WorldPacket data(SMSG_RESET_FAILED_NOTIFY, 4);
    data << uint32(mapid);
    GetSession()->SendPacket(&data);
}

/// Reset all solo instances and optionally send a message on success for each
void Player::ResetInstances(InstanceResetMethod method)
{
    // method can be INSTANCE_RESET_ALL, INSTANCE_RESET_CHANGE_DIFFICULTY, INSTANCE_RESET_GROUP_JOIN

    // we assume that when the difficulty changes, all instances that can be reset will be
    Difficulty diff = GetDifficulty();

    for (BoundInstancesMap::iterator itr = m_boundInstances[diff].begin(); itr != m_boundInstances[diff].end();)
    {
        DungeonPersistentState* state = itr->second.state;
        const MapEntry* entry = sMapStore.LookupEntry(itr->first);
        if (!entry || !state->CanReset())
        {
            ++itr;
            continue;
        }

        if (method == INSTANCE_RESET_ALL)
        {
            // the "reset all instances" method can only reset normal maps
            if (entry->map_type == MAP_RAID || diff == DUNGEON_DIFFICULTY_HEROIC)
            {
                ++itr;
                continue;
            }
        }

        // if the map is loaded, reset it
        if (Map* map = sMapMgr.FindMap(state->GetMapId(), state->GetInstanceId()))
            if (map->IsDungeon())
            {
                ((DungeonMap*)map)->Reset(method);
            }

        // since this is a solo instance there should not be any players inside
        if (method == INSTANCE_RESET_ALL || method == INSTANCE_RESET_CHANGE_DIFFICULTY)
        {
            SendResetInstanceSuccess(state->GetMapId());
        }

        state->DeleteFromDB();
        m_boundInstances[diff].erase(itr++);

        // the following should remove the instance save from the manager and delete it as well
        state->RemovePlayer(this);
    }
}

/**
 * @brief Sends a successful instance reset notification to the client.
 *
 * @param MapId The reset map identifier.
 */
void Player::SendResetInstanceSuccess(uint32 MapId)
{
    WorldPacket data(SMSG_INSTANCE_RESET, 4);
    data << uint32(MapId);
    GetSession()->SendPacket(&data);
}

/**
 * @brief Sends an instance reset failure message to the client.
 *
 * @param reason The reset failure reason code.
 * @param MapId The map identifier that failed to reset.
 */
void Player::SendResetInstanceFailed(uint32 reason, uint32 MapId)
{
    // reason: see enum InstanceResetFailReason
    WorldPacket data(SMSG_INSTANCE_RESET_FAILED, 4);
    data << uint32(reason);
    data << uint32(MapId);
    GetSession()->SendPacket(&data);
}

/*********************************************************/
/***              Update timers                        ***/
/*********************************************************/

/// checks the 15 afk reports per 5 minutes limit
void Player::UpdateAfkReport(time_t currTime)
{
    if (m_bgData.bgAfkReportedTimer <= currTime)
    {
        m_bgData.bgAfkReportedCount = 0;
        m_bgData.bgAfkReportedTimer = currTime + 5 * MINUTE;
    }
}

void Player::UpdateContestedPvP(uint32 diff)
{
    if (!m_contestedPvPTimer || IsInCombat())
    {
        return;
    }
    if (m_contestedPvPTimer <= diff)
    {
        ResetContestedPvP();
    }
    else
    {
        m_contestedPvPTimer -= diff;
    }
}

/**
 * @brief Updates and clears the player's PvP flag when the timeout expires.
 *
 * @param currTime The current server time.
 */
void Player::UpdatePvPFlag(time_t currTime)
{
    if (!IsPvP())
    {
        return;
    }
    if (pvpInfo.endTimer == 0 || currTime < (pvpInfo.endTimer + 300))
    {
        return;
    }

    UpdatePvP(false);
}

/**
 * @brief Enables or disables the free-for-all PvP player flag.
 *
 * @param state True to enable FFA PvP; false to clear it.
 */
void Player::SetFFAPvP(bool state)
{
    if (state)
    {
        SetFlag(PLAYER_FLAGS, PLAYER_FLAGS_FFA_PVP);
    }
    else
    {
        RemoveFlag(PLAYER_FLAGS, PLAYER_FLAGS_FFA_PVP);
    }
}

/**
 * @brief Starts an active duel once the duel countdown completes.
 *
 * @param currTime The current server time.
 */
void Player::UpdateDuelFlag(time_t currTime)
{
    if (!duel || duel->startTimer == 0 || currTime < duel->startTimer + 3)
    {
        return;
    }

    // Used by Eluna
#ifdef ENABLE_ELUNA
    if (Eluna* e = GetEluna())
    {
        e->OnDuelStart(this, duel->opponent);
    }
#endif /* ENABLE_ELUNA */

    SetUInt32Value(PLAYER_DUEL_TEAM, 1);
    duel->opponent->SetUInt32Value(PLAYER_DUEL_TEAM, 2);

    duel->startTimer = 0;
    duel->startTime  = currTime;
    duel->opponent->duel->startTimer = 0;
    duel->opponent->duel->startTime  = currTime;
}

/**
 * @brief Unsummons the player's current pet using the requested save mode.
 *
 * @param mode The persistence mode to use when removing the pet.
 */
void Player::RemovePet(PetSaveMode mode)
{
    if (Pet* pet = GetPet())
    {
        pet->Unsummon(mode, this);
    }
}

/**
 * @brief Unsummons the player's active mini-pet.
 */
void Player::RemoveMiniPet()
{
    if (Pet* pet = GetMiniPet())
    {
        pet->Unsummon(PET_SAVE_AS_DELETED);
    }
}

/**
 * @brief Retrieves the player's currently summoned mini-pet.
 *
 * @return The mini-pet instance, or NULL if none is active.
 */
Pet* Player::GetMiniPet() const
{
    if (m_miniPetGuid.IsEmpty())
    {
        return NULL;
    }

    return GetMap()->GetPet(m_miniPetGuid);
}

/**
 * @brief Clears the pet action bar on the client.
 */
void Player::RemovePetActionBar()
{
    WorldPacket data(SMSG_PET_SPELLS, 8);
    data << ObjectGuid();
    SendDirectMessage(&data);
}

/**
 * @brief Checks whether a spell modifier currently applies to a spell.
 *
 * @param spellInfo The spell entry being evaluated.
 * @param mod The modifier to test.
 * @param spell The active spell instance, if any.
 * @return True if the modifier applies; otherwise, false.
 */
bool Player::IsAffectedBySpellmod(SpellEntry const* spellInfo, SpellModifier* mod, Spell const* spell)
{
    if (!mod || !spellInfo)
    {
        return false;
    }

    if (mod->charges == -1 && mod->lastAffected)            // marked as expired but locked until spell casting finish
    {
        // prevent apply to any spell except spell that trigger expire
        if (spell)
        {
            if (mod->lastAffected != spell)
            {
                return false;
            }
        }
        else if (mod->lastAffected != FindCurrentSpellBySpellId(spellInfo->Id))
        {
            return false;
        }
    }

    return mod->isAffectedOnSpell(spellInfo);
}

/**
 * @brief Applies or removes a spell modifier and notifies the client.
 *
 * @param mod The modifier to add or remove.
 * @param apply True to apply the modifier; false to remove it.
 */
void Player::AddSpellMod(SpellModifier* mod, bool apply)
{
    OpcodesList opcode = (mod->type == SPELLMOD_FLAT) ? SMSG_SET_FLAT_SPELL_MODIFIER : SMSG_SET_PCT_SPELL_MODIFIER;

    for (int eff = 0; eff < 64; ++eff)
    {
        uint64 _mask = uint64(1) << eff;
        if (mod->mask.IsFitToFamilyMask(_mask))
        {
            int32 val = 0;
            for (SpellModList::const_iterator itr = m_spellMods[mod->op].begin(); itr != m_spellMods[mod->op].end(); ++itr)
            {
                if ((*itr)->type == mod->type && ((*itr)->mask.IsFitToFamilyMask(_mask)))
                {
                    val += (*itr)->value;
                }
            }
            val += apply ? mod->value : -(mod->value);
            WorldPacket data(opcode, (1 + 1 + 4));
            data << uint8(eff);
            data << uint8(mod->op);
            data << int32(val);
            SendDirectMessage(&data);
        }
    }

    if (apply)
    {
        m_spellMods[mod->op].push_back(mod);
    }
    else
    {
        if (mod->charges == -1)
        {
            --m_SpellModRemoveCount;
        }
        m_spellMods[mod->op].remove(mod);
        delete mod;
    }
}

/**
 * @brief Finds a spell modifier by operation and owning spell.
 *
 * @param op The modifier operation bucket.
 * @param spellId The spell that created the modifier.
 * @return The matching modifier, or NULL if none exists.
 */
SpellModifier* Player::GetSpellMod(SpellModOp op, uint32 spellId) const
{
    for (SpellModList::const_iterator itr = m_spellMods[op].begin(); itr != m_spellMods[op].end(); ++itr)
    {
        if ((*itr)->spellId == spellId)
        {
            return *itr;
        }
    }

    return NULL;
}

/**
 * @brief Removes expired spell modifiers associated with a spell cast.
 *
 * @param spell The spell whose pending modifiers should be consumed.
 */
void Player::RemoveSpellMods(Spell const* spell)
{
    if (!spell || (m_SpellModRemoveCount == 0))
    {
        return;
    }

    for (int i = 0; i < MAX_SPELLMOD; ++i)
    {
        for (SpellModList::const_iterator itr = m_spellMods[i].begin(); itr != m_spellMods[i].end();)
        {
            SpellModifier* mod = *itr;
            ++itr;

            if (mod && mod->charges == -1 && (mod->lastAffected == spell || mod->lastAffected == NULL))
            {
                RemoveAurasDueToSpell(mod->spellId);
                if (m_spellMods[i].empty())
                {
                    break;
                }
                else
                {
                    itr = m_spellMods[i].begin();
                }
            }
        }
    }
}

/**
 * @brief Restores cancellable spell modifiers when a spell cast is interrupted.
 *
 * @param spell The canceled spell instance.
 */
void Player::ResetSpellModsDueToCanceledSpell(Spell const* spell)
{
    for (int i = 0; i < MAX_SPELLMOD; ++i)
    {
        for (SpellModList::const_iterator itr = m_spellMods[i].begin(); itr != m_spellMods[i].end(); ++itr)
        {
            SpellModifier *mod = *itr;

            if (mod->lastAffected != spell)
            {
                continue;
            }

            mod->lastAffected = NULL;

            if (mod->charges == -1)
            {
                mod->charges = 1;
                if (m_SpellModRemoveCount > 0)
                {
                    --m_SpellModRemoveCount;
                }
            }
            else if (mod->charges > 0)
            {
                ++mod->charges;
            }
        }
    }
}

// send Proficiency
void Player::SendProficiency(ItemClass itemClass, uint32 itemSubclassMask)
{
    WorldPacket data(SMSG_SET_PROFICIENCY, 1 + 4);
    data << uint8(itemClass) << uint32(itemSubclassMask);
    GetSession()->SendPacket(&data);
}

/**
 * @brief Removes petition ownership and signatures associated with a player.
 *
 * @param guid The player GUID whose petition data should be removed.
 */
void Player::RemovePetitionsAndSigns(ObjectGuid guid, uint32 type)
{
    uint32 lowguid = guid.GetCounter();

    QueryResult* result = NULL;
    if (type == 10)
    {
        result = CharacterDatabase.PQuery("SELECT `ownerguid`,`petitionguid` FROM `petition_sign` WHERE `playerguid` = '%u'", lowguid);
    }
    else
    {
        result = CharacterDatabase.PQuery("SELECT `ownerguid`,`petitionguid` FROM `petition_sign` WHERE `playerguid` = '%u' AND `type` = '%u'", lowguid, type);
    }
    if (result)
    {
        do                                                  // this part effectively does nothing, since the deletion / modification only takes place _after_ the PetitionQuery. Though I don't know if the result remains intact if I execute the delete query beforehand.
        {
            // and SendPetitionQueryOpcode reads data from the DB
            Field* fields = result->Fetch();
            ObjectGuid ownerguid   = ObjectGuid(HIGHGUID_PLAYER, fields[0].GetUInt32());
            ObjectGuid petitionguid = ObjectGuid(HIGHGUID_ITEM, fields[1].GetUInt32());

            // send update if charter owner in game
            Player* owner = sObjectMgr.GetPlayer(ownerguid);
            if (owner)
            {
                owner->GetSession()->SendPetitionQueryOpcode(petitionguid);
            }
        }
        while (result->NextRow());

        delete result;

        if (type == 10)
        {
            CharacterDatabase.PExecute("DELETE FROM `petition_sign` WHERE `playerguid` = '%u'", lowguid);
        }
        else
        {
            CharacterDatabase.PExecute("DELETE FROM `petition_sign` WHERE `playerguid` = '%u' AND `type` = '%u'", lowguid, type);
        }
    }

    CharacterDatabase.BeginTransaction();
    if (type == 10)
    {
        CharacterDatabase.PExecute("DELETE FROM `petition` WHERE `ownerguid` = '%u'", lowguid);
        CharacterDatabase.PExecute("DELETE FROM `petition_sign` WHERE `ownerguid` = '%u'", lowguid);
    }
    else
    {
        CharacterDatabase.PExecute("DELETE FROM `petition` WHERE `ownerguid` = '%u' AND `type` = '%u'", lowguid, type);
        CharacterDatabase.PExecute("DELETE FROM `petition_sign` WHERE `ownerguid` = '%u' AND `type` = '%u'", lowguid, type);
    }
    CharacterDatabase.CommitTransaction();
}

void Player::LeaveAllArenaTeams(ObjectGuid guid)
{
    uint32 lowguid = guid.GetCounter();
    QueryResult* result = CharacterDatabase.PQuery("SELECT `arena_team_member`.`arenateamid` FROM `arena_team_member` JOIN `arena_team` ON `arena_team_member`.`arenateamid` = `arena_team`.`arenateamid` WHERE `guid`='%u'", lowguid);
    if (!result)
    {
        return;
    }

    do
    {
        Field* fields = result->Fetch();
        if (uint32 at_id = fields[0].GetUInt32())
            if (ArenaTeam* at = sObjectMgr.GetArenaTeamById(at_id))
            {
                at->DelMember(guid);
            }
    }
    while (result->NextRow());

    delete result;
}

/**
 * @brief Sets the player's accumulated rested experience bonus.
 *
 * @param rest_bonus_new The new rested bonus amount.
 */
void Player::SetRestBonus(float rest_bonus_new)
{
    // Prevent resting on max level
    if (getLevel() >= sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL))
    {
        rest_bonus_new = 0;
    }

    if (rest_bonus_new < 0)
    {
        rest_bonus_new = 0;
    }

    float rest_bonus_max = (float)GetUInt32Value(PLAYER_NEXT_LEVEL_XP) * 1.5f / 2.0f;

    if (rest_bonus_new > rest_bonus_max)
    {
        m_rest_bonus = rest_bonus_max;
    }
    else
    {
        m_rest_bonus = rest_bonus_new;
    }

    // update data for client
    if (m_rest_bonus > 10)
    {
        SetByteValue(PLAYER_BYTES_2, 3, REST_STATE_RESTED);
    }
    else if (m_rest_bonus <= 1)
    {
        SetByteValue(PLAYER_BYTES_2, 3, REST_STATE_NORMAL);
    }

    // RestTickUpdate
    SetUInt32Value(PLAYER_REST_STATE_EXPERIENCE, uint32(m_rest_bonus));
}

/**
 * @brief Updates visibility of nearby stealthed units based on detection checks.
 */
void Player::HandleStealthedUnitsDetection()
{
    std::list<Unit*> stealthedUnits;

    MaNGOS::AnyStealthedCheck u_check(this);
    MaNGOS::UnitListSearcher<MaNGOS::AnyStealthedCheck > searcher(stealthedUnits, u_check);
    Cell::VisitAllObjects(this, searcher, MAX_PLAYER_STEALTH_DETECT_RANGE);

    WorldObject const* viewPoint = GetCamera().GetBody();

    for (std::list<Unit*>::const_iterator i = stealthedUnits.begin(); i != stealthedUnits.end(); ++i)
    {
        if ((*i) == this)
        {
            continue;
        }

        bool hasAtClient = HaveAtClient((*i));
        bool hasDetected = (*i)->IsVisibleForOrDetect(this, viewPoint, true);

        if (hasDetected)
        {
            if (!hasAtClient)
            {
                ObjectGuid i_guid = (*i)->GetObjectGuid();
                (*i)->SendCreateUpdateToPlayer(this);
                m_clientGUIDs.insert(i_guid);

                DEBUG_FILTER_LOG(LOG_FILTER_VISIBILITY_CHANGES, "UpdateVisibilityOf(): %s is detected in stealth by player %u. Distance = %f", i_guid.GetString().c_str(), GetGUIDLow(), GetDistance(*i));

                // target aura duration for caster show only if target exist at caster client
                // send data at target visibility change (adding to client)
                if ((*i) != this && (*i)->isType(TYPEMASK_UNIT))
                {
                    SendAuraDurationsForTarget(*i);
                }
            }
        }
        else
        {
            if (hasAtClient)
            {
                (*i)->DestroyForPlayer(this);
                m_clientGUIDs.erase((*i)->GetObjectGuid());
            }
        }
    }
}

/**
 * @brief Starts a taxi flight across a sequence of taxi nodes.
 *
 * @param nodes The ordered taxi node path to travel.
 * @param npc The taxi master providing the route, or NULL for spell/scripted travel.
 * @param spellid The spell initiating the taxi flight, if any.
 * @return True if the flight started successfully; otherwise, false.
 */
bool Player::ActivateTaxiPathTo(std::vector<uint32> const& nodes, Creature* npc /*= NULL*/, uint32 spellid /*= 0*/)
{
    if (nodes.size() < 2)
    {
        return false;
    }

    // not let cheating with start flight in time of logout process || if casting not finished || while in combat || if not use Spell's with EffectSendTaxi
    if (GetSession()->isLogingOut() || IsInCombat())
    {
        GetSession()->SendActivateTaxiReply(ERR_TAXIPLAYERBUSY);
        return false;
    }

    if (HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_DISABLE_MOVE))
    {
        return false;
    }

    // taximaster case
    if (npc)
    {
        // not let cheating with start flight mounted
        if (IsMounted())
        {
            GetSession()->SendActivateTaxiReply(ERR_TAXIPLAYERALREADYMOUNTED);
            return false;
        }

        if (IsInDisallowedMountForm())
        {
            GetSession()->SendActivateTaxiReply(ERR_TAXIPLAYERSHAPESHIFTED);
            return false;
        }

        // not let cheating with start flight in time of logout process || if casting not finished || while in combat || if not use Spell's with EffectSendTaxi
        if (IsNonMeleeSpellCasted(false))
        {
            GetSession()->SendActivateTaxiReply(ERR_TAXIPLAYERBUSY);
            return false;
        }
    }
    // cast case or scripted call case
    else
    {
        RemoveSpellsCausingAura(SPELL_AURA_MOUNTED);

        if (IsInDisallowedMountForm())
        {
            RemoveSpellsCausingAura(SPELL_AURA_MOD_SHAPESHIFT);
        }

        if (Spell* spell = GetCurrentSpell(CURRENT_GENERIC_SPELL))
            if (spell->m_spellInfo->Id != spellid)
            {
                InterruptSpell(CURRENT_GENERIC_SPELL, false);
            }

        InterruptSpell(CURRENT_AUTOREPEAT_SPELL, false);

        if (Spell* spell = GetCurrentSpell(CURRENT_CHANNELED_SPELL))
            if (spell->m_spellInfo->Id != spellid)
            {
                InterruptSpell(CURRENT_CHANNELED_SPELL, true);
            }
    }

    uint32 sourcenode = nodes[0];

    // starting node too far away (cheat?)
    TaxiNodesEntry const* node = sTaxiNodesStore.LookupEntry(sourcenode);
    if (!node)
    {
        GetSession()->SendActivateTaxiReply(ERR_TAXINOSUCHPATH);
        return false;
    }

    // check node starting pos data set case if provided
    if (node->x != 0.0f || node->y != 0.0f || node->z != 0.0f)
    {
        if (node->map_id != GetMapId() ||
            (node->x - GetPositionX()) * (node->x - GetPositionX()) +
            (node->y - GetPositionY()) * (node->y - GetPositionY()) +
            (node->z - GetPositionZ()) * (node->z - GetPositionZ()) >
            (2 * INTERACTION_DISTANCE) * (2 * INTERACTION_DISTANCE) * (2 * INTERACTION_DISTANCE))
        {
            GetSession()->SendActivateTaxiReply(ERR_TAXITOOFARAWAY);
            return false;
        }
    }
    // node must have pos if taxi master case (npc != NULL)
    else if (npc)
    {
        GetSession()->SendActivateTaxiReply(ERR_TAXIUNSPECIFIEDSERVERERROR);
        return false;
    }

    // Prepare to flight start now

    // stop combat at start taxi flight if any
    CombatStop();

    // stop trade (client cancel trade at taxi map open but cheating tools can be used for reopen it)
    TradeCancel(true);

    // clean not finished taxi path if any
    m_taxi.ClearTaxiDestinations();

    // 0 element current node
    m_taxi.AddTaxiDestination(sourcenode);

    // fill destinations path tail
    uint32 sourcepath = 0;
    uint32 totalcost = 0;

    uint32 prevnode = sourcenode;

    for (uint32 i = 1; i < nodes.size(); ++i)
    {
        uint32 path, cost;

        uint32 lastnode = nodes[i];
        sObjectMgr.GetTaxiPath(prevnode, lastnode, path, cost);

        if (!path)
        {
            m_taxi.ClearTaxiDestinations();
            return false;
        }

        totalcost += cost;

        if (prevnode == sourcenode)
        {
            sourcepath = path;
        }

        m_taxi.AddTaxiDestination(lastnode);

        prevnode = lastnode;
    }

    // get mount model (in case non taximaster (npc==NULL) allow more wide lookup)
    uint32 mount_display_id = sObjectMgr.GetTaxiMountDisplayId(sourcenode, GetTeam(), npc == NULL);

    // in spell case allow 0 model
    if ((mount_display_id == 0 && spellid == 0) || sourcepath == 0)
    {
        GetSession()->SendActivateTaxiReply(ERR_TAXIUNSPECIFIEDSERVERERROR);

        m_taxi.ClearTaxiDestinations();
        return false;
    }

    uint32 money = GetMoney();

    if (npc)
    {
        totalcost = (uint32)ceil(totalcost * GetReputationPriceDiscount(npc));
    }

    if (money < totalcost)
    {
        GetSession()->SendActivateTaxiReply(ERR_TAXINOTENOUGHMONEY);

        m_taxi.ClearTaxiDestinations();
        return false;
    }

    // Checks and preparations done, DO FLIGHT
    ModifyMoney(-(int32)totalcost);

    // prevent stealth flight
    RemoveSpellsCausingAura(SPELL_AURA_MOD_STEALTH);

    if (sWorld.getConfig(CONFIG_BOOL_INSTANT_TAXI))
    {
        TaxiNodesEntry const* lastnode = sTaxiNodesStore.LookupEntry(nodes[nodes.size() - 1]);
        m_taxi.ClearTaxiDestinations();
        TeleportTo(lastnode->map_id, lastnode->x, lastnode->y, lastnode->z, GetOrientation());
        return false;
    }
    else
    {
        GetSession()->SendActivateTaxiReply(ERR_TAXIOK);
        GetSession()->SendDoFlight(mount_display_id, sourcepath);
    }

    return true;
}

/**
 * @brief Starts a taxi flight using a direct taxi path identifier.
 *
 * @param taxi_path_id The taxi path identifier to use.
 * @param spellid The spell initiating the taxi flight, if any.
 * @return True if the flight started successfully; otherwise, false.
 */
bool Player::ActivateTaxiPathTo(uint32 taxi_path_id, uint32 spellid /*= 0*/)
{
    TaxiPathEntry const* entry = sTaxiPathStore.LookupEntry(taxi_path_id);
    if (!entry)
    {
        return false;
    }

    std::vector<uint32> nodes;

    nodes.resize(2);
    nodes[0] = entry->from;
    nodes[1] = entry->to;

    return ActivateTaxiPathTo(nodes, NULL, spellid);
}

/**
 * @brief Resumes an interrupted taxi flight from the nearest path node.
 */
void Player::ContinueTaxiFlight()
{
    uint32 sourceNode = m_taxi.GetTaxiSource();
    if (!sourceNode)
    {
        return;
    }

    DEBUG_LOG("WORLD: Restart character %u taxi flight", GetGUIDLow());

    uint32 mountDisplayId = sObjectMgr.GetTaxiMountDisplayId(sourceNode, GetTeam(), true);
    uint32 path = m_taxi.GetCurrentTaxiPath();

    // search appropriate start path node
    uint32 startNode = 0;

    TaxiPathNodeList const& nodeList = sTaxiPathNodesByPath[path];

    float distPrev = MAP_SIZE * MAP_SIZE;
    float distNext =
        (nodeList[0].x - GetPositionX()) * (nodeList[0].x - GetPositionX()) +
        (nodeList[0].y - GetPositionY()) * (nodeList[0].y - GetPositionY()) +
        (nodeList[0].z - GetPositionZ()) * (nodeList[0].z - GetPositionZ());

    for (uint32 i = 1; i < nodeList.size(); ++i)
    {
        TaxiPathNodeEntry const& node = nodeList[i];
        TaxiPathNodeEntry const& prevNode = nodeList[i - 1];

        // skip nodes at another map
        if (node.mapid != GetMapId())
        {
            continue;
        }

        distPrev = distNext;

        distNext =
            (node.x - GetPositionX()) * (node.x - GetPositionX()) +
            (node.y - GetPositionY()) * (node.y - GetPositionY()) +
            (node.z - GetPositionZ()) * (node.z - GetPositionZ());

        float distNodes =
            (node.x - prevNode.x) * (node.x - prevNode.x) +
            (node.y - prevNode.y) * (node.y - prevNode.y) +
            (node.z - prevNode.z) * (node.z - prevNode.z);

        if (distNext + distPrev < distNodes)
        {
            startNode = i;
            break;
        }
    }

    GetSession()->SendDoFlight(mountDisplayId, path, startNode);
}

/**
 * @brief Applies cooldown lockouts to spells in the specified school mask.
 *
 * @param idSchoolMask The spell school mask to prohibit.
 * @param unTimeMs The prohibition duration in milliseconds.
 */
void Player::ProhibitSpellSchool(SpellSchoolMask idSchoolMask, uint32 unTimeMs)
{
    Unit::ProhibitSpellSchool(idSchoolMask, unTimeMs);
    // last check 2.0.10
    WorldPacket data(SMSG_SPELL_COOLDOWN, 8 + 1 + m_spells.size() * 8);
    data << GetObjectGuid();
    data << uint8(0x0);                                     // flags (0x1, 0x2)
    time_t curTime = time(NULL);
    for (PlayerSpellMap::const_iterator itr = m_spells.begin(); itr != m_spells.end(); ++itr)
    {
        if (itr->second.state == PLAYERSPELL_REMOVED)
        {
            continue;
        }
        uint32 unSpellId = itr->first;
        SpellEntry const* spellInfo = sSpellStore.LookupEntry(unSpellId);
        MANGOS_ASSERT(spellInfo);

        // Not send cooldown for this spells
        if (spellInfo->HasAttribute(SPELL_ATTR_DISABLED_WHILE_ACTIVE))
        {
            continue;
        }

        if ((idSchoolMask & GetSpellSchoolMask(spellInfo)) && GetSpellCooldownDelay(unSpellId) < unTimeMs)
        {
            data << uint32(unSpellId);
            data << uint32(unTimeMs);                       // in m.secs
            AddSpellCooldown(unSpellId, 0, curTime + unTimeMs / IN_MILLISECONDS);
        }
    }
    GetSession()->SendPacket(&data);
}

/**
 * @brief Reinitializes player combat data for the current shapeshift form.
 *
 * @param reapplyMods True when reapplying modifiers without a real form change.
 */
void Player::InitDataForForm(bool reapplyMods)
{
    ShapeshiftForm form = GetShapeshiftForm();

    SpellShapeshiftFormEntry const* ssEntry = sSpellShapeshiftFormStore.LookupEntry(form);
    if (ssEntry && ssEntry->attackSpeed)
    {
        SetAttackTime(BASE_ATTACK, ssEntry->attackSpeed);
        SetAttackTime(OFF_ATTACK, ssEntry->attackSpeed);
        SetAttackTime(RANGED_ATTACK, BASE_ATTACK_TIME);
    }
    else
    {
        SetRegularAttackTime();
    }

    switch (form)
    {
        case FORM_CAT:
        {
            if (GetPowerType() != POWER_ENERGY)
            {
                SetPowerType(POWER_ENERGY);
            }
            break;
        }
        case FORM_BEAR:
        case FORM_DIREBEAR:
        {
            if (GetPowerType() != POWER_RAGE)
            {
                SetPowerType(POWER_RAGE);
            }
            break;
        }
        default:                                            // 0, for example
        {
            ChrClassesEntry const* cEntry = sChrClassesStore.LookupEntry(getClass());
            if (cEntry && cEntry->powerType < MAX_POWERS && uint32(GetPowerType()) != cEntry->powerType)
            {
                SetPowerType(Powers(cEntry->powerType));
            }

            break;
        }
    }

    // update auras at form change, ignore this at mods reapply (.reset stats/etc) when form not change.
    if (!reapplyMods)
    {
        UpdateEquipSpellsAtFormChange();
    }

    UpdateAttackPowerAndDamage();
    UpdateAttackPowerAndDamage(true);
}

/**
 * @brief Initializes the player's native and current display identifiers.
 */
void Player::InitDisplayIds()
{
    PlayerInfo const* info = sObjectMgr.GetPlayerInfo(getRace(), getClass());
    if (!info)
    {
        sLog.outError("Player %u has incorrect race/class pair. Can't init display ids.", GetGUIDLow());
        return;
    }

    // reset scale before reapply auras
    SetObjectScale(DEFAULT_OBJECT_SCALE);

    uint8 gender = getGender();
    switch (gender)
    {
        case GENDER_FEMALE:
            SetDisplayId(info->displayId_f);
            SetNativeDisplayId(info->displayId_f);
            break;
        case GENDER_MALE:
            SetDisplayId(info->displayId_m);
            SetNativeDisplayId(info->displayId_m);
            break;
        default:
            sLog.outError("Invalid gender %u for player", gender);
            return;
    }
}

// Return true is the bought item has a max count to force refresh of window by caller
bool Player::BuyItemFromVendor(ObjectGuid vendorGuid, uint32 item, uint8 count, uint8 bag, uint8 slot)
{
    // cheating attempt
    if (count < 1)
    {
        count = 1;
    }

    // cheating attempt
    if (bag != NULL_BAG && bag != INVENTORY_SLOT_BAG_0 && slot > MAX_BAG_SIZE && slot != NULL_SLOT)
    {
        return false;
    }

    if (!IsAlive())
    {
        return false;
    }

    ItemPrototype const* pProto = ObjectMgr::GetItemPrototype(item);
    if (!pProto)
    {
        SendBuyError(BUY_ERR_CANT_FIND_ITEM, NULL, item, 0);
        return false;
    }

    Creature* pCreature = GetNPCIfCanInteractWith(vendorGuid, UNIT_NPC_FLAG_VENDOR);
    if (!pCreature)
    {
        DEBUG_LOG("WORLD: BuyItemFromVendor - %s not found or you can't interact with him.", vendorGuid.GetString().c_str());
        SendBuyError(BUY_ERR_DISTANCE_TOO_FAR, NULL, item, 0);
        return false;
    }

    VendorItemData const* vItems = pCreature->GetVendorItems();
    VendorItemData const* tItems = pCreature->GetVendorTemplateItems();
    if ((!vItems || vItems->Empty()) && (!tItems || tItems->Empty()))
    {
        SendBuyError(BUY_ERR_CANT_FIND_ITEM, pCreature, item, 0);
        return false;
    }

    uint32 vCount = vItems ? vItems->GetItemCount() : 0;
    uint32 tCount = tItems ? tItems->GetItemCount() : 0;

    size_t vendorslot = vItems ? vItems->FindItemSlot(item) : vCount;
    if (vendorslot >= vCount)
    {
        vendorslot = vCount + (tItems ? tItems->FindItemSlot(item) : tCount);
    }

    if (vendorslot >= vCount + tCount)
    {
        SendBuyError(BUY_ERR_CANT_FIND_ITEM, pCreature, item, 0);
        return false;
    }

    VendorItem const* crItem = vendorslot < vCount ? vItems->GetItem(vendorslot) : tItems->GetItem(vendorslot - vCount);
    if (!crItem || crItem->item != item)                    // store diff item (cheating)
    {
        SendBuyError(BUY_ERR_CANT_FIND_ITEM, pCreature, item, 0);
        return false;
    }

    uint32 totalCount = pProto->BuyCount * count;

    // check current item amount if it limited
    if (crItem->maxcount != 0)
    {
        if (pCreature->GetVendorItemCurrentCount(crItem) < totalCount)
        {
            SendBuyError(BUY_ERR_ITEM_ALREADY_SOLD, pCreature, item, 0);
            return false;
        }
    }

    if (uint32(GetReputationRank(pProto->RequiredReputationFaction)) < pProto->RequiredReputationRank)
    {
        SendBuyError(BUY_ERR_REPUTATION_REQUIRE, pCreature, item, 0);
        return false;
    }

    if (uint32 extendedCostId = crItem->ExtendedCost)
    {
        ItemExtendedCostEntry const* iece = sItemExtendedCostStore.LookupEntry(extendedCostId);
        if (!iece)
        {
            sLog.outError("Item %u have wrong ExtendedCost field value %u", pProto->ItemId, extendedCostId);
            return false;
        }

        // honor points price
        if (GetHonorPoints() < (iece->reqhonorpoints * count))
        {
            SendEquipError(EQUIP_ERR_NOT_ENOUGH_HONOR_POINTS, NULL, NULL);
            return false;
        }

        // arena points price
        if (GetArenaPoints() < (iece->reqarenapoints * count))
        {
            SendEquipError(EQUIP_ERR_NOT_ENOUGH_ARENA_POINTS, NULL, NULL);
            return false;
        }

        // item base price
        for (uint8 i = 0; i < MAX_EXTENDED_COST_ITEMS; ++i)
        {
            if (iece->reqitem[i] && !HasItemCount(iece->reqitem[i], iece->reqitemcount[i] * count))
            {
                SendEquipError(EQUIP_ERR_VENDOR_MISSING_TURNINS, NULL, NULL);
                return false;
            }
        }

        // check for personal arena rating requirement
        if (GetMaxPersonalArenaRatingRequirement() < iece->reqpersonalarenarating)
        {
            // probably not the proper equip err
            SendEquipError(EQUIP_ERR_CANT_EQUIP_RANK, NULL, NULL);
            return false;
        }
    }

    if (crItem->conditionId && !isGameMaster() && !sObjectMgr.IsPlayerMeetToCondition(crItem->conditionId, this, pCreature->GetMap(), pCreature, CONDITION_FROM_VENDOR))
    {
        SendBuyError(BUY_ERR_CANT_FIND_ITEM, pCreature, item, 0);
        return false;
    }

    uint32 price = pProto->BuyPrice * count;

    // reputation discount
    price = uint32(floor(price * GetReputationPriceDiscount(pCreature)));

    if (GetMoney() < price)
    {
        SendBuyError(BUY_ERR_NOT_ENOUGHT_MONEY, pCreature, item, 0);
        return false;
    }

    Item* pItem = NULL;

    if ((bag == NULL_BAG && slot == NULL_SLOT) || IsInventoryPos(bag, slot))
    {
        ItemPosCountVec dest;
        InventoryResult msg = CanStoreNewItem(bag, slot, dest, item, totalCount);
        if (msg != EQUIP_ERR_OK)
        {
            SendEquipError(msg, NULL, NULL, item);
            return false;
        }

        ModifyMoney(-int32(price));

        if (crItem->ExtendedCost)
        {
            TakeExtendedCost(crItem->ExtendedCost, count);
        }

        pItem = StoreNewItem(dest, item, true);
    }
    else if (IsEquipmentPos(bag, slot))
    {
        if (totalCount != 1)
        {
            SendEquipError(EQUIP_ERR_ITEM_CANT_BE_EQUIPPED, NULL, NULL);
            return false;
        }

        uint16 dest;
        InventoryResult msg = CanEquipNewItem(slot, dest, item, false);
        if (msg != EQUIP_ERR_OK)
        {
            SendEquipError(msg, NULL, NULL, item);
            return false;
        }

        ModifyMoney(-int32(price));

        if (crItem->ExtendedCost)
        {
            TakeExtendedCost(crItem->ExtendedCost, count);
        }

        pItem = EquipNewItem(dest, item, true);

        if (pItem)
        {
            AutoUnequipOffhandIfNeed();
        }
    }
    else
    {
        SendEquipError(EQUIP_ERR_ITEM_DOESNT_GO_TO_SLOT, NULL, NULL);
        return false;
    }

    if (!pItem)
    {
        return false;
    }

    uint32 new_count = pCreature->UpdateVendorItemCurrentCount(crItem, totalCount);

    WorldPacket data(SMSG_BUY_ITEM, 8 + 4 + 4 + 4);
    data << pCreature->GetObjectGuid();
    data << uint32(vendorslot + 1);                 // numbered from 1 at client
    data << uint32(crItem->maxcount > 0 ? new_count : 0xFFFFFFFF);
    data << uint32(count);
    GetSession()->SendPacket(&data);

    SendNewItem(pItem, totalCount, true, false, false);

    return crItem->maxcount != 0;
}

/**
 * @brief Updates the invalid-instance homebind timer and teleports when it expires.
 *
 * @param time The elapsed update time in milliseconds.
 */
void Player::UpdateHomebindTime(uint32 time)
{
    // GMs never get homebind timer online
    if (m_InstanceValid || isGameMaster())
    {
        if (m_HomebindTimer)                                // instance valid, but timer not reset
        {
            // hide reminder
            WorldPacket data(SMSG_RAID_GROUP_ONLY, 4 + 4);
            data << uint32(0);
            data << uint32(ERR_RAID_GROUP_NONE);            // error used only when timer = 0
            GetSession()->SendPacket(&data);
        }
        // instance is valid, reset homebind timer
        m_HomebindTimer = 0;
    }
    else if (m_HomebindTimer > 0)
    {
        if (time >= m_HomebindTimer)
        {
            // teleport to homebind location
            TeleportTo(m_homebindMapId, m_homebindX, m_homebindY, m_homebindZ, GetOrientation());
        }
        else
        {
            m_HomebindTimer -= time;
        }
    }
    else
    {
        // instance is invalid, start homebind timer
        m_HomebindTimer = 60000;
        // send message to player
        WorldPacket data(SMSG_RAID_GROUP_ONLY, 4 + 4);
        data << uint32(m_HomebindTimer);
        data << uint32(ERR_RAID_GROUP_NONE);                // error used only when timer = 0
        GetSession()->SendPacket(&data);
        DEBUG_LOG("PLAYER: Player '%s' (GUID: %u) will be teleported to homebind in 60 seconds", GetName(), GetGUIDLow());
    }
}

/**
 * @brief Sets or clears the player's PvP state with timeout-aware handling.
 *
 * @param state True to enable PvP; false to disable it.
 * @param ovrride True to bypass the delayed PvP timeout behavior.
 */
void Player::UpdatePvP(bool state, bool ovrride)
{
    if (!state || ovrride)
    {
        SetPvP(state);
        pvpInfo.endTimer = 0;
    }
    else
    {
        if (pvpInfo.endTimer != 0)
        {
            pvpInfo.endTimer = time(NULL);
        }
        else
        {
            SetPvP(state);
        }
    }
}

bool Player::EnchantmentFitsRequirements(uint32 enchantmentcondition, int8 slot)
{
    if (!enchantmentcondition)
    {
        return true;
    }

    SpellItemEnchantmentConditionEntry const* Condition = sSpellItemEnchantmentConditionStore.LookupEntry(enchantmentcondition);

    if (!Condition)
    {
        return true;
    }

    uint8 curcount[4] = {0, 0, 0, 0};

    // counting current equipped gem colors
    for (uint8 i = EQUIPMENT_SLOT_START; i < EQUIPMENT_SLOT_END; ++i)
    {
        if (i == slot)
        {
            continue;
        }
        Item* pItem2 = GetItemByPos(INVENTORY_SLOT_BAG_0, i);
        if (pItem2 && !pItem2->IsBroken() && pItem2->GetProto()->Socket[0].Color)
        {
            for (uint32 enchant_slot = SOCK_ENCHANTMENT_SLOT; enchant_slot < SOCK_ENCHANTMENT_SLOT + 3; ++enchant_slot)
            {
                uint32 enchant_id = pItem2->GetEnchantmentId(EnchantmentSlot(enchant_slot));
                if (!enchant_id)
                {
                    continue;
                }

                SpellItemEnchantmentEntry const* enchantEntry = sSpellItemEnchantmentStore.LookupEntry(enchant_id);
                if (!enchantEntry)
                {
                    continue;
                }

                uint32 gemid = enchantEntry->GemID;
                if (!gemid)
                {
                    continue;
                }

                ItemPrototype const* gemProto = sItemStorage.LookupEntry<ItemPrototype>(gemid);
                if (!gemProto)
                {
                    continue;
                }

                GemPropertiesEntry const* gemProperty = sGemPropertiesStore.LookupEntry(gemProto->GemProperties);
                if (!gemProperty)
                {
                    continue;
                }

                uint8 GemColor = gemProperty->color;

                for (uint8 b = 0, tmpcolormask = 1; b < 4; ++b, tmpcolormask <<= 1)
                {
                    if (tmpcolormask & GemColor)
                    {
                        ++curcount[b];
                    }
                }
            }
        }
    }

    bool activate = true;

    for (int i = 0; i < 5; ++i)
    {
        if (!Condition->Color[i])
        {
            continue;
        }

        uint32 _cur_gem = curcount[Condition->Color[i] - 1];

        // if have <CompareColor> use them as count, else use <value> from Condition
        uint32 _cmp_gem = Condition->CompareColor[i] ? curcount[Condition->CompareColor[i] - 1] : Condition->Value[i];

        switch (Condition->Comparator[i])
        {
            case 2:                                         // requires less <color> than (<value> || <comparecolor>) gems
                activate &= (_cur_gem < _cmp_gem) ? true : false;
                break;
            case 3:                                         // requires more <color> than (<value> || <comparecolor>) gems
                activate &= (_cur_gem > _cmp_gem) ? true : false;
                break;
            case 5:                                         // requires at least <color> than (<value> || <comparecolor>) gems
                activate &= (_cur_gem >= _cmp_gem) ? true : false;
                break;
        }
    }

    DEBUG_LOG("Checking Condition %u, there are %u Meta Gems, %u Red Gems, %u Yellow Gems and %u Blue Gems, Activate:%s", enchantmentcondition, curcount[0], curcount[1], curcount[2], curcount[3], activate ? "yes" : "no");

    return activate;
}

void Player::CorrectMetaGemEnchants(uint8 exceptslot, bool apply)
{
    // cycle all equipped items
    for (uint32 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
    {
        // enchants for the slot being socketed are handled by Player::ApplyItemMods
        if (slot == exceptslot)
        {
            continue;
        }

        Item* pItem = GetItemByPos(INVENTORY_SLOT_BAG_0, slot);

        if (!pItem || !pItem->GetProto()->Socket[0].Color)
        {
            continue;
        }

        for (uint32 enchant_slot = SOCK_ENCHANTMENT_SLOT; enchant_slot < SOCK_ENCHANTMENT_SLOT + 3; ++enchant_slot)
        {
            uint32 enchant_id = pItem->GetEnchantmentId(EnchantmentSlot(enchant_slot));
            if (!enchant_id)
            {
                continue;
            }

            SpellItemEnchantmentEntry const* enchantEntry = sSpellItemEnchantmentStore.LookupEntry(enchant_id);
            if (!enchantEntry)
            {
                continue;
            }

            uint32 condition = enchantEntry->EnchantmentCondition;
            if (condition)
            {
                // was enchant active with/without item?
                bool wasactive = EnchantmentFitsRequirements(condition, apply ? exceptslot : -1);
                // should it now be?
                if (wasactive != EnchantmentFitsRequirements(condition, apply ? -1 : exceptslot))
                {
                    // ignore item gem conditions
                    // if state changed, (dis)apply enchant
                    ApplyEnchantment(pItem, EnchantmentSlot(enchant_slot), !wasactive, true, true);
                }
            }
        }
    }
}

// if false -> then toggled off if was on| if true -> toggled on if was off AND meets requirements
void Player::ToggleMetaGemsActive(uint8 exceptslot, bool apply)
{
    // cycle all equipped items
    for (int slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
    {
        // enchants for the slot being socketed are handled by WorldSession::HandleSocketOpcode(WorldPacket& recv_data)
        if (slot == exceptslot)
        {
            continue;
        }

        Item* pItem = GetItemByPos(INVENTORY_SLOT_BAG_0, slot);

        if (!pItem || !pItem->GetProto()->Socket[0].Color)  // if item has no sockets or no item is equipped go to next item
        {
            continue;
        }

        // cycle all (gem)enchants
        for (uint32 enchant_slot = SOCK_ENCHANTMENT_SLOT; enchant_slot < SOCK_ENCHANTMENT_SLOT + 3; ++enchant_slot)
        {
            uint32 enchant_id = pItem->GetEnchantmentId(EnchantmentSlot(enchant_slot));
            if (!enchant_id)                                // if no enchant go to next enchant(slot)
            {
                continue;
            }

            SpellItemEnchantmentEntry const* enchantEntry = sSpellItemEnchantmentStore.LookupEntry(enchant_id);
            if (!enchantEntry)
            {
                continue;
            }

            // only metagems to be (de)activated, so only enchants with condition
            uint32 condition = enchantEntry->EnchantmentCondition;
            if (condition)
            {
                ApplyEnchantment(pItem, EnchantmentSlot(enchant_slot), apply);
            }
        }
    }
}

/**
 * @brief Stores the location used to return the player after leaving a battleground.
 *
 * @param leader The group leader to mirror entry positioning from, or NULL.
 */
void Player::SetBattleGroundEntryPoint(Player* leader /*= NULL*/)
{
    // chat command use case, or non-group join
    if (!leader || !leader->IsInWorld() || leader->IsTaxiFlying() || leader->GetMap()->IsDungeon() || leader->GetMap()->IsBattleGroundOrArena())
    {
        leader = this;
    }

    if (leader->IsInWorld() && !leader->IsTaxiFlying())
    {
        // If map is dungeon find linked graveyard
        if (leader->GetMap()->IsDungeon())
        {
            if (const WorldSafeLocsEntry* entry = sObjectMgr.GetClosestGraveYard(leader->GetPositionX(), leader->GetPositionY(), leader->GetPositionZ(), leader->GetMapId(), leader->GetTeam()))
            {
                m_bgData.joinPos = WorldLocation(entry->map_id, entry->x, entry->y, entry->z, 0.0f);
                m_bgData.m_needSave = true;
                return;
            }
            else
            {
                sLog.outError("SetBattleGroundEntryPoint: Dungeon map %u has no linked graveyard, setting home location as entry point.", leader->GetMapId());
            }
        }
        // If new entry point is not BG or arena set it
        else if (!leader->GetMap()->IsBattleGroundOrArena())
        {
            m_bgData.joinPos = WorldLocation(leader->GetMapId(), leader->GetPositionX(), leader->GetPositionY(), leader->GetPositionZ(), leader->GetOrientation());
            m_bgData.m_needSave = true;
            return;
        }
    }

    // In error cases use homebind position
    m_bgData.joinPos = WorldLocation(m_homebindMapId, m_homebindX, m_homebindY, m_homebindZ, 0.0f);
    m_bgData.m_needSave = true;
}

/**
 * @brief Removes the player from the battleground and handles deserter penalties.
 *
 * @param teleportToEntryPoint True to teleport the player back to the saved entry point.
 */
void Player::LeaveBattleground(bool teleportToEntryPoint)
{
    if (BattleGround* bg = GetBattleGround())
    {
        bg->RemovePlayerAtLeave(GetObjectGuid(), teleportToEntryPoint, true);

        // call after remove to be sure that player resurrected for correct cast
        if (bg->isBattleGround() && !isGameMaster() && sWorld.getConfig(CONFIG_BOOL_BATTLEGROUND_CAST_DESERTER))
        {
            if (bg->GetStatus() == STATUS_IN_PROGRESS || bg->GetStatus() == STATUS_WAIT_JOIN)
            {
                // lets check if player was teleported from BG and schedule delayed Deserter spell cast
                if (IsBeingTeleportedFar())
                {
                    ScheduleDelayedOperation(DELAYED_SPELL_CAST_DESERTER);
                    return;
                }

                CastSpell(this, 26013, true);               // Deserter
            }
        }
    }
}

/**
 * @brief Checks whether the player may currently join a battleground.
 *
 * @return True if the player can join; otherwise, false.
 */
bool Player::CanJoinToBattleground() const
{
    // check Deserter debuff
    if (GetDummyAura(26013))
    {
        return false;
    }

    return true;
}

bool Player::CanReportAfkDueToLimit()
{
    // a player can complain about 15 people per 5 minutes
    if (m_bgData.bgAfkReportedCount++ >= 15)
    {
        return false;
    }

    return true;
}

/// This player has been blamed to be inactive in a battleground
void Player::ReportedAfkBy(Player* reporter)
{
    BattleGround* bg = GetBattleGround();
    if (!bg || bg != reporter->GetBattleGround() || GetTeam() != reporter->GetTeam())
    {
        return;
    }

    // check if player has 'Idle' or 'Inactive' debuff
    if (m_bgData.bgAfkReporter.find(reporter->GetGUIDLow()) == m_bgData.bgAfkReporter.end() && !HasAura(43680, EFFECT_INDEX_0) && !HasAura(43681, EFFECT_INDEX_0) && reporter->CanReportAfkDueToLimit())
    {
        m_bgData.bgAfkReporter.insert(reporter->GetGUIDLow());
        // 3 players have to complain to apply debuff
        if (m_bgData.bgAfkReporter.size() >= 3)
        {
            // cast 'Idle' spell
            CastSpell(this, 43680, true);
            m_bgData.bgAfkReporter.clear();
        }
    }
}

/**
 * @brief Initializes the number of primary professions the player may learn.
 */
void Player::InitPrimaryProfessions()
{
    uint32 maxProfs = GetSession()->GetSecurity() < AccountTypes(sWorld.getConfig(CONFIG_UINT32_TRADE_SKILL_GMIGNORE_MAX_PRIMARY_COUNT))
                      ? sWorld.getConfig(CONFIG_UINT32_MAX_PRIMARY_TRADE_SKILL) : 10;
    SetFreePrimaryProfessions(maxProfs);
}

/**
 * @brief Sends the current combo point target and value to the client fields.
 */
void Player::SendComboPoints()
{
    Unit* combotarget = sObjectAccessor.GetUnit(*this, m_comboTargetGuid);
    if (combotarget)
    {
        WorldPacket data(SMSG_UPDATE_COMBO_POINTS, combotarget->GetPackGUID().size() + 1);
        data << combotarget->GetPackGUID();
        data << uint8(m_comboPoints);
        GetSession()->SendPacket(&data);
    }
    /*else
    {
        // can be NULL, and then points=0. Use unknown; to reset points of some sort?
        data << PackedGuid();
        data << uint8(0);
        GetSession()->SendPacket(&data);
    }*/
}

/**
 * @brief Adds combo points on a target and updates combo point ownership.
 *
 * @param target The unit receiving combo points.
 * @param count The number of combo points to add.
 */
void Player::AddComboPoints(Unit* target, int8 count)
{
    if (!count)
    {
        return;
    }

    // without combo points lost (duration checked in aura)
    RemoveSpellsCausingAura(SPELL_AURA_RETAIN_COMBO_POINTS);

    if (target->GetObjectGuid() == m_comboTargetGuid)
    {
        m_comboPoints += count;
    }
    else
    {
        if (m_comboTargetGuid)
            if (Unit* target2 = sObjectAccessor.GetUnit(*this, m_comboTargetGuid))
            {
                target2->RemoveComboPointHolder(GetGUIDLow());
            }

        m_comboTargetGuid = target->GetObjectGuid();
        m_comboPoints = count;

        target->AddComboPointHolder(GetGUIDLow());
    }

    if (m_comboPoints > 5)
    {
        m_comboPoints = 5;
    }
    if (m_comboPoints < 0)
    {
        m_comboPoints = 0;
    }

    SendComboPoints();
}

/**
 * @brief Clears the player's combo points and current combo target.
 */
void Player::ClearComboPoints()
{
    if (!m_comboTargetGuid)
    {
        return;
    }

    // without combopoints lost (duration checked in aura)
    RemoveSpellsCausingAura(SPELL_AURA_RETAIN_COMBO_POINTS);

    m_comboPoints = 0;

    SendComboPoints();

    if (Unit* target = sObjectAccessor.GetUnit(*this, m_comboTargetGuid))
    {
        target->RemoveComboPointHolder(GetGUIDLow());
    }

    m_comboTargetGuid.Clear();
}

/**
 * @brief Assigns the player to a group and subgroup.
 *
 * @param group The group to join, or NULL to clear membership.
 * @param subgroup The subgroup index when joining a group.
 */
void Player::SetGroup(Group* group, int8 subgroup)
{
    if (group == NULL)
    {
        m_group.unlink();
    }
    else
    {
        // never use SetGroup without a subgroup unless you specify NULL for group
        MANGOS_ASSERT(subgroup >= 0);
        m_group.link(group, this);
        m_group.setSubGroup((uint8)subgroup);
    }
}

/* Called by WorldSession::HandlePlayerLogin */
void Player::SendInitialPacketsBeforeAddToMap()
{
    /* This packet seems useless...
     * TODO: Work out if we need SMSG_SET_REST_START */
    WorldPacket data(SMSG_SET_REST_START, 4);
    data << uint32(0);                                      // unknown, may be rest state time or experience
    GetSession()->SendPacket(&data);

    /* Send information about player's home binding */
    data.Initialize(SMSG_BINDPOINTUPDATE, 5 * 4);
    data << m_homebindX << m_homebindY << m_homebindZ;
    data << (uint32) m_homebindMapId;
    data << (uint32) m_homebindAreaId;
    GetSession()->SendPacket(&data);

    /* Tutorial data */
    GetSession()->SendTutorialsData();
    SendInitialSpells();

    data.Initialize(SMSG_SEND_UNLEARN_SPELLS, 4);
    data << uint32(0);                                      // count, for(count) uint32;
    GetSession()->SendPacket(&data);

    SendInitialActionButtons();

    /* Send player reputations */
    m_reputationMgr.SendInitialReputations();

    // SMSG_SET_AURA_SINGLE

    const float game_time = 0.01666667f; // Game speed

    data.Initialize(SMSG_LOGIN_SETTIMESPEED, 4 + 4);
    data << uint32(secsToTimeBitFields(sWorld.GetGameTime()));
    data << game_time; // Float is 4 bytes here
    GetSession()->SendPacket(&data);

    // set fly flag if in fly form or taxi flight to prevent visually drop at ground in showup moment
    if (IsFreeFlying() || IsTaxiFlying())
    {
        m_movementInfo.AddMovementFlag(MOVEFLAG_FLYING);
    }

    /* Finally, set the player as the active mover */
    SetMover(this);
}

/**
 * @brief Sends map-dependent initialization packets after the player is added to the world.
 */
void Player::SendInitialPacketsAfterAddToMap()
{
    /* Update players zone */
    uint32 newzone, newarea;
    GetZoneAndAreaId(newzone, newarea);
    UpdateZone(newzone, newarea);                           // This calls SendInitWorldStates

    ResetTimeSync();
    SendTimeSync();

    CastSpell(this, 836, true);                             // LOGINEFFECT

    /* Sets aura effects that need to be sent after the player is added to the map
     * We use SendMessageToSet so that it's sent to everyone, including the player
     * Some auras lose their state on long teleports, we should reapply them in this case also */
    static const AuraType auratypes[] =
    {
        SPELL_AURA_MOD_FEAR,     SPELL_AURA_TRANSFORM,                 SPELL_AURA_WATER_WALK,
        SPELL_AURA_FEATHER_FALL, SPELL_AURA_HOVER,                     SPELL_AURA_SAFE_FALL,
        SPELL_AURA_FLY,          SPELL_AURA_MOD_FLIGHT_SPEED_MOUNTED,  SPELL_AURA_NONE
    };

    /* For each aura type */
    for (AuraType const* itr = &auratypes[0]; itr && itr[0] != SPELL_AURA_NONE; ++itr)
    {
        /* Populate iterator with player's auras */
        Unit::AuraList const& auraList = GetAurasByType(*itr);

        /* If the list isn't empty, re-apply the ones we found */
        if (!auraList.empty())
        {
            auraList.front()->ApplyModifier(true, true);
        }
    }

    /* If the player is marked as stunned, root them */
    if (HasAuraType(SPELL_AURA_MOD_STUN) || HasAuraType(SPELL_AURA_MOD_ROOT))
    {
        SetRoot(true);
    }

    /* Must be called after loading the map */
    SendEnchantmentDurations();
    SendItemDurations();
}

/**
 * @brief Flushes pending group update data to out-of-range group members.
 */
void Player::SendUpdateToOutOfRangeGroupMembers()
{
    if (m_groupUpdateMask == GROUP_UPDATE_FLAG_NONE)
    {
        return;
    }
    if (Group* group = GetGroup())
    {
        group->UpdatePlayerOutOfRange(this);
    }

    m_groupUpdateMask = GROUP_UPDATE_FLAG_NONE;
    m_auraUpdateMask = 0;
    if (Pet* pet = GetPet())
    {
        pet->ResetAuraUpdateMask();
    }
}

/**
 * @brief Sends the appropriate transfer-aborted feedback for an area lock failure.
 *
 * @param mapEntry The destination map entry.
 * @param at The triggering area trigger, if any.
 * @param lockStatus The evaluated area lock status.
 * @param miscRequirement Extra requirement data used by some messages.
 */
void Player::SendTransferAbortedByLockStatus(MapEntry const* mapEntry, AreaLockStatus lockStatus, uint32 miscRequirement)
{
    MANGOS_ASSERT(mapEntry);

    DEBUG_LOG("SendTransferAbortedByLockStatus: Called for %s on map %u, LockAreaStatus %u, miscRequirement %u)", GetGuidStr().c_str(), mapEntry->MapID, lockStatus, miscRequirement);

    switch (lockStatus)
    {
        case AREA_LOCKSTATUS_TOO_LOW_LEVEL:
            GetSession()->SendAreaTriggerMessage(GetSession()->GetMangosString(LANG_LEVEL_MINREQUIRED), miscRequirement);
            break;
        case AREA_LOCKSTATUS_ZONE_IN_COMBAT:
            GetSession()->SendTransferAborted(mapEntry->MapID, TRANSFER_ABORT_ZONE_IN_COMBAT);
            break;
        case AREA_LOCKSTATUS_INSTANCE_IS_FULL:
            GetSession()->SendTransferAborted(mapEntry->MapID, TRANSFER_ABORT_MAX_PLAYERS);
            break;
        case AREA_LOCKSTATUS_QUEST_NOT_COMPLETED:
            if (mapEntry->MapID == 269)                     // Exception for Black Morass
            {
                GetSession()->SendAreaTriggerMessage("%s", GetSession()->GetMangosString(LANG_TELEREQ_QUEST_BLACK_MORASS));
                break;
            }
            else if (mapEntry->IsContinent())               // do not report anything for quest areatrigge
            {
                DEBUG_LOG("SendTransferAbortedByLockStatus: LockAreaStatus %u, do not teleport, no message sent (mapId %u)", lockStatus, mapEntry->MapID);
                break;
            }
            // No break here!
            [[fallthrough]];
        case AREA_LOCKSTATUS_MISSING_ITEM:
            GetSession()->SendTransferAborted(mapEntry->MapID, TRANSFER_ABORT_DIFFICULTY, GetDifficulty());
            break;
        case AREA_LOCKSTATUS_MISSING_DIFFICULTY:
        {
            Difficulty difficulty = GetDifficulty();
            GetSession()->SendTransferAborted(mapEntry->MapID, TRANSFER_ABORT_DIFFICULTY, difficulty);
            break;
        }
        case AREA_LOCKSTATUS_INSUFFICIENT_EXPANSION:
            GetSession()->SendTransferAborted(mapEntry->MapID, TRANSFER_ABORT_INSUF_EXPAN_LVL, miscRequirement);
            if (sObjectMgr.GetMapEntranceTrigger(mapEntry->MapID))
            {
                GetSession()->SendAreaTriggerMessage(GetSession()->GetMangosString(LANG_LEVEL_MINREQUIRED_AND_ITEM), sObjectMgr.GetItemPrototype(miscRequirement)->Name1);
            }
            break;
        case AREA_LOCKSTATUS_NOT_ALLOWED:
        case AREA_LOCKSTATUS_RAID_LOCKED:
        case AREA_LOCKSTATUS_UNKNOWN_ERROR:
            // ToDo: SendAreaTriggerMessage or Transfer Abort for these cases!
            break;
        case AREA_LOCKSTATUS_OK:
            sLog.outError("SendTransferAbortedByLockStatus: LockAreaStatus AREA_LOCKSTATUS_OK received for %s (mapId %u)", GetGuidStr().c_str(), mapEntry->MapID);
            MANGOS_ASSERT(false);
            break;
        default:
            sLog.outError("SendTransfertAbortedByLockstatus: unhandled LockAreaStatus %u, when %s attempts to enter in map %u", lockStatus, GetGuidStr().c_str(), mapEntry->MapID);
            break;
    }
}

/**
 * @brief Sends an instance reset warning message to the client.
 *
 * @param mapid The map identifier being reset.
 * @param time The remaining time until reset in seconds.
 */
void Player::SendInstanceResetWarning(uint32 mapid, uint32 time)
{
    // type of warning, based on the time remaining until reset
    uint32 type;
    if (time > 3600)
    {
        type = RAID_INSTANCE_WELCOME;
    }
    else if (time > 900 && time <= 3600)
    {
        type = RAID_INSTANCE_WARNING_HOURS;
    }
    else if (time > 300 && time <= 900)
    {
        type = RAID_INSTANCE_WARNING_MIN;
    }
    else
    {
        type = RAID_INSTANCE_WARNING_MIN_SOON;
    }

    WorldPacket data(SMSG_RAID_INSTANCE_MESSAGE, 4 + 4 + 4);
    data << uint32(type);
    data << uint32(mapid);
    data << uint32(time);
    GetSession()->SendPacket(&data);
}

/**
 * @brief Applies the default equip cooldown for item use spells.
 *
 * @param pItem The item whose on-use spells should receive equip cooldowns.
 */
void Player::ApplyEquipCooldown(Item* pItem)
{
    if (pItem->GetProto()->Flags & ITEM_FLAG_NO_EQUIP_COOLDOWN)
    {
        return;
    }

    for (int i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
    {
        _Spell const& spellData = pItem->GetProto()->Spells[i];

        // no spell
        if (!spellData.SpellId)
        {
            continue;
        }

        // wrong triggering type (note: ITEM_SPELLTRIGGER_ON_NO_DELAY_USE not have cooldown)
        if (spellData.SpellTrigger != ITEM_SPELLTRIGGER_ON_USE)
        {
            continue;
        }

        AddSpellCooldown(spellData.SpellId, pItem->GetEntry(), time(NULL) + 30);

        WorldPacket data(SMSG_ITEM_COOLDOWN, 12);
        data << ObjectGuid(pItem->GetObjectGuid());
        data << uint32(spellData.SpellId);
        GetSession()->SendPacket(&data);
    }
}

/**
 * @brief Resets the player's learned spells and restores default and quest rewards.
 */
void Player::resetSpells()
{
    // not need after this call
    if (HasAtLoginFlag(AT_LOGIN_RESET_SPELLS))
    {
        RemoveAtLoginFlag(AT_LOGIN_RESET_SPELLS, true);
    }

    // make full copy of map (spells removed and marked as deleted at another spell remove
    // and we can't use original map for safe iterative with visit each spell at loop end
    PlayerSpellMap smap = GetSpellMap();

    for (PlayerSpellMap::const_iterator iter = smap.begin(); iter != smap.end(); ++iter)
    {
        removeSpell(iter->first, false, false); // only iter->first can be accessed, object by iter->second can be deleted already
    }

    learnDefaultSpells();
    learnQuestRewardedSpells();
}

/**
 * @brief Teaches the player's default race and class spells.
 */
void Player::learnDefaultSpells()
{
    // learn default race/class spells
    PlayerInfo const* info = sObjectMgr.GetPlayerInfo(getRace(), getClass());
    for (PlayerCreateInfoSpells::const_iterator itr = info->spell.begin(); itr != info->spell.end(); ++itr)
    {
        uint32 tspell = *itr;
        DEBUG_LOG("PLAYER (Class: %u Race: %u): Adding initial spell, id = %u", uint32(getClass()), uint32(getRace()), tspell);
        if (!IsInWorld())                                   // will send in INITIAL_SPELLS in list anyway at map add
        {
            addSpell(tspell, true, true, true, false);
        }
        else                                                // but send in normal spell in game learn case
        {
            learnSpell(tspell, true);
        }
    }
}

/**
 * @brief Teaches a quest reward spell if the quest grants a learnable spell.
 *
 * @param quest The rewarded quest to inspect.
 */
void Player::learnQuestRewardedSpells(Quest const* quest)
{
    uint32 spell_id = quest->GetRewSpellCast();

    // skip quests without rewarded spell
    if (!spell_id)
    {
        return;
    }

    SpellEntry const* spellInfo = sSpellStore.LookupEntry(spell_id);
    if (!spellInfo)
    {
        return;
    }

    // check learned spells state
    bool found = false;
    for (int i = 0; i < MAX_EFFECT_INDEX; ++i)
    {
        if (spellInfo->Effect[i] == SPELL_EFFECT_LEARN_SPELL && !HasSpell(spellInfo->EffectTriggerSpell[i]))
        {
            found = true;
            break;
        }
    }

    // skip quests with not teaching spell or already known spell
    if (!found)
    {
        return;
    }

    // prevent learn non first rank unknown profession and second specialization for same profession)
    uint32 learned_0 = spellInfo->EffectTriggerSpell[EFFECT_INDEX_0];
    if (sSpellMgr.GetSpellRank(learned_0) > 1 && !HasSpell(learned_0))
    {
        // not have first rank learned (unlearned prof?)
        uint32 first_spell = sSpellMgr.GetFirstSpellInChain(learned_0);
        if (!HasSpell(first_spell))
        {
            return;
        }

        SpellEntry const* learnedInfo = sSpellStore.LookupEntry(learned_0);
        if (!learnedInfo)
        {
            return;
        }

        // specialization
        if (learnedInfo->Effect[EFFECT_INDEX_0] == SPELL_EFFECT_TRADE_SKILL && learnedInfo->Effect[EFFECT_INDEX_1] == 0)
        {
            // search other specialization for same prof
            for (PlayerSpellMap::const_iterator itr = m_spells.begin(); itr != m_spells.end(); ++itr)
            {
                if (itr->second.state == PLAYERSPELL_REMOVED || itr->first == learned_0)
                {
                    continue;
                }

                SpellEntry const* itrInfo = sSpellStore.LookupEntry(itr->first);
                if (!itrInfo)
                {
                    return;
                }

                // compare only specializations
                if (itrInfo->Effect[EFFECT_INDEX_0] != SPELL_EFFECT_TRADE_SKILL || itrInfo->Effect[EFFECT_INDEX_1] != 0)
                {
                    continue;
                }

                // compare same chain spells
                if (sSpellMgr.GetFirstSpellInChain(itr->first) != first_spell)
                {
                    continue;
                }

                // now we have 2 specialization, learn possible only if found is lesser specialization rank
                if (!sSpellMgr.IsHighRankOfSpell(learned_0, itr->first))
                {
                    return;
                }
            }
        }
    }

    CastSpell(this, spell_id, true);
}

/**
 * @brief Reapplies all quest reward spells from previously rewarded quests.
 */
void Player::learnQuestRewardedSpells()
{
    // learn spells received from quest completing
    for (QuestStatusMap::const_iterator itr = mQuestStatus.begin(); itr != mQuestStatus.end(); ++itr)
    {
        // skip no rewarded quests
        if (!itr->second.m_rewarded)
        {
            continue;
        }

        Quest const* quest = sObjectMgr.GetQuestTemplate(itr->first);
        if (!quest)
        {
            continue;
        }

        learnQuestRewardedSpells(quest);
    }
}

/**
 * @brief Learns or removes spells unlocked by a profession or skill value.
 *
 * @param skill_id The skill line identifier.
 * @param skill_value The current skill value.
 */
void Player::learnSkillRewardedSpells(uint32 skill_id, uint32 skill_value)
{
    uint32 raceMask  = getRaceMask();
    uint32 classMask = getClassMask();
    for (uint32 j = 0; j < sSkillLineAbilityStore.GetNumRows(); ++j)
    {
        SkillLineAbilityEntry const* pAbility = sSkillLineAbilityStore.LookupEntry(j);
        if (!pAbility || pAbility->skillId != skill_id || pAbility->learnOnGetSkill != ABILITY_LEARNED_ON_GET_PROFESSION_SKILL)
        {
            continue;
        }
        // Check race if set
        if (pAbility->racemask && !(pAbility->racemask & raceMask))
        {
            continue;
        }
        // Check class if set
        if (pAbility->classmask && !(pAbility->classmask & classMask))
        {
            continue;
        }

        if (sSpellStore.LookupEntry(pAbility->spellId))
        {
            // need unlearn spell
            if (skill_value < pAbility->req_skill_value)
            {
                removeSpell(pAbility->spellId);
            }
            // need learn
            else if (!IsInWorld())
            {
                addSpell(pAbility->spellId, true, true, true, false);
            }
            else
            {
                learnSpell(pAbility->spellId, true);
            }
        }
    }
}

/**
 * @brief Sends visible aura duration updates for a target to the player.
 *
 * @param target The unit whose aura durations should be sent.
 */
void Player::SendAuraDurationsForTarget(Unit* target)
{
    SpellAuraHolderMap const& auraHolders = target->GetSpellAuraHolderMap();
    for (SpellAuraHolderMap::const_iterator itr = auraHolders.begin(); itr != auraHolders.end(); ++itr)
    {
        SpellAuraHolder* holder = itr->second;

        if (holder->GetAuraSlot() >= MAX_AURAS || holder->IsPassive() || holder->GetCasterGuid() != GetObjectGuid())
        {
            continue;
        }

        holder->SendAuraDurationForCaster(this);
    }
}

void Player::SetDailyQuestStatus(uint32 quest_id)
{
    for (uint32 quest_daily_idx = 0; quest_daily_idx < PLAYER_MAX_DAILY_QUESTS; ++quest_daily_idx)
    {
        if (!GetUInt32Value(PLAYER_FIELD_DAILY_QUESTS_1 + quest_daily_idx))
        {
            SetUInt32Value(PLAYER_FIELD_DAILY_QUESTS_1 + quest_daily_idx, quest_id);
            m_DailyQuestChanged = true;
            break;
        }
    }
}

void Player::ResetDailyQuestStatus()
{
    for (uint32 quest_daily_idx = 0; quest_daily_idx < PLAYER_MAX_DAILY_QUESTS; ++quest_daily_idx)
    {
        SetUInt32Value(PLAYER_FIELD_DAILY_QUESTS_1 + quest_daily_idx, 0);
    }

    // DB data deleted in caller
    m_DailyQuestChanged = false;
}

/**
 * @brief Gets the battleground instance the player is currently associated with.
 *
 * @return The active battleground instance, or NULL if none exists.
 */
BattleGround* Player::GetBattleGround() const
{
    if (GetBattleGroundId() == 0)
    {
        return NULL;
    }

    return sBattleGroundMgr.GetBattleGround(GetBattleGroundId(), m_bgData.bgTypeID);
}

bool Player::InArena() const
{
    BattleGround* bg = GetBattleGround();
    if (!bg || !bg->isArena())
    {
        return false;
    }

    return true;
}

/**
 * @brief Checks whether the player's level fits a battleground's allowed range.
 *
 * @param bgTypeId The battleground type to evaluate.
 * @return True if the player's level is within range; otherwise, false.
 */
bool Player::GetBGAccessByLevel(BattleGroundTypeId bgTypeId) const
{
    // get a template bg instead of running one
    BattleGround* bg = sBattleGroundMgr.GetBattleGroundTemplate(bgTypeId);
    if (!bg)
    {
        return false;
    }

    if (getLevel() < bg->GetMinLevel() || getLevel() > bg->GetMaxLevel())
    {
        return false;
    }

    return true;
}

/**
 * @brief Gets the minimum level for a battleground bracket.
 *
 * @param bracket_id The battleground bracket identifier.
 * @param bgTypeId The battleground type.
 * @return The minimum level for the bracket.
 */
uint32 Player::GetMinLevelForBattleGroundBracketId(BattleGroundBracketId bracket_id, BattleGroundTypeId bgTypeId)
{
    if (bracket_id < 1)
    {
        return 0;
    }

    if (bracket_id > BG_BRACKET_ID_LAST)
    {
        bracket_id = BG_BRACKET_ID_LAST;
    }

    BattleGround* bg = sBattleGroundMgr.GetBattleGroundTemplate(bgTypeId);
    assert(bg);
    return 10 * bracket_id + bg->GetMinLevel();
}

/**
 * @brief Gets the maximum level for a battleground bracket.
 *
 * @param bracket_id The battleground bracket identifier.
 * @param bgTypeId The battleground type.
 * @return The maximum level for the bracket.
 */
uint32 Player::GetMaxLevelForBattleGroundBracketId(BattleGroundBracketId bracket_id, BattleGroundTypeId bgTypeId)
{
    if (bracket_id >= BG_BRACKET_ID_LAST)
    {
        return 255; // hardcoded max level
    }

    return GetMinLevelForBattleGroundBracketId(bracket_id, bgTypeId) + 10;
}

/**
 * @brief Determines the battleground bracket that matches the player's level.
 *
 * @param bgTypeId The battleground type.
 * @return The matching battleground bracket identifier.
 */
BattleGroundBracketId Player::GetBattleGroundBracketIdFromLevel(BattleGroundTypeId bgTypeId) const
{
    BattleGround* bg = sBattleGroundMgr.GetBattleGroundTemplate(bgTypeId);
    assert(bg);
    if (getLevel() < bg->GetMinLevel())
    {
        return BG_BRACKET_ID_FIRST;
    }

    uint32 bracket_id = (getLevel() - bg->GetMinLevel()) / 10;
    if (bracket_id > MAX_BATTLEGROUND_BRACKETS)
    {
        return BG_BRACKET_ID_LAST;
    }

    return BattleGroundBracketId(bracket_id);
}

/**
 * @brief Calculates the vendor price discount earned from reputation and rank.
 *
 * @param pCreature The vendor creature.
 * @return The price multiplier applied to vendor costs.
 */
float Player::GetReputationPriceDiscount(Creature const* pCreature) const
{
    FactionTemplateEntry const* vendor_faction = pCreature->getFactionTemplateEntry();
    if (!vendor_faction || !vendor_faction->faction)
    {
        return 1.0f;
    }

    ReputationRank rank = GetReputationRank(vendor_faction->faction);   // get repution rank for that specific vendor faction
    if (rank <= REP_NEUTRAL)
    {
        return 1.0f;
    }

    return 1.0f - 0.05f * (rank - REP_NEUTRAL);
}

/**
 * Check spell availability for training base at SkillLineAbility/SkillRaceClassInfo data.
 * Checked allowed race/class and dependent from race/class allowed min level
 *
 * @param spell_id  checked spell id
 * @param pReqlevel if arg provided then function work in view mode (level check not applied but detected minlevel returned to var by arg pointer.
                    if arg not provided then considered train action mode and level checked
 * @return          true if spell available for show in trainer list (with skip level check) or training.
 */
bool Player::IsSpellFitByClassAndRace(uint32 spell_id, uint32* pReqlevel /*= NULL*/) const
{
    uint32 racemask  = getRaceMask();
    uint32 classmask = getClassMask();

    SkillLineAbilityMapBounds bounds = sSpellMgr.GetSkillLineAbilityMapBounds(spell_id);
    if (bounds.first == bounds.second)
    {
        return true;
    }

    for (SkillLineAbilityMap::const_iterator _spell_idx = bounds.first; _spell_idx != bounds.second; ++_spell_idx)
    {
        SkillLineAbilityEntry const* abilityEntry = _spell_idx->second;
        // skip wrong race skills
        if (abilityEntry->racemask && (abilityEntry->racemask & racemask) == 0)
        {
            continue;
        }

        // skip wrong class skills
        if (abilityEntry->classmask && (abilityEntry->classmask & classmask) == 0)
        {
            continue;
        }

        SkillRaceClassInfoMapBounds raceBounds = sSpellMgr.GetSkillRaceClassInfoMapBounds(abilityEntry->skillId);
        for (SkillRaceClassInfoMap::const_iterator itr = raceBounds.first; itr != raceBounds.second; ++itr)
        {
            SkillRaceClassInfoEntry const* skillRCEntry = itr->second;
            if ((skillRCEntry->raceMask & racemask) && (skillRCEntry->classMask & classmask))
            {
                if (skillRCEntry->flags & ABILITY_SKILL_NONTRAINABLE)
                {
                    return false;
                }

                if (pReqlevel)                              // show trainers list case
                {
                    if (skillRCEntry->reqLevel)
                    {
                        *pReqlevel = skillRCEntry->reqLevel;
                        return true;
                    }
                }
                else                                        // check availble case at train
                {
                    if (skillRCEntry->reqLevel && getLevel() < skillRCEntry->reqLevel)
                    {
                        return false;
                    }
                }
            }
        }

        return true;
    }

    return false;
}

/**
 * @brief Accepts or declines a pending summon and teleports when valid.
 *
 * @param agree True to accept the summon; false to decline it.
 */
void Player::SummonIfPossible(bool agree)
{
    if (!agree)
    {
        m_summon_expire = 0;
        return;
    }

    // expire and auto declined
    if (m_summon_expire < time(NULL))
    {
        return;
    }

    // stop taxi flight at summon
    if (IsTaxiFlying())
    {
        GetMotionMaster()->MovementExpired();
        m_taxi.ClearTaxiDestinations();
    }

    // drop flag at summon
    // this code can be reached only when GM is summoning player who carries flag, because player should be immune to summoning spells when he carries flag
    if (BattleGround* bg = GetBattleGround())
    {
        bg->EventPlayerDroppedFlag(this);
    }

    m_summon_expire = 0;

    TeleportTo(m_summon_mapid, m_summon_x, m_summon_y, m_summon_z, GetOrientation());
}

/**
 * @brief Removes an item from the list of items with active duration tracking.
 *
 * @param item The item to stop tracking.
 */
void Player::RemoveItemDurations(Item* item)
{
    for (ItemDurationList::iterator itr = m_itemDuration.begin(); itr != m_itemDuration.end(); ++itr)
    {
        if (*itr == item)
        {
            m_itemDuration.erase(itr);
            break;
        }
    }
}

/**
 * @brief Adds an item to the list of items with active duration tracking.
 *
 * @param item The item to track.
 */
void Player::AddItemDurations(Item* item)
{
    if (item->GetUInt32Value(ITEM_FIELD_DURATION))
    {
        m_itemDuration.push_back(item);
        item->SendTimeUpdate(this);
    }
}

/**
 * @brief Automatically unequips the offhand item when a two-handed weapon requires it.
 */
void Player::AutoUnequipOffhandIfNeed()
{
    Item* offItem = GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND);
    if (!offItem)
    {
        return;
    }

    ItemPrototype const* itemProto = offItem->GetProto();

    // need unequip offhand for 2h-weapon
    if ((CanDualWield() || itemProto->InventoryType == INVTYPE_SHIELD || itemProto->InventoryType == INVTYPE_HOLDABLE) &&
            !IsTwoHandUsed())
            {
                return;
            }

    ItemPosCountVec off_dest;
    uint8 off_msg = CanStoreItem(NULL_BAG, NULL_SLOT, off_dest, offItem, false);
    if (off_msg == EQUIP_ERR_OK)
    {
        RemoveItem(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND, true);
        StoreItem(off_dest, offItem, true);
    }
    else
    {
        MoveItemFromInventory(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND, true);
        CharacterDatabase.BeginTransaction();
        offItem->DeleteFromInventoryDB();                   // deletes item from character's inventory
        offItem->SaveToDB();                                // recursive and not have transaction guard into self, item not in inventory and can be save standalone
        CharacterDatabase.CommitTransaction();

        std::string subject = GetSession()->GetMangosString(LANG_NOT_EQUIPPED_ITEM);
        MailDraft(subject, "There's were problems with equipping this item.").AddItem(offItem).SendMailTo(this, MailSender(this, MAIL_STATIONERY_GM), MAIL_CHECK_MASK_COPIED);
    }
}

/**
 * @brief Checks whether the player has an equipped item that satisfies a spell requirement.
 *
 * @param spellInfo The spell entry defining the equipment requirement.
 * @param ignoreItem An equipped item to ignore during the search.
 * @return True if a valid item is equipped; otherwise, false.
 */
bool Player::HasItemFitToSpellReqirements(SpellEntry const* spellInfo, Item const* ignoreItem)
{
    if (spellInfo->EquippedItemClass < 0)
    {
        return true;
    }

    // scan other equipped items for same requirements (mostly 2 daggers/etc)
    // for optimize check 2 used cases only
    switch (spellInfo->EquippedItemClass)
    {
        case ITEM_CLASS_WEAPON:
        {
            for (int i = EQUIPMENT_SLOT_MAINHAND; i < EQUIPMENT_SLOT_TABARD; ++i)
            {
                if (Item* item = GetItemByPos(INVENTORY_SLOT_BAG_0, i))
                    if (item != ignoreItem && item->IsFitToSpellRequirements(spellInfo))
                    {
                        return true;
                    }
            }
            break;
        }
        case ITEM_CLASS_ARMOR:
        {
            // tabard not have dependent spells
            for (int i = EQUIPMENT_SLOT_START; i < EQUIPMENT_SLOT_MAINHAND; ++i)
            {
                if (Item* item = GetItemByPos(INVENTORY_SLOT_BAG_0, i))
                    if (item != ignoreItem && item->IsFitToSpellRequirements(spellInfo))
                    {
                        return true;
                    }
            }

            // shields can be equipped to offhand slot
            if (Item* item = GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND))
                if (item != ignoreItem && item->IsFitToSpellRequirements(spellInfo))
                {
                    return true;
                }

            // ranged slot can have some armor subclasses
            if (Item* item = GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_RANGED))
                if (item != ignoreItem && item->IsFitToSpellRequirements(spellInfo))
                {
                    return true;
                }

            break;
        }
        default:
            sLog.outError("HasItemFitToSpellReqirements: Not handled spell requirement for item class %u", spellInfo->EquippedItemClass);
            break;
    }

    return false;
}

/**
 * @brief Checks whether the player may cast a spell without consuming reagents.
 *
 * @param spellInfo The spell being cast.
 * @return True if reagents can be ignored; otherwise, false.
 */
bool Player::CanNoReagentCast(SpellEntry const* spellInfo) const
{
    // don't take reagents for spells with SPELL_ATTR_EX5_NO_REAGENT_WHILE_PREP
    if (spellInfo->HasAttribute(SPELL_ATTR_EX5_NO_REAGENT_WHILE_PREP) && HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PREPARATION))
    {
        return true;
    }

    return false;
}

/**
 * @brief Removes auras and interrupts casts that depend on a removed item.
 *
 * @param pItem The item being removed or invalidated.
 */
void Player::RemoveItemDependentAurasAndCasts(Item* pItem)
{
    SpellAuraHolderMap& auras = GetSpellAuraHolderMap();
    for (SpellAuraHolderMap::const_iterator itr = auras.begin(); itr != auras.end();)
    {
        SpellAuraHolder* holder = itr->second;

        // skip passive (passive item dependent spells work in another way) and not self applied auras
        SpellEntry const* spellInfo = holder->GetSpellProto();
        if (holder->IsPassive() ||  holder->GetCasterGuid() != GetObjectGuid())
        {
            ++itr;
            continue;
        }

        // skip if not item dependent or have alternative item
        if (HasItemFitToSpellReqirements(spellInfo, pItem))
        {
            ++itr;
            continue;
        }

        // no alt item, remove aura, restart check
        RemoveAurasDueToSpell(holder->GetId());
        itr = auras.begin();
    }

    // currently casted spells can be dependent from item
    for (uint32 i = 0; i < CURRENT_MAX_SPELL; ++i)
    {
        if (Spell* spell = GetCurrentSpell(CurrentSpellTypes(i)))
            if (spell->getState() != SPELL_STATE_DELAYED && !HasItemFitToSpellReqirements(spell->m_spellInfo, pItem))
            {
                InterruptSpell(CurrentSpellTypes(i));
            }
    }
}

/**
 * @brief Chooses the resurrection spell currently available to the player.
 *
 * @return The resurrection spell identifier, or 0 if none is available.
 */
uint32 Player::GetResurrectionSpellId()
{
    // search priceless resurrection possibilities
    uint32 prio = 0;
    uint32 spell_id = 0;
    AuraList const& dummyAuras = GetAurasByType(SPELL_AURA_DUMMY);
    for (AuraList::const_iterator itr = dummyAuras.begin(); itr != dummyAuras.end(); ++itr)
    {
        // Soulstone Resurrection                           // prio: 3 (max, non death persistent)
        if (prio < 2 && (*itr)->GetSpellProto()->SpellVisual == 99 && (*itr)->GetSpellProto()->SpellIconID == 92)
        {
            switch ((*itr)->GetId())
            {
                case 20707: spell_id =  3026; break;        // rank 1
                case 20762: spell_id = 20758; break;        // rank 2
                case 20763: spell_id = 20759; break;        // rank 3
                case 20764: spell_id = 20760; break;        // rank 4
                case 20765: spell_id = 20761; break;        // rank 5
                case 27239: spell_id = 27240; break;        // rank 6
                default:
                    sLog.outError("Unhandled spell %u: S.Resurrection", (*itr)->GetId());
                    continue;
            }

            prio = 3;
        }
        // Twisting Nether                                  // prio: 2 (max)
        else if ((*itr)->GetId() == 23701 && roll_chance_i(10))
        {
            prio = 2;
            spell_id = 23700;
        }
    }

    // Reincarnation (passive spell)                        // prio: 1
    if (prio < 1 && HasSpell(20608) && !HasSpellCooldown(21169) && HasItemCount(17030, EFFECT_INDEX_1))
    {
        spell_id = 21169;
    }

    return spell_id;
}

// Used in triggers for check "Only to targets that grant experience or honor" req
bool Player::isHonorOrXPTarget(Unit* pVictim) const
{
    uint32 v_level = pVictim->getLevel();
    uint32 k_grey  = MaNGOS::XP::GetGrayLevel(getLevel());

    // Victim level less gray level
    if (v_level <= k_grey)
    {
        return false;
    }

    if (pVictim->GetTypeId() == TYPEID_UNIT)
    {
        if (((Creature*)pVictim)->IsTotem() ||
            ((Creature*)pVictim)->IsPet() ||
            ((Creature*)pVictim)->GetCreatureInfo()->ExtraFlags & CREATURE_FLAG_EXTRA_NO_XP_AT_KILL)
            {
                return false;
            }
    }
    return true;
}

/**
 * @brief Rewards the player for killing a unit outside group reward distribution.
 *
 * @param pVictim The killed unit.
 */
void Player::RewardSinglePlayerAtKill(Unit* pVictim)
{
    bool PvP = pVictim->IsCharmedOwnedByPlayerOrPlayer();

    uint32 xp = PvP ? 0 : MaNGOS::XP::Gain(this, pVictim);

    // honor can be in PvP and !PvP (racial leader) cases
    RewardHonor(pVictim, 1);

    // xp and reputation only in !PvP case
    if (!PvP)
    {
        RewardReputation(pVictim, 1);
        GiveXP(xp, pVictim);

        if (Pet* pet = GetPet())
        {
            pet->GivePetXP(xp);
        }

        // normal creature (not pet/etc) can be only in !PvP case
        if (pVictim->GetTypeId() == TYPEID_UNIT)
        {
            if (CreatureInfo const* normalInfo = ObjectMgr::GetCreatureTemplate(pVictim->GetEntry()))
            {
                KilledMonster(normalInfo, pVictim->GetObjectGuid());
            }
        }
    }
}

/**
 * @brief Grants event kill credit to the player or nearby group members.
 *
 * @param creature_id The credited creature entry identifier.
 * @param pRewardSource The world object used for distance checks.
 */
void Player::RewardPlayerAndGroupAtEvent(uint32 creature_id, WorldObject* pRewardSource)
{
    MANGOS_ASSERT((!GetGroup() || pRewardSource));              // Player::RewardPlayerAndGroupAtEvent called for Group-Case but no source for range searching provided

    ObjectGuid creature_guid = pRewardSource && pRewardSource->GetTypeId() == TYPEID_UNIT ? pRewardSource->GetObjectGuid() : ObjectGuid();

    // prepare data for near group iteration
    if (Group* pGroup = GetGroup())
    {
        for (GroupReference* itr = pGroup->GetFirstMember(); itr != NULL; itr = itr->next())
        {
            Player* pGroupGuy = itr->getSource();
            if (!pGroupGuy)
            {
                continue;
            }

            if (!pGroupGuy->IsAtGroupRewardDistance(pRewardSource))
            {
                continue; // member (alive or dead) or his corpse at req. distance
            }

            // quest objectives updated only for alive group member or dead but with not released body
            if (pGroupGuy->IsAlive() || !pGroupGuy->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST))
            {
                pGroupGuy->KilledMonsterCredit(creature_id, creature_guid);
            }
        }
    }
    else                                                    // if (!pGroup)
    {
        KilledMonsterCredit(creature_id, creature_guid);
    }
}

/**
 * @brief Grants quest cast credit to the player or nearby group members.
 *
 * @param pRewardSource The credited creature or gameobject.
 * @param spellid The spell that granted the credit.
 */
void Player::RewardPlayerAndGroupAtCast(WorldObject* pRewardSource, uint32 spellid)
{
    // prepare data for near group iteration
    if (Group* pGroup = GetGroup())
    {
        for (GroupReference* itr = pGroup->GetFirstMember(); itr != NULL; itr = itr->next())
        {
            Player* pGroupGuy = itr->getSource();
            if (!pGroupGuy)
            {
                continue;
            }

            if (!pGroupGuy->IsAtGroupRewardDistance(pRewardSource))
            {
                continue; // member (alive or dead) or his corpse at req. distance
            }

            // quest objectives updated only for alive group member or dead but with not released body
            if (pGroupGuy->IsAlive() || !pGroupGuy->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST))
            {
                pGroupGuy->CastedCreatureOrGO(pRewardSource->GetEntry(), pRewardSource->GetObjectGuid(), spellid, pGroupGuy == this);
            }
        }
    }
    else                                                    // if (!pGroup)
    {
        CastedCreatureOrGO(pRewardSource->GetEntry(), pRewardSource->GetObjectGuid(), spellid);
    }
}

/**
 * @brief Checks whether the player or corpse is close enough for shared rewards.
 *
 * @param pRewardSource The source object used for the distance check.
 * @return True if the player qualifies for group reward range; otherwise, false.
 */
bool Player::IsAtGroupRewardDistance(WorldObject const* pRewardSource) const
{
    if (pRewardSource->IsWithinDistInMap(this, sWorld.getConfig(CONFIG_FLOAT_GROUP_XP_DISTANCE)))
    {
        return true;
    }

    if (IsAlive())
    {
        return false;
    }

    Corpse* corpse = GetCorpse();
    if (!corpse)
    {
        return false;
    }

    return pRewardSource->IsWithinDistInMap(corpse, sWorld.getConfig(CONFIG_FLOAT_GROUP_XP_DISTANCE));
}

/**
 * @brief Gets the player's base weapon skill for an attack type.
 *
 * @param attType The attack type to evaluate.
 * @return The corresponding base weapon skill value.
 */
uint32 Player::GetBaseWeaponSkillValue(WeaponAttackType attType) const
{
    Item* item = GetWeaponForAttack(attType, true, true);

    // unarmed only with base attack
    if (attType != BASE_ATTACK && !item)
    {
        return 0;
    }

    // weapon skill or (unarmed for base attack)
    uint32  skill = item ? item->GetSkill() : uint32(SKILL_UNARMED);
    return GetBaseSkillValue(skill);
}

/**
 * @brief Resurrects the player using the pending resurrection request data.
 */
void Player::ResurectUsingRequestData()
{
    /// Teleport before resurrecting by player, otherwise the player might get attacked from creatures near his corpse
    if (m_resurrectGuid.IsPlayer())
    {
        TeleportTo(m_resurrectMap, m_resurrectX, m_resurrectY, m_resurrectZ, GetOrientation());
    }

    // we can not resurrect player when we triggered far teleport
    // player will be resurrected upon teleportation
    if (IsBeingTeleportedFar())
    {
        ScheduleDelayedOperation(DELAYED_RESURRECT_PLAYER);
        return;
    }

    ResurrectPlayer(0.0f, false);

    if (GetMaxHealth() > m_resurrectHealth)
    {
        SetHealth(m_resurrectHealth);
    }
    else
    {
        SetHealth(GetMaxHealth());
    }

    if (GetMaxPower(POWER_MANA) > m_resurrectMana)
    {
        SetPower(POWER_MANA, m_resurrectMana);
    }
    else
    {
        SetPower(POWER_MANA, GetMaxPower(POWER_MANA));
    }

    SetPower(POWER_RAGE, 0);

    SetPower(POWER_ENERGY, GetMaxPower(POWER_ENERGY));

    SpawnCorpseBones();
}

/**
 * @brief Sends a client-control state update for a unit.
 *
 * @param target The unit whose control state is being updated.
 * @param allowMove Nonzero to allow movement; zero to disable it.
 */
void Player::SetClientControl(Unit* target, uint8 allowMove)
{
    WorldPacket data(SMSG_CLIENT_CONTROL_UPDATE, target->GetPackGUID().size() + 1);
    data << target->GetPackGUID();
    data << uint8(allowMove);
    GetSession()->SendPacket(&data);
}

/**
 * @brief Applies or removes auras that depend on the player's current zone.
 */
void Player::UpdateZoneDependentAuras()
{
    // Some spells applied at enter into zone (with subzones), aura removed in UpdateAreaDependentAuras that called always at zone->area update
    SpellAreaForAreaMapBounds saBounds = sSpellMgr.GetSpellAreaForAreaMapBounds(m_zoneUpdateId);
    for (SpellAreaForAreaMap::const_iterator itr = saBounds.first; itr != saBounds.second; ++itr)
    {
        itr->second->ApplyOrRemoveSpellIfCan(this, m_zoneUpdateId, 0, true);
    }
}

/**
 * @brief Applies or removes auras that depend on the player's current subzone.
 */
void Player::UpdateAreaDependentAuras()
{
    // remove auras from spells with area limitations
    for (SpellAuraHolderMap::iterator iter = m_spellAuraHolders.begin(); iter != m_spellAuraHolders.end();)
    {
        // use m_zoneUpdateId for speed: UpdateArea called from UpdateZone or instead UpdateZone in both cases m_zoneUpdateId up-to-date
        if (sSpellMgr.GetSpellAllowedInLocationError(iter->second->GetSpellProto(), GetMapId(), m_zoneUpdateId, m_areaUpdateId, this) != SPELL_CAST_OK)
        {
            RemoveSpellAuraHolder(iter->second);
            iter = m_spellAuraHolders.begin();
        }
        else
        {
            ++iter;
        }
    }

    // some auras applied at subzone enter
    SpellAreaForAreaMapBounds saBounds = sSpellMgr.GetSpellAreaForAreaMapBounds(m_areaUpdateId);
    for (SpellAreaForAreaMap::const_iterator itr = saBounds.first; itr != saBounds.second; ++itr)
    {
        itr->second->ApplyOrRemoveSpellIfCan(this, m_zoneUpdateId, m_areaUpdateId, true);
    }
}

struct UpdateZoneDependentPetsHelper
{
    explicit UpdateZoneDependentPetsHelper(Player* _owner, uint32 zone, uint32 area) : owner(_owner), zone_id(zone), area_id(area) {}
    void operator()(Unit* unit) const
    {
        if (unit->GetTypeId() == TYPEID_UNIT && ((Creature*)unit)->IsPet() && !((Pet*)unit)->IsPermanentPetFor(owner))
            if (uint32 spell_id = unit->GetUInt32Value(UNIT_CREATED_BY_SPELL))
                if (SpellEntry const* spellEntry = sSpellStore.LookupEntry(spell_id))
                    if (sSpellMgr.GetSpellAllowedInLocationError(spellEntry, owner->GetMapId(), zone_id, area_id, owner) != SPELL_CAST_OK)
                    {
                        ((Pet*)unit)->Unsummon(PET_SAVE_AS_DELETED, owner);
                    }
    }
    Player* owner;
    uint32 zone_id;
    uint32 area_id;
};

void Player::UpdateZoneDependentPets()
{
    // check pet (permanent pets ignored), minipet, guardians (including protector)
    CallForAllControlledUnits(UpdateZoneDependentPetsHelper(this, m_zoneUpdateId, m_areaUpdateId), CONTROLLED_PET | CONTROLLED_GUARDIANS | CONTROLLED_MINIPET);
}

/**
 * @brief Gets the current corpse reclaim delay for PvE or PvP death.
 *
 * @param pvp True for PvP death rules; false for PvE death rules.
 * @return The reclaim delay in seconds.
 */
uint32 Player::GetCorpseReclaimDelay(bool pvp) const
{
    if ((pvp && !sWorld.getConfig(CONFIG_BOOL_DEATH_CORPSE_RECLAIM_DELAY_PVP)) ||
        (!pvp && !sWorld.getConfig(CONFIG_BOOL_DEATH_CORPSE_RECLAIM_DELAY_PVE)))
    {
        return corpseReclaimDelay[0];
    }

    time_t now = time(NULL);
    // 0..2 full period
    uint32 count = (now < m_deathExpireTime) ? uint32((m_deathExpireTime - now) / DEATH_EXPIRE_STEP) : 0;
    return corpseReclaimDelay[count];
}

/**
 * @brief Advances the corpse reclaim delay escalation after death.
 */
void Player::UpdateCorpseReclaimDelay()
{
    bool pvp = m_ExtraFlags & PLAYER_EXTRA_PVP_DEATH;

    if ((pvp && !sWorld.getConfig(CONFIG_BOOL_DEATH_CORPSE_RECLAIM_DELAY_PVP)) ||
        (!pvp && !sWorld.getConfig(CONFIG_BOOL_DEATH_CORPSE_RECLAIM_DELAY_PVE)))
    {
        return;
    }

    time_t now = time(NULL);
    if (now < m_deathExpireTime)
    {
        // full and partly periods 1..3
        uint32 count = uint32((m_deathExpireTime - now) / DEATH_EXPIRE_STEP + 1);
        if (count < MAX_DEATH_COUNT)
        {
            m_deathExpireTime = now + (count + 1) * DEATH_EXPIRE_STEP;
        }
        else
        {
            m_deathExpireTime = now + MAX_DEATH_COUNT * DEATH_EXPIRE_STEP;
        }
    }
    else
    {
        m_deathExpireTime = now + DEATH_EXPIRE_STEP;
    }
}

/**
 * @brief Sends the current corpse reclaim delay to the client.
 *
 * @param load True when restoring the delay from saved corpse state.
 */
void Player::SendCorpseReclaimDelay(bool load)
{
    Corpse* corpse = GetCorpse();
    if (!corpse)
    {
        return;
    }

    uint32 delay;
    if (load)
    {
        if (corpse->GetGhostTime() > m_deathExpireTime)
        {
            return;
        }

        bool pvp = corpse->GetType() == CORPSE_RESURRECTABLE_PVP;

        uint32 count;
        if ((pvp && sWorld.getConfig(CONFIG_BOOL_DEATH_CORPSE_RECLAIM_DELAY_PVP)) ||
            (!pvp && sWorld.getConfig(CONFIG_BOOL_DEATH_CORPSE_RECLAIM_DELAY_PVE)))
        {
            count = uint32(m_deathExpireTime - corpse->GetGhostTime()) / DEATH_EXPIRE_STEP;
            if (count >= MAX_DEATH_COUNT)
            {
                count = MAX_DEATH_COUNT - 1;
            }
        }
        else
        {
            count = 0;
        }

        time_t expected_time = corpse->GetGhostTime() + corpseReclaimDelay[count];

        time_t now = time(NULL);
        if (now >= expected_time)
        {
            return;
        }

        delay = uint32(expected_time - now);
    }
    else
    {
        delay = GetCorpseReclaimDelay(corpse->GetType() == CORPSE_RESURRECTABLE_PVP);
    }

    //! corpse reclaim delay 30 * 1000ms or longer at often deaths
    WorldPacket data(SMSG_CORPSE_RECLAIM_DELAY, 4);
    data << uint32(delay * IN_MILLISECONDS);
    GetSession()->SendPacket(&data);
}

/**
 * @brief Selects a random nearby raid member within a radius.
 *
 * @param radius The maximum search radius.
 * @return A random eligible raid member, or NULL if none are found.
 */
Player* Player::GetNextRandomRaidMember(float radius)
{
    Group* pGroup = GetGroup();
    if (!pGroup)
    {
        return NULL;
    }

    std::vector<Player*> nearMembers;
    nearMembers.reserve(pGroup->GetMembersCount());

    for (GroupReference* itr = pGroup->GetFirstMember(); itr != NULL; itr = itr->next())
    {
        Player* Target = itr->getSource();

        // IsHostileTo check duel and controlled by enemy
        if (Target && Target != this && IsWithinDistInMap(Target, radius) &&
            !Target->HasInvisibilityAura() && !IsHostileTo(Target))
            {
                nearMembers.push_back(Target);
            }
    }

    if (nearMembers.empty())
    {
        return NULL;
    }

    uint32 randTarget = urand(0, nearMembers.size() - 1);
    return nearMembers[randTarget];
}

/**
 * @brief Checks whether the player is allowed to uninvite someone from the group.
 *
 * @return The party result code describing whether uninvite is allowed.
 */
PartyResult Player::CanUninviteFromGroup() const
{
    const Group* grp = GetGroup();
    if (!grp)
    {
        return ERR_NOT_IN_GROUP;
    }

    if (!grp->IsLeader(GetObjectGuid()) && !grp->IsAssistant(GetObjectGuid()))
    {
        return ERR_NOT_LEADER;
    }

    if (InBattleGround())
    {
        return ERR_INVITE_RESTRICTED;
    }

    return ERR_PARTY_RESULT_OK;
}

/**
 * @brief Moves the player into a battleground raid group.
 *
 * @param group The battleground raid group.
 * @param subgroup The battleground subgroup index.
 */
void Player::SetBattleGroundRaid(Group* group, int8 subgroup)
{
    // we must move references from m_group to m_originalGroup
    SetOriginalGroup(GetGroup(), GetSubGroup());

    m_group.unlink();
    m_group.link(group, this);
    m_group.setSubGroup((uint8)subgroup);
}

/**
 * @brief Restores the player's original group after leaving a battleground raid.
 */
void Player::RemoveFromBattleGroundRaid()
{
    // remove existing reference
    m_group.unlink();
    if (Group* group = GetOriginalGroup())
    {
        m_group.link(group, this);
        m_group.setSubGroup(GetOriginalSubGroup());
    }
    SetOriginalGroup(NULL);
}

/**
 * @brief Stores the player's original non-battleground group reference.
 *
 * @param group The original group, or NULL to clear it.
 * @param subgroup The original subgroup index.
 */
void Player::SetOriginalGroup(Group* group, int8 subgroup)
{
    if (group == NULL)
    {
        m_originalGroup.unlink();
    }
    else
    {
        // never use SetOriginalGroup without a subgroup unless you specify NULL for group
        MANGOS_ASSERT(subgroup >= 0);
        m_originalGroup.link(group, this);
        m_originalGroup.setSubGroup((uint8)subgroup);
    }
}

/**
 * @brief Updates liquid auras and mirror timers based on the player's position.
 *
 * @param m The current map.
 * @param x The X coordinate.
 * @param y The Y coordinate.
 * @param z The Z coordinate.
 */
void Player::UpdateUnderwaterState(Map* m, float x, float y, float z)
{
    GridMapLiquidData liquid_status;
    GridMapLiquidStatus res = m->GetTerrain()->getLiquidStatus(x, y, z, MAP_ALL_LIQUIDS, &liquid_status);
    if (!res)
    {
        m_MirrorTimerFlags &= ~(UNDERWATER_INWATER | UNDERWATER_INLAVA | UNDERWATER_INSLIME | UNDERWATER_INDARKWATER);
        if (m_lastLiquid && m_lastLiquid->SpellId)
        {
            RemoveAurasDueToSpell(m_lastLiquid->SpellId == 37025 ? 37284 : m_lastLiquid->SpellId);
        }
        m_lastLiquid = NULL;
        return;
    }

    if (uint32 liqEntry = liquid_status.entry)
    {
        LiquidTypeEntry const* liquid = sLiquidTypeStore.LookupEntry(liqEntry);
        if (m_lastLiquid && m_lastLiquid->SpellId && m_lastLiquid->Id != liqEntry)
        {
            RemoveAurasDueToSpell(m_lastLiquid->SpellId);
        }

        if (liquid && liquid->SpellId)
        {
            // Exception for SSC water
            uint32 liquidSpellId = liquid->SpellId == 37025 ? 37284 : liquid->SpellId;

            if (res & (LIQUID_MAP_UNDER_WATER | LIQUID_MAP_IN_WATER))
            {
                if (!HasAura(liquidSpellId))
                {
                    // Handle exception for SSC water
                    if (liquid->SpellId == 37025)
                    {
                        if (InstanceData* pInst = GetInstanceData())
                        {
                            if (pInst->CheckConditionCriteriaMeet(this, INSTANCE_CONDITION_ID_LURKER, NULL, CONDITION_FROM_HARDCODED))
                            {
                                if (pInst->CheckConditionCriteriaMeet(this, INSTANCE_CONDITION_ID_SCALDING_WATER, NULL, CONDITION_FROM_HARDCODED))
                                {
                                    CastSpell(this, liquidSpellId, true);
                                }
                                else
                                {
                                    SummonCreature(21508, 0, 0, 0, 0, TEMPSPAWN_TIMED_OOC_DESPAWN, 2000);
                                    // Special update timer for the SSC water
                                    m_positionStatusUpdateTimer = 2000;
                                }
                            }
                        }
                    }
                    else
                    {
                        CastSpell(this, liquidSpellId, true);
                    }
                }
            }
            else
            {
                RemoveAurasDueToSpell(liquidSpellId);
            }
        }

        m_lastLiquid = liquid;
    }
    else if (m_lastLiquid && m_lastLiquid->SpellId)
    {
        RemoveAurasDueToSpell(m_lastLiquid->SpellId == 37025 ? 37284 : m_lastLiquid->SpellId);
        m_lastLiquid = NULL;
    }

    // All liquids type - check under water position
    if (liquid_status.type_flags & (MAP_LIQUID_TYPE_WATER | MAP_LIQUID_TYPE_OCEAN | MAP_LIQUID_TYPE_MAGMA | MAP_LIQUID_TYPE_SLIME))
    {
        if (res & LIQUID_MAP_UNDER_WATER)
        {
            m_MirrorTimerFlags |= UNDERWATER_INWATER;
        }
        else
        {
            m_MirrorTimerFlags &= ~UNDERWATER_INWATER;
        }
    }

    // Allow travel in dark water on taxi or transport
    if ((liquid_status.type_flags & MAP_LIQUID_TYPE_DARK_WATER) && !IsTaxiFlying() && !GetTransport())
    {
        m_MirrorTimerFlags |= UNDERWATER_INDARKWATER;
    }
    else
    {
        m_MirrorTimerFlags &= ~UNDERWATER_INDARKWATER;
    }

    // in lava check, anywhere in lava level
    if (liquid_status.type_flags & MAP_LIQUID_TYPE_MAGMA)
    {
        if (res & (LIQUID_MAP_UNDER_WATER | LIQUID_MAP_IN_WATER | LIQUID_MAP_WATER_WALK))
        {
            m_MirrorTimerFlags |= UNDERWATER_INLAVA;
        }
        else
        {
            m_MirrorTimerFlags &= ~UNDERWATER_INLAVA;
        }
    }
    // in slime check, anywhere in slime level
    if (liquid_status.type_flags & MAP_LIQUID_TYPE_SLIME)
    {
        if (res & (LIQUID_MAP_UNDER_WATER | LIQUID_MAP_IN_WATER | LIQUID_MAP_WATER_WALK))
        {
            m_MirrorTimerFlags |= UNDERWATER_INSLIME;
        }
        else
        {
            m_MirrorTimerFlags &= ~UNDERWATER_INSLIME;
        }
    }
}

/**
 * @brief Enables or disables the player's ability to parry.
 *
 * @param value True to allow parry; false to disable it.
 */
void Player::SetCanParry(bool value)
{
    if (m_canParry == value)
    {
        return;
    }

    m_canParry = value;
    UpdateParryPercentage();
}

/**
 * @brief Enables or disables the player's ability to block.
 *
 * @param value True to allow block; false to disable it.
 */
void Player::SetCanBlock(bool value)
{
    if (m_canBlock == value)
    {
        return;
    }

    m_canBlock = value;
    UpdateBlockPercentage();
}

/**
 * @brief Checks whether this item position entry exists in a vector of positions.
 *
 * @param vec The vector of item positions to search.
 * @return True if an entry with the same position exists; otherwise, false.
 */
bool ItemPosCount::isContainedIn(ItemPosCountVec const& vec) const
{
    for (ItemPosCountVec::const_iterator itr = vec.begin(); itr != vec.end(); ++itr)
    {
        if (itr->pos == pos)
        {
            return true;
        }
    }

    return false;
}

/**
 * @brief Checks whether the player can interact with a battleground object.
 *
 * @return True if the player can use the object; otherwise, false.
 */
bool Player::CanUseBattleGroundObject()
{
    // TODO : some spells gives player ForceReaction to one faction (ReputationMgr::ApplyForceReaction)
    // maybe gameobject code should handle that ForceReaction usage
    return (IsAlive() &&                                    // living
            // the following two are incorrect, because invisible/stealthed players should get visible when they click on flag
            !HasStealthAura() &&                            // not stealthed
            !HasInvisibilityAura() &&                       // visible
            !isTotalImmune() &&                             // vulnerable (not immune)
            !HasAura(SPELL_RECENTLY_DROPPED_FLAG, EFFECT_INDEX_0));
}

/**
 * @brief Checks whether the player is immune to all spell schools.
 *
 * @return True if total immunity is active; otherwise, false.
 */
bool Player::isTotalImmune()
{
    AuraList const& immune = GetAurasByType(SPELL_AURA_SCHOOL_IMMUNITY);

    uint32 immuneMask = 0;
    for (AuraList::const_iterator itr = immune.begin(); itr != immune.end(); ++itr)
    {
        immuneMask |= (*itr)->GetModifier()->m_miscvalue;
        if (immuneMask & SPELL_SCHOOL_MASK_ALL)             // total immunity
        {
            return true;
        }
    }
    return false;
}

bool Player::HasTitle(uint32 bitIndex) const
{
    if (bitIndex > MAX_TITLE_INDEX)
    {
        return false;
    }

    uint32 fieldIndexOffset = bitIndex / 32;
    uint32 flag = 1 << (bitIndex % 32);
    return HasFlag(PLAYER__FIELD_KNOWN_TITLES + fieldIndexOffset, flag);
}

void Player::SetTitle(CharTitlesEntry const* title, bool lost)
{
    uint32 fieldIndexOffset = title->bit_index / 32;
    uint32 flag = 1 << (title->bit_index % 32);

    if (lost)
    {
        if (!HasFlag(PLAYER__FIELD_KNOWN_TITLES + fieldIndexOffset, flag))
        {
            return;
        }

        RemoveFlag(PLAYER__FIELD_KNOWN_TITLES + fieldIndexOffset, flag);
    }
    else
    {
        if (HasFlag(PLAYER__FIELD_KNOWN_TITLES + fieldIndexOffset, flag))
        {
            return;
        }

        SetFlag(PLAYER__FIELD_KNOWN_TITLES + fieldIndexOffset, flag);
    }

    WorldPacket data(SMSG_TITLE_EARNED, 4 + 4);
    data << uint32(title->bit_index);
    data << uint32(lost ? 0 : 1);                           // 1 - earned, 0 - lost
    GetSession()->SendPacket(&data);
}

/**
 * @brief Builds temporary loot data and stores all eligible loot automatically.
 *
 * @param lootTarget The object owning the loot.
 * @param loot_id The loot template identifier.
 * @param store The loot store to use.
 * @param broadcast True to broadcast item gains.
 * @param bag The preferred destination bag.
 * @param slot The preferred destination slot.
 */
void Player::AutoStoreLoot(WorldObject const* lootTarget, uint32 loot_id, LootStore const& store, bool broadcast, uint8 bag, uint8 slot)
{
    Loot loot(lootTarget);
    loot.FillLoot(loot_id, store, this, true);

    AutoStoreLoot(loot, broadcast, bag, slot);
}

/**
 * @brief Stores all eligible loot entries directly into the player's inventory.
 *
 * @param loot The loot container to process.
 * @param broadcast True to broadcast item gains.
 * @param bag The preferred destination bag.
 * @param slot The preferred destination slot.
 */
void Player::AutoStoreLoot(Loot& loot, bool broadcast, uint8 bag, uint8 slot)
{
    uint32 max_slot = loot.GetMaxSlotInLootFor(this);
    for (uint32 i = 0; i < max_slot; ++i)
    {
        LootItem* lootItem = loot.LootItemInSlot(i, this);

        ItemPosCountVec dest;
        InventoryResult msg = CanStoreNewItem(bag, slot, dest, lootItem->itemid, lootItem->count);
        if (msg != EQUIP_ERR_OK && slot != NULL_SLOT)
        {
            msg = CanStoreNewItem(bag, NULL_SLOT, dest, lootItem->itemid, lootItem->count);
        }
        if (msg != EQUIP_ERR_OK && bag != NULL_BAG)
        {
            msg = CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, lootItem->itemid, lootItem->count);
        }
        if (msg != EQUIP_ERR_OK)
        {
            SendEquipError(msg, NULL, NULL, lootItem->itemid);
            continue;
        }

        Item* pItem = StoreNewItem(dest, lootItem->itemid, true, lootItem->randomPropertyId);
        SendNewItem(pItem, lootItem->count, false, false, broadcast);
    }
}

/**
 * @brief Replaces an item with another item while preserving transferable state.
 *
 * @param item The original item.
 * @param newItemId The new item entry identifier.
 * @return The converted item, or NULL if conversion failed.
 */
Item* Player::ConvertItem(Item* item, uint32 newItemId)
{
    uint16 pos = item->GetPos();

    Item* pNewItem = Item::CreateItem(newItemId, 1, this);
    if (!pNewItem)
    {
        return NULL;
    }

    // copy enchantments
    for (uint8 j = PERM_ENCHANTMENT_SLOT; j <= TEMP_ENCHANTMENT_SLOT; ++j)
    {
        if (item->GetEnchantmentId(EnchantmentSlot(j)))
            pNewItem->SetEnchantment(EnchantmentSlot(j), item->GetEnchantmentId(EnchantmentSlot(j)),
                                     item->GetEnchantmentDuration(EnchantmentSlot(j)), item->GetEnchantmentCharges(EnchantmentSlot(j)));
    }

    // copy durability
    if (item->GetUInt32Value(ITEM_FIELD_DURABILITY) < item->GetUInt32Value(ITEM_FIELD_MAXDURABILITY))
    {
        double loosePercent = 1 - item->GetUInt32Value(ITEM_FIELD_DURABILITY) / double(item->GetUInt32Value(ITEM_FIELD_MAXDURABILITY));
        DurabilityLoss(pNewItem, loosePercent);
    }

    if (IsInventoryPos(pos))
    {
        ItemPosCountVec dest;
        InventoryResult msg = CanStoreItem(item->GetBagSlot(), item->GetSlot(), dest, pNewItem, true);
        // ignore cast/combat time restriction
        if (msg == EQUIP_ERR_OK)
        {
            DestroyItem(item->GetBagSlot(), item->GetSlot(), true);
            return StoreItem(dest, pNewItem, true);
        }
    }
    else if (IsBankPos(pos))
    {
        ItemPosCountVec dest;
        InventoryResult msg = CanBankItem(item->GetBagSlot(), item->GetSlot(), dest, pNewItem, true);
        // ignore cast/combat time restriction
        if (msg == EQUIP_ERR_OK)
        {
            DestroyItem(item->GetBagSlot(), item->GetSlot(), true);
            return BankItem(dest, pNewItem, true);
        }
    }
    else if (IsEquipmentPos(pos))
    {
        uint16 dest;
        InventoryResult msg = CanEquipItem(item->GetSlot(), dest, pNewItem, true, false);
        // ignore cast/combat time restriction
        if (msg == EQUIP_ERR_OK)
        {
            DestroyItem(item->GetBagSlot(), item->GetSlot(), true);
            pNewItem = EquipItem(dest, pNewItem, true);
            AutoUnequipOffhandIfNeed();
            return pNewItem;
        }
    }

    // fail
    delete pNewItem;
    return NULL;
}

/**
 * @brief Calculates the total talent points available for the player's level.
 *
 * @return The number of talent points granted by level and rate settings.
 */
uint32 Player::CalculateTalentsPoints() const
{
    uint32 talentPointsForLevel = getLevel() < 10 ? 0 : getLevel() - 9;
    return uint32(talentPointsForLevel * sWorld.getConfig(CONFIG_FLOAT_RATE_TALENT));
}

struct DoPlayerLearnSpell
{
    DoPlayerLearnSpell(Player& _player) : player(_player) {}
    void operator()(uint32 spell_id) { player.learnSpell(spell_id, false); }
    Player& player;
};

/**
 * @brief Learns a spell and all higher ranks linked in its rank chain.
 *
 * @param spellid The base spell identifier.
 */
void Player::learnSpellHighRank(uint32 spellid)
{
    learnSpell(spellid, false);

    DoPlayerLearnSpell worker(*this);
    sSpellMgr.doForHighRanks(spellid, worker);
}

/**
 * @brief Loads skill values from the database and initializes related rewards.
 *
 * @param result The query result containing saved skill rows.
 */
void Player::_LoadSkills(QueryResult* result)
{
    //                                                           0      1      2
    // SetPQuery(PLAYER_LOGIN_QUERY_LOADSKILLS,          "SELECT `skill`, `value`, `max` FROM `character_skills` WHERE `guid` = '%u'", GUID_LOPART(m_guid));

    uint32 count = 0;
    if (result)
    {
        do
        {
            Field* fields = result->Fetch();

            uint16 skill    = fields[0].GetUInt16();
            uint16 value    = fields[1].GetUInt16();
            uint16 max      = fields[2].GetUInt16();

            SkillLineEntry const* pSkill = sSkillLineStore.LookupEntry(skill);
            if (!pSkill)
            {
                sLog.outError("Character %u has skill %u that does not exist.", GetGUIDLow(), skill);
                continue;
            }

            // set fixed skill ranges
            switch (GetSkillRangeType(pSkill, false))
            {
                case SKILL_RANGE_LANGUAGE:                  // 300..300
                    value = max = 300;
                    break;
                case SKILL_RANGE_MONO:                      // 1..1, grey monolite bar
                    value = max = 1;
                    break;
                case SKILL_RANGE_LEVEL:
                    max = GetMaxSkillValueForLevel();       // max value can be wrong for the actual level
                    break;
                default:
                    break;
            }

            if (value == 0)
            {
                sLog.outError("Character %u has skill %u with value 0. Will be deleted.", GetGUIDLow(), skill);
                CharacterDatabase.PExecute("DELETE FROM `character_skills` WHERE `guid` = '%u' AND `skill` = '%u' ", GetGUIDLow(), skill);
                continue;
            }

            SetUInt32Value(PLAYER_SKILL_INDEX(count), MAKE_PAIR32(skill, 0));
            SetUInt32Value(PLAYER_SKILL_VALUE_INDEX(count), MAKE_SKILL_VALUE(value, max));
            SetUInt32Value(PLAYER_SKILL_BONUS_INDEX(count), 0);

            mSkillStatus.insert(SkillStatusMap::value_type(skill, SkillStatusData(count, SKILL_UNCHANGED)));

            learnSkillRewardedSpells(skill, value);

            ++count;

            if (count >= PLAYER_MAX_SKILLS)                 // client limit
            {
                sLog.outError("Character %u has more than %u skills.", GetGUIDLow(), PLAYER_MAX_SKILLS);
                break;
            }
        }
        while (result->NextRow());
        delete result;
    }

    for (; count < PLAYER_MAX_SKILLS; ++count)
    {
        SetUInt32Value(PLAYER_SKILL_INDEX(count), 0);
        SetUInt32Value(PLAYER_SKILL_VALUE_INDEX(count), 0);
        SetUInt32Value(PLAYER_SKILL_BONUS_INDEX(count), 0);
    }
}

/**
 * @brief Checks whether a concrete item can be equipped under unique-equip rules.
 *
 * @param pItem The item to test.
 * @param eslot The equipment slot being considered.
 * @return The inventory result describing whether equipping is allowed.
 */
InventoryResult Player::CanEquipUniqueItem(Item* pItem, uint8 eslot) const
{
    ItemPrototype const* pProto = pItem->GetProto();

    // proto based limitations
    if (InventoryResult res = CanEquipUniqueItem(pProto, eslot))
    {
        return res;
    }

    // check unique-equipped on gems
    for (uint32 enchant_slot = SOCK_ENCHANTMENT_SLOT; enchant_slot < SOCK_ENCHANTMENT_SLOT + 3; ++enchant_slot)
    {
        uint32 enchant_id = pItem->GetEnchantmentId(EnchantmentSlot(enchant_slot));
        if (!enchant_id)
        {
            continue;
        }
        SpellItemEnchantmentEntry const* enchantEntry = sSpellItemEnchantmentStore.LookupEntry(enchant_id);
        if (!enchantEntry)
        {
            continue;
        }

        ItemPrototype const* pGem = ObjectMgr::GetItemPrototype(enchantEntry->GemID);
        if (!pGem)
        {
            continue;
        }

        if (InventoryResult res = CanEquipUniqueItem(pGem, eslot))
        {
            return res;
        }
    }

    return EQUIP_ERR_OK;
}

/**
 * @brief Checks whether an item prototype can be equipped under unique-equip rules.
 *
 * @param itemProto The item prototype to test.
 * @param except_slot An equipment slot to ignore during the check.
 * @return The inventory result describing whether equipping is allowed.
 */
InventoryResult Player::CanEquipUniqueItem(ItemPrototype const* itemProto, uint8 except_slot) const
{
    // check unique-equipped on item
    if (itemProto->Flags & ITEM_FLAG_UNIQUE_EQUIPPED)
    {
        // there is an equip limit on this item
        if (HasItemOrGemWithIdEquipped(itemProto->ItemId, 1, except_slot))
        {
            return EQUIP_ERR_ITEM_UNIQUE_EQUIPABLE;
        }
    }

    return EQUIP_ERR_OK;
}

/**
 * @brief Calculates and applies fall damage from movement updates.
 *
 * @param movementInfo The movement packet information containing fall data.
 */
void Player::HandleFall(MovementInfo const& movementInfo)
{
    // calculate total z distance of the fall
    Position const* position = movementInfo.GetPos();
    float z_diff = m_lastFallZ - position->z;
    DEBUG_LOG("zDiff = %f", z_diff);

    // Players with low fall distance, Feather Fall or physical immunity (charges used) are ignored
    // 14.57 can be calculated by resolving damageperc formula below to 0
    if (z_diff >= 14.57f && !IsDead() && !isGameMaster() && !HasMovementFlag(MOVEFLAG_ONTRANSPORT) &&
        !HasAuraType(SPELL_AURA_HOVER) && !HasAuraType(SPELL_AURA_FEATHER_FALL) &&
        !HasAuraType(SPELL_AURA_FLY) && !IsImmuneToDamage(SPELL_SCHOOL_MASK_NORMAL))
    {
        // Safe fall, fall height reduction
        int32 safe_fall = GetTotalAuraModifier(SPELL_AURA_SAFE_FALL);

        float damageperc = 0.018f * (z_diff - safe_fall) - 0.2426f;

        if (damageperc > 0)
        {
            uint32 damage = (uint32)(damageperc * GetMaxHealth() * sWorld.getConfig(CONFIG_FLOAT_RATE_DAMAGE_FALL));

            float height = position->z;
            UpdateAllowedPositionZ(position->x, position->y, height);

            if (damage > 0)
            {
                // Prevent fall damage from being more than the player maximum health
                if (damage > GetMaxHealth())
                {
                    damage = GetMaxHealth();
                }

                // Gust of Wind
                if (GetDummyAura(43621))
                {
                    damage = GetMaxHealth() / 2;
                }

                EnvironmentalDamage(DAMAGE_FALL, damage);
            }

            // Z given by moveinfo, LastZ, FallTime, WaterZ, MapZ, Damage, Safefall reduction
            DEBUG_LOG("FALLDAMAGE z=%f sz=%f pZ=%f FallTime=%d mZ=%f damage=%d SF=%d" , position->z, height, GetPositionZ(), movementInfo.GetFallTime(), height, damage, safe_fall);
        }
    }
}

/**
 * @brief Temporarily unsummons the current pet when the player's state requires it.
 */
void Player::UnsummonPetTemporaryIfAny()
{
    Pet* pet = GetPet();
    if (!pet)
    {
        return;
    }

    if (!m_temporaryUnsummonedPetNumber && pet->isControlled() && !pet->isTemporarySummoned())
    {
        m_temporaryUnsummonedPetNumber = pet->GetCharmInfo()->GetPetNumber();
    }

    pet->Unsummon(PET_SAVE_AS_CURRENT, this);
}

/**
 * @brief Resummons a pet that was temporarily unsummoned earlier.
 */
void Player::ResummonPetTemporaryUnSummonedIfAny()
{
    if (!m_temporaryUnsummonedPetNumber)
    {
        return;
    }

    // not resummon in not appropriate state
    if (IsPetNeedBeTemporaryUnsummoned())
    {
        return;
    }

    if (GetPetGuid())
    {
        return;
    }

    Pet* NewPet = new Pet;
    if (!NewPet->LoadPetFromDB(this, 0, m_temporaryUnsummonedPetNumber, true))
    {
        delete NewPet;
    }

    m_temporaryUnsummonedPetNumber = 0;
}

/**
 * @brief Adds or removes money from the player while clamping to valid limits.
 *
 * @param d The signed money delta.
 */
void Player::ModifyMoney(int32 d)
{
    // Used by Eluna
#ifdef ENABLE_ELUNA
    if (Eluna* e = GetEluna())
    {
        e->OnMoneyChanged(this, d);
    }
#endif /* ENABLE_ELUNA */

    if (d < 0)
    {
        SetMoney(GetMoney() > uint32(-d) ? GetMoney() + d : 0);
    }
    else
    {
        SetMoney(GetMoney() < uint32(MAX_MONEY_AMOUNT - d) ? GetMoney() + d : MAX_MONEY_AMOUNT);
    }

    // "At Gold Limit"
    if (GetMoney() >= MAX_MONEY_AMOUNT)
    {
        SendEquipError(EQUIP_ERR_TOO_MUCH_GOLD, NULL, NULL);
    }
}

/**
 * @brief Clears an at-login flag from the player and optionally from the database.
 *
 * @param f The flag to remove.
 * @param in_db_also True to persist the removal to the database immediately.
 */
void Player::RemoveAtLoginFlag(AtLoginFlags f, bool in_db_also /*= false*/)
{
    m_atLoginFlags &= ~f;

    if (in_db_also)
    {
        CharacterDatabase.PExecute("UPDATE `characters` set `at_login` = `at_login` & ~ %u WHERE `guid` ='%u'", uint32(f), GetGUIDLow());
    }
}

/**
 * @brief Sends a packet that clears a spell cooldown on the client.
 *
 * @param spell_id The spell whose cooldown is being cleared.
 * @param target The target unit associated with the cooldown clear.
 */
void Player::SendClearCooldown(uint32 spell_id, Unit* target)
{
    WorldPacket data(SMSG_CLEAR_COOLDOWN, 4 + 8);
    data << uint32(spell_id);
    data << target->GetObjectGuid();
    SendDirectMessage(&data);
}

/**
 * @brief Builds a teleport acknowledgement packet using a destination position.
 *
 * @param data The packet to populate.
 * @param x The destination X coordinate.
 * @param y The destination Y coordinate.
 * @param z The destination Z coordinate.
 * @param ang The destination orientation.
 */
void Player::BuildTeleportAckMsg(WorldPacket& data, float x, float y, float z, float ang) const
{
    MovementInfo mi = m_movementInfo;
    mi.ChangePosition(x, y, z, ang);

    data.Initialize(MSG_MOVE_TELEPORT_ACK, 41);
    data << GetPackGUID();
    data << uint32(0);                                      // this value increments every time
    data << mi;
}

/**
 * @brief Checks whether the player's movement info currently contains a flag.
 *
 * @param f The movement flag to test.
 * @return True if the flag is present; otherwise, false.
 */
bool Player::HasMovementFlag(MovementFlags f) const
{
    return m_movementInfo.HasMovementFlag(f);
}

void Player::ResetTimeSync()
{
    m_timeSyncCounter = 0;
    m_timeSyncTimer = 0;
    m_timeSyncClient = 0;
    m_timeSyncServer = GameTime::GetGameTimeMS();
}

void Player::SendTimeSync()
{
    WorldPacket data(SMSG_TIME_SYNC_REQ, 4);
    data << uint32(m_timeSyncCounter++);
    GetSession()->SendPacket(&data);

    // Schedule next sync in 10 sec
    m_timeSyncTimer = 10000;
    m_timeSyncServer = GameTime::GetGameTimeMS();
}

/**
 * @brief Sets the player's home bind location and persists it to the database.
 *
 * @param loc The new home bind world location.
 * @param area_id The associated area identifier.
 */
void Player::SetHomebindToLocation(WorldLocation const& loc, uint32 area_id)
{
    m_homebindMapId = loc.mapid;
    m_homebindAreaId = area_id;
    m_homebindX = loc.coord_x;
    m_homebindY = loc.coord_y;
    m_homebindZ = loc.coord_z;

    // update sql homebind
    CharacterDatabase.PExecute("UPDATE `character_homebind` SET `map` = '%u', `zone` = '%u', `position_x` = '%f', `position_y` = '%f', `position_z` = '%f' WHERE `guid` = '%u'",
                               m_homebindMapId, m_homebindAreaId, m_homebindX, m_homebindY, m_homebindZ, GetGUIDLow());
}

/**
 * @brief Resolves an object GUID to a world object constrained by a type mask.
 *
 * @param guid The object GUID to resolve.
 * @param typemask The allowed object type mask.
 * @return The matching object, or NULL if not found or disallowed.
 */
Object* Player::GetObjectByTypeMask(ObjectGuid guid, TypeMask typemask)
{
    switch (guid.GetHigh())
    {
        case HIGHGUID_ITEM:
            if (typemask & TYPEMASK_ITEM)
            {
                return GetItemByGuid(guid);
            }
            break;
        case HIGHGUID_PLAYER:
            if (GetObjectGuid() == guid)
            {
                return this;
            }
            if ((typemask & TYPEMASK_PLAYER) && IsInWorld())
            {
                return sObjectAccessor.FindPlayer(guid);
            }
            break;
        case HIGHGUID_GAMEOBJECT:
            if ((typemask & TYPEMASK_GAMEOBJECT) && IsInWorld())
            {
                return GetMap()->GetGameObject(guid);
            }
            break;
        case HIGHGUID_UNIT:
            if ((typemask & TYPEMASK_UNIT) && IsInWorld())
            {
                return GetMap()->GetCreature(guid);
            }
            break;
        case HIGHGUID_PET:
            if ((typemask & TYPEMASK_UNIT) && IsInWorld())
            {
                return GetMap()->GetPet(guid);
            }
            break;
        case HIGHGUID_DYNAMICOBJECT:
            if ((typemask & TYPEMASK_DYNAMICOBJECT) && IsInWorld())
            {
                return GetMap()->GetDynamicObject(guid);
            }
            break;
        case HIGHGUID_TRANSPORT:
        case HIGHGUID_CORPSE:
        case HIGHGUID_MO_TRANSPORT:
        case HIGHGUID_GROUP:
        default:
            break;
    }

    return NULL;
}

/**
 * @brief Updates the player's current resting state.
 *
 * @param n_r_type The new rest type.
 * @param areaTriggerId The inn or rest area trigger identifier, if applicable.
 */
void Player::SetRestType(RestType n_r_type, uint32 areaTriggerId /*= 0*/)
{
    rest_type = n_r_type;

    if (rest_type == REST_TYPE_NO)
    {
        RemoveFlag(PLAYER_FLAGS, PLAYER_FLAGS_RESTING);

        // Set player to FFA PVP when not in rested environment.
        if (sWorld.IsFFAPvPRealm())
        {
            SetFFAPvP(true);
        }
    }
    else
    {
        SetFlag(PLAYER_FLAGS, PLAYER_FLAGS_RESTING);

        inn_trigger_id = areaTriggerId;
        time_inn_enter = time(NULL);

        if (sWorld.IsFFAPvPRealm())
        {
            SetFFAPvP(false);
        }
    }
}

/**
 * @brief Sends the duel countdown timer to the client.
 *
 * @param counter The countdown value in seconds.
 */
void Player::SendDuelCountdown(uint32 counter)
{
    WorldPacket data(SMSG_DUEL_COUNTDOWN, 4);
    data << uint32(counter);                                // seconds
    GetSession()->SendPacket(&data);
}

/**
 * @brief Checks whether the player is immune to a specific spell effect.
 *
 * @param spellInfo The spell entry being evaluated.
 * @param index The effect index within the spell.
 * @param castOnSelf True when the spell is being cast on the player by the player.
 * @return True if the effect is immune; otherwise, false.
 */
bool Player::IsImmuneToSpellEffect(SpellEntry const* spellInfo, SpellEffectIndex index, bool castOnSelf) const
{
    switch (spellInfo->Effect[index])
    {
        case SPELL_EFFECT_ATTACK_ME:
            return true;
        default:
            break;
    }
    switch (spellInfo->EffectApplyAuraName[index])
    {
        case SPELL_AURA_MOD_TAUNT:
            return true;
        default:
            break;
    }
    return Unit::IsImmuneToSpellEffect(spellInfo, index, castOnSelf);
}

/**
 * @brief Sends a knockback effect away from a target or from the player itself.
 *
 * @param target The source unit of the knockback.
 * @param horizontalSpeed The horizontal knockback speed.
 * @param verticalSpeed The vertical knockback speed.
 */
void Player::KnockBackFrom(Unit* target, float horizontalSpeed, float verticalSpeed)
{
    float angle = this == target ? GetOrientation() + M_PI_F : target->GetAngle(this);
    GetSession()->SendKnockBack(angle, horizontalSpeed, verticalSpeed);
}

/**
 * @brief Evaluates whether the player can use an area trigger into another map.
 *
 * @param at The area trigger being used.
 * @param miscRequirement Output requirement data for failure messaging.
 * @return The evaluated area lock status.
 */
AreaLockStatus Player::GetAreaTriggerLockStatus(AreaTrigger const* at, uint32& miscRequirement)
{
    miscRequirement = 0;

    if (!at)
    {
        return AREA_LOCKSTATUS_UNKNOWN_ERROR;
    }

    MapEntry const* mapEntry = sMapStore.LookupEntry(at->target_mapId);
    if (!mapEntry)
    {
        return AREA_LOCKSTATUS_UNKNOWN_ERROR;
    }

    bool isRegularTargetMap = !mapEntry->IsDungeon() || GetDifficulty() == REGULAR_DIFFICULTY;

    // Heroic allowed only on TBC instances
    if (!isRegularTargetMap && mapEntry->IsDungeon() && mapEntry->Expansion() != 1)
    {
        return AREA_LOCKSTATUS_MISSING_DIFFICULTY;
    }

    // Expansion requirement
    if (GetSession()->Expansion() < mapEntry->Expansion())
    {
        miscRequirement = mapEntry->Expansion();
        return AREA_LOCKSTATUS_INSUFFICIENT_EXPANSION;
    }

    // Gamemaster can always enter
    if (isGameMaster())
    {
        return AREA_LOCKSTATUS_OK;
    }

    // Level Requirements
    if (getLevel() < at->requiredLevel && !sWorld.getConfig(CONFIG_BOOL_INSTANCE_IGNORE_LEVEL))
    {
        miscRequirement = at->requiredLevel;
        return AREA_LOCKSTATUS_TOO_LOW_LEVEL;
    }
    if (!isRegularTargetMap && !sWorld.getConfig(CONFIG_BOOL_INSTANCE_IGNORE_LEVEL) && getLevel() < uint32(maxLevelForExpansion[mapEntry->Expansion()]))
    {
        miscRequirement = maxLevelForExpansion[mapEntry->Expansion()];
        return AREA_LOCKSTATUS_TOO_LOW_LEVEL;
    }

    // Raid Requirements
    if (mapEntry->IsRaid() && !sWorld.getConfig(CONFIG_BOOL_INSTANCE_IGNORE_RAID))
        if (!GetGroup() || !GetGroup()->isRaidGroup())
        {
            return AREA_LOCKSTATUS_RAID_LOCKED;
        }

    // Item Requirements: must have requiredItem OR requiredItem2, report the first one that's missing
    if (at->requiredItem)
    {
        if (!HasItemCount(at->requiredItem, 1) &&
            (!at->requiredItem2 || !HasItemCount(at->requiredItem2, 1)))
        {
            miscRequirement = at->requiredItem;
            return AREA_LOCKSTATUS_MISSING_ITEM;
        }
    }
    else if (at->requiredItem2 && !HasItemCount(at->requiredItem2, 1))
    {
        miscRequirement = at->requiredItem2;
        return AREA_LOCKSTATUS_MISSING_ITEM;
    }
    // Heroic item requirements
    if (!isRegularTargetMap && at->heroicKey)
    {
        if (!HasItemCount(at->heroicKey, 1) && (!at->heroicKey2 || !HasItemCount(at->heroicKey2, 1)))
        {
            miscRequirement = at->heroicKey;
            return AREA_LOCKSTATUS_MISSING_ITEM;
        }
    }
    else if (!isRegularTargetMap && at->heroicKey2 && !HasItemCount(at->heroicKey2, 1))
    {
        miscRequirement = at->heroicKey2;
        return AREA_LOCKSTATUS_MISSING_ITEM;
    }

    // Quest Requirements
    if (isRegularTargetMap && at->requiredQuest && !GetQuestRewardStatus(at->requiredQuest))
    {
        miscRequirement = at->requiredQuest;
        return AREA_LOCKSTATUS_QUEST_NOT_COMPLETED;
    }
    if (!isRegularTargetMap && at->requiredQuestHeroic && !GetQuestRewardStatus(at->requiredQuestHeroic))
    {
        miscRequirement = at->requiredQuestHeroic;
        return AREA_LOCKSTATUS_QUEST_NOT_COMPLETED;
    }

    // If the map is not created, assume it is possible to enter it.
    DungeonPersistentState* state = GetBoundInstanceSaveForSelfOrGroup(at->target_mapId);
    Map* map = sMapMgr.FindMap(at->target_mapId, state ? state->GetInstanceId() : 0);

    // Map's state check
    if (map && map->IsDungeon())
    {
        // can not enter if the instance is full (player cap), GMs don't count
        if (((DungeonMap*)map)->GetPlayersCountExceptGMs() >= ((DungeonMap*)map)->GetMaxPlayers())
        {
            return AREA_LOCKSTATUS_INSTANCE_IS_FULL;
        }

        // In Combat check
        if (map && map->GetInstanceData() && map->GetInstanceData()->IsEncounterInProgress())
        {
            return AREA_LOCKSTATUS_ZONE_IN_COMBAT;
        }

        // Bind Checks
        InstancePlayerBind* pBind = GetBoundInstance(at->target_mapId, GetDifficulty());
        if (pBind && pBind->perm && pBind->state != state)
        {
            return AREA_LOCKSTATUS_HAS_BIND;
        }
        if (pBind && pBind->perm && pBind->state != map->GetPersistentState())
        {
            return AREA_LOCKSTATUS_HAS_BIND;
        }
    }

    return AREA_LOCKSTATUS_OK;
};

/**
 * @brief Computes rested experience gained over a period of time.
 *
 * @param timePassed The elapsed time in seconds.
 * @param offline True when the gain is being computed for offline time.
 * @param inRestPlace True when the player is in a rest area while offline.
 * @return The amount of rested experience bonus gained.
 */
float Player::ComputeRest(time_t timePassed, bool offline /*= false*/, bool inRestPlace /*= false*/)
{
    // Every 8h in resting zone we gain a bubble
    // A bubble is 5% of the total xp so there is 20 bubbles
    // So we gain (total XP/20 every 8h) (8h = 288800 sec)
    // (TotalXP/20)/28800; simplified to (TotalXP/576000) per second
    // Client automatically double the value sent so we have to divide it by 2
    // So final formula (TotalXP/1152000)
    float bonus = timePassed * (GetUInt32Value(PLAYER_NEXT_LEVEL_XP) / 1152000.0f); // Get the gained rest xp for given second
    if (!offline)
    {
        bonus *= sWorld.getConfig(CONFIG_FLOAT_RATE_REST_INGAME);                   // Apply the custom setting
    }
    else
    {
        if (inRestPlace)
        {
            bonus *= sWorld.getConfig(CONFIG_FLOAT_RATE_REST_OFFLINE_IN_TAVERN_OR_CITY);
        }
        else
        {
            bonus *= sWorld.getConfig(CONFIG_FLOAT_RATE_REST_OFFLINE_IN_WILDERNESS) / 4.0f; // bonus is reduced by 4 when not in rest place
        }
    }
    return bonus;
}
