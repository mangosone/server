/**
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * MaNGOS is a full featured server for World of Warcraft, supporting
 * the following clients: 1.12.x, 2.4.3, 3.3.5a, 4.3.4a and 5.4.8
 *
 * Copyright (C) 2005-2026 MaNGOS <https://www.getmangos.eu>
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
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 *
 * World of Warcraft, and all World of Warcraft or Warcraft art, images,
 * and lore are copyrighted by Blizzard Entertainment, Inc.
 */



#include "Player.h"
#include "Language.h"
#include "Database/DatabaseEnv.h"
#include "Log.h"
#include "Opcodes.h"
#include "World.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include "UpdateMask.h"
#include "ObjectMgr.h"
#include "DBCStores.h"
#include "MapManager.h"
#include <cstdlib>
#include <sstream>
#include <string>

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
        m_taximask[index] = sTaxiNodesMask[index] & uint32(std::strtoul((*iter).c_str(), NULL, 10));
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
        uint32 node = uint32(std::strtoul(iter->c_str(), NULL, 10));
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
 * @brief Welds the booked legs that share a mount model into one flyable route.
 *
 * @param firstPath  Taxi path id of the leg about to be flown.
 * @param startNode  First node of that leg to fly from.
 * @param mount      Mount model already in use for this leg; legs that want another one end it.
 * @param route      Receives the concatenated nodes, already sliced to startNode.
 * @param junctions  Receives the route index of each hub that is now flown through.
 */
void Player::BuildTaxiRoute(uint32 firstPath, uint32 startNode, uint32 mount,
                            TaxiPathNodeList& route, std::vector<uint32>& junctions) const
{
    route.clear();
    junctions.clear();

    if (firstPath >= sTaxiPathNodesByPath.size())
    {
        return;
    }

    std::vector<TaxiPathNodeEntry const*> nodes;

    TaxiPathNodeList const& first = sTaxiPathNodesByPath[firstPath];
    bool mergeable = true;

    for (size_t i = startNode; i < first.size(); ++i)
    {
        nodes.push_back(&first[i]);
        if (first[i].ContinentID != GetMapId())
        {
            // A leg that leaves this map keeps its own spline: the teleport handshake in
            // HandleMoveSplineDoneOpcode indexes the very leg it is flying.
            mergeable = false;
        }
    }

    if (nodes.empty())
    {
        return;
    }

    for (size_t leg = 1; mergeable && leg + 1 < m_taxi.GetDestinationCount(); ++leg)
    {
        const uint32 src = m_taxi.GetDestination(leg);

        // A hub that hands out a different mount model is where the flight is SUPPOSED to
        // break -- you land, dismount and remount there. Every other hub is flown through.
        // Same lookup flags HandleMoveSplineDoneOpcode would have used for this leg.
        if (sObjectMgr.GetTaxiMountDisplayId(src, GetTeam()) != mount)
        {
            break;
        }

        uint32 path, cost;
        sObjectMgr.GetTaxiPath(src, m_taxi.GetDestination(leg + 1), path, cost);

        if (!path || path >= sTaxiPathNodesByPath.size())
        {
            break;
        }

        TaxiPathNodeList const& next = sTaxiPathNodesByPath[path];
        if (next.size() < 2)
        {
            break;
        }

        bool sameMap = true;
        for (size_t i = 0; i < next.size() && sameMap; ++i)
        {
            sameMap = (next[i].ContinentID == GetMapId());
        }

        if (!sameMap)
        {
            break;
        }

        junctions.push_back(uint32(nodes.size() - 1));

        // Node 0 is the hub itself, already the last node of the leg just appended.
        for (size_t i = 1; i < next.size(); ++i)
        {
            nodes.push_back(&next[i]);
        }
    }

    if (junctions.empty())
    {
        return;
    }

    route.resize(uint32(nodes.size()));
    for (size_t i = 0; i < nodes.size(); ++i)
    {
        route.set(i, TaxiPathNodePtr(nodes[i]));
    }
}
