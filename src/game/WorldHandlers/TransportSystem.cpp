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

/*
 * @addtogroup TransportSystem
 * @{
 *
 * @file TransportSystem.cpp
 * This file contains the code needed for MaNGOS to provide abstract support for transported entities
 * Currently implemented
 * - Calculating between local and global coords
 * - Abstract storage of passengers (added by BoardPassenger, UnboardPassenger)
 */

#include "TransportSystem.h"
#include "Unit.h"
#include "Creature.h"
#include "GameTime.h"
#include "MapManager.h"

#include <utility>
#include <vector>

/* **************************************** TransportBase ****************************************/

TransportBase::TransportBase(WorldObject* owner) :
    m_owner(owner),
    m_lastPosition(owner->GetPositionX(), owner->GetPositionY(), owner->GetPositionZ(), owner->GetOrientation()),
    m_sinO(sin(m_lastPosition.o)),
    m_cosO(cos(m_lastPosition.o)),
    m_updatePositionsTimer(500)
{
    MANGOS_ASSERT(m_owner);
}

TransportBase::~TransportBase()
{
    MANGOS_ASSERT(m_passengers.size() == 0);
}

// Update every now and then (after some change of transporter's position)
// This is used to calculate global positions (which don't have to be exact, they are only required for some server-side calculations
void TransportBase::Update(uint32 diff)
{
    if (m_updatePositionsTimer < diff)
    {
        if (fabs(m_owner->GetPositionX() - m_lastPosition.x) +
                fabs(m_owner->GetPositionY() - m_lastPosition.y) +
                fabs(m_owner->GetPositionZ() - m_lastPosition.z) > 1.0f ||
                MapManager::NormalizeOrientation(m_owner->GetOrientation() - m_lastPosition.o) > 0.01f)
            UpdateGlobalPositions();

        m_updatePositionsTimer = 500;
    }
    else
    {
        m_updatePositionsTimer -= diff;
    }
}

// Update the global positions of all passengers
void TransportBase::UpdateGlobalPositions()
{
    Position pos(m_owner->GetPositionX(), m_owner->GetPositionY(),
                 m_owner->GetPositionZ(), m_owner->GetOrientation());

    // Recomputed unconditionally: two trig calls, and the guard that used to stand here
    // asked NormalizeOrientation(delta) > 0.01f, which is true for almost any delta
    // (Normalize maps a small negative rotation to nearly 2*PI). It never saved anything
    // and it would go badly wrong the moment a caller relied on it to be exact -- which
    // the pose recovery, which Relocates the vessel to a client-observed yaw, now does.
    m_sinO = sin(pos.o);
    m_cosO = cos(pos.o);

    // Over a SNAPSHOT, and this is not defensive habit -- the live map cannot survive the walk.
    //
    // Refreshing a GRID-RESIDENT passenger (a pet) goes out through Map::CreatureRelocation,
    // and the grid is entitled to REFUSE it: the deck may have carried it into a cell that will
    // not take it. When that happens the minion steps off the boat and unboards itself
    // (Map::CreatureRespawnRelocation) -- erasing, re-entrantly, the very element this loop is
    // standing on. Iterating m_passengers directly would then walk a dead iterator.
    //
    // Crew never do this: their refresh is a plain Relocate that touches no grid at all. But
    // they share the loop, so the loop has to be safe for the passenger that does.
    std::vector<std::pair<WorldObject*, Position> > refresh;
    refresh.reserve(m_passengers.size());

    for (PassengerMap::const_iterator itr = m_passengers.begin(); itr != m_passengers.end(); ++itr)
    {
        refresh.push_back(std::make_pair(itr->first,
                                         Position(itr->second->GetLocalPositionX(),
                                                  itr->second->GetLocalPositionY(),
                                                  itr->second->GetLocalPositionZ(),
                                                  itr->second->GetLocalOrientation())));
    }

    for (std::pair<WorldObject*, Position> const& entry : refresh)
    {
        // An earlier iteration's relocation may have put this one ashore.
        if (m_passengers.find(entry.first) == m_passengers.end())
        {
            continue;
        }

        UpdateGlobalPositionOf(entry.first, entry.second.x, entry.second.y,
                               entry.second.z, entry.second.o);
    }

    m_lastPosition = pos;
}

/**
 * @brief Refresh a passenger's WORLD position -- which is a cache, and only a cache.
 *
 * A passenger does not live in the world. It lives in the vessel, which is a little map
 * of its own: the vessel owns the passenger list, the passenger update tick, the deck
 * mesh they collide against and the one broadcast that tells observers about them. None
 * of that is the world grid's business, and the crew are not citizens of it.
 *
 * So this is a plain Relocate: it stamps the world token that OFF-SHIP questions need
 * ("how far is that deckhand from the pier I am standing on") and touches nothing else.
 *
 * It deliberately does NOT call Map::CreatureRelocation. That would re-file the crew
 * into the map cell for a coordinate the server only ESTIMATES -- the client runs the
 * vessel's Catmull path, the server does not -- and would hand the crew's visibility to
 * a grid that is not the thing managing it. Both halves of that are wrong.
 */
void TransportBase::UpdateGlobalPositionOf(WorldObject* passenger, float lx, float ly, float lz, float lo) const
{
    float gx, gy, gz, go;
    CalculateGlobalPositionOf(lx, ly, lz, lo, gx, gy, gz, go);

    // The client parents a unit to a vessel from that unit's MOVEMENT INFO and nothing else:
    // MovementInfo::Write emits the transport block only when MOVEFLAG_ONTRANSPORT is set.
    // A server-driven passenger never sets it on its own -- only a player's own client does,
    // in the packets it sends us -- so without this the crew go out as free units carrying a
    // bare world position. The client then has no idea they belong to the ship: it plants
    // them where that position says, the vessel sails on, and the deckhands are left standing
    // on the open sea.
    //
    // The deck offset is the authoritative coordinate for these passengers (the world one is
    // a derived cache -- see the comment above), so it is what gets stamped. This covers a
    // boarded MINION exactly as it covers the crew: to the client a pet is just as much on
    // the deck as a deckhand, and must ride with the ship.
    if (passenger->isType(TYPEMASK_UNIT))
    {
        Unit* boarded = static_cast<Unit*>(passenger);

        boarded->m_movementInfo.AddMovementFlag(MOVEFLAG_ONTRANSPORT);
        boarded->m_movementInfo.SetTransportData(m_owner->GetObjectGuid(), lx, ly, lz, lo,
                                                 GameTime::GetGameTimeMS());
    }

    // A CREW member and a boarded MINION are treated identically here, and that is the
    // whole point of the ship-local model: neither is in a world grid cell, so this is a
    // pure cache write. It stamps the world token that an off-ship distance check needs --
    // and that the pet subsystem's eventual remove-list pass needs to find the creature in
    // a loaded cell (the token tracks the ship, i.e. where the master is standing, which is
    // always loaded) -- and touches nothing else.
    //
    // It deliberately does NOT call Map::CreatureRelocation. A boarded creature is not a
    // citizen of the world grid; it is a passenger of the vessel's own container, and
    // relocating it through the world grid was exactly the "composed world position" that
    // made a moving ship churn the pet's cell membership and blink it out of sight.
    //
    // (A player aboard is not one of these passengers. The player's own client is the sole
    // authority for where the player stands; the server relays that when the player moves,
    // and while the player is STILL there is nothing to relay -- the client keeps drawing it
    // on the deck through the transport parent, needing no help from us. Either way we never
    // server-relocate the player from here. The player stays a grid citizen only so it can
    // SEE and interact with the world; being seen is the ship's job, like any passenger.)
    passenger->Relocate(gx, gy, gz, go);
}

// This rotates the vector (lx, ly) by transporter->orientation
void TransportBase::RotateLocalPosition(float lx, float ly, float& rx, float& ry) const
{
    rx = lx * m_cosO - ly * m_sinO;
    ry = lx * m_sinO + ly * m_cosO;
}

// This rotates the vector (rx, ry) by -transporter->orientation.
//
// NOTE: this is the true inverse rotation, R(-o). The version that stood here before
// negated BOTH terms, which yields -R(o) -- a rotation composed with a point reflection,
// not an inverse. It had no callers, so nothing ever noticed; it does now.
void TransportBase::NormalizeRotatedPosition(float rx, float ry, float& lx, float& ly) const
{
    lx = rx * m_cosO + ry * m_sinO;
    ly = -rx * m_sinO + ry * m_cosO;
}

// Calculate a global position of local positions based on this transporter
void TransportBase::CalculateGlobalPositionOf(float lx, float ly, float lz, float lo, float& gx, float& gy, float& gz, float& go) const
{
    RotateLocalPosition(lx, ly, gx, gy);
    gx += m_owner->GetPositionX();
    gy += m_owner->GetPositionY();

    gz = lz + m_owner->GetPositionZ();
    go = MapManager::NormalizeOrientation(lo + m_owner->GetOrientation());
}

// Calculate a local position from a global one. The exact inverse of the above.
void TransportBase::CalculateLocalPositionOf(float gx, float gy, float gz, float go, float& lx, float& ly, float& lz, float& lo) const
{
    NormalizeRotatedPosition(gx - m_owner->GetPositionX(), gy - m_owner->GetPositionY(), lx, ly);

    lz = gz - m_owner->GetPositionZ();
    lo = MapManager::NormalizeOrientation(go - m_owner->GetOrientation());
}

void TransportBase::UnBoardAllPassengers()
{
    while (!m_passengers.empty())
    {
        UnBoardPassenger(m_passengers.begin()->first);
    }
}

void TransportBase::BoardPassenger(WorldObject* passenger, float lx, float ly, float lz, float lo,
                                   bool minion)
{
    TransportInfo* transportInfo = new TransportInfo(passenger, this, lx, ly, lz, lo, minion);

    // Insert our new passenger
    m_passengers.insert(PassengerMap::value_type(passenger, transportInfo));

    // The passenger needs fast access to transportInfo
    passenger->SetTransportInfo(transportInfo);
}

void TransportBase::UnBoardPassenger(WorldObject* passenger)
{
    PassengerMap::iterator itr = m_passengers.find(passenger);

    if (itr == m_passengers.end())
    {
        return;
    }

    // Set passengers transportInfo to NULL
    passenger->SetTransportInfo(NULL);

    // And stop telling the client it is standing on us -- otherwise it stays welded to a deck
    // it has stepped off, and every position we send it is read as a deck offset.
    if (passenger->isType(TYPEMASK_UNIT))
    {
        Unit* leaving = static_cast<Unit*>(passenger);

        leaving->m_movementInfo.RemoveMovementFlag(MOVEFLAG_ONTRANSPORT);
        leaving->m_movementInfo.ClearTransportData();
    }

    // Delete transportInfo
    delete itr->second;

    // Unboard finally
    m_passengers.erase(itr);
}

/* **************************************** TransportInfo ****************************************/

TransportInfo::TransportInfo(WorldObject* owner, TransportBase* transport, float lx, float ly, float lz, float lo,
                             bool minion) :
    m_owner(owner),
    m_transport(transport),
    m_localPosition(lx, ly, lz, lo),
    m_minion(minion)
{
    MANGOS_ASSERT(owner && m_transport);
}

void TransportInfo::SetLocalPosition(float lx, float ly, float lz, float lo)
{
    m_localPosition.x = lx;
    m_localPosition.y = ly;
    m_localPosition.z = lz;
    m_localPosition.o = lo;

    // Update global position
    m_transport->UpdateGlobalPositionOf(m_owner, lx, ly, lz, lo);
}

/*! @} */
