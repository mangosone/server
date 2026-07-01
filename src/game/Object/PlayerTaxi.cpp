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
 * @file PlayerTaxi.cpp
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
