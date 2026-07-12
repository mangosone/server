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

#ifndef TRANSPORTS_H
#define TRANSPORTS_H

#include "GameObject.h"
#include "GridDefines.h"
#include "TransportSystem.h"
#include "terrain/Geometry.hpp"
#include "terrain/ICollisionModel.hpp"

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <vector>

class Creature;
class MovementInfo;

/**
 * @brief An MO transport -- a ship, a zeppelin -- which on this server is A LITTLE MAP.
 *
 * It is worth being exact about what a transport is NOT, because every core that has
 * tried to give one a living crew has run onto the same rock.
 *
 * THE SERVER DOES NOT KNOW WHERE A TRANSPORT IS. The client interpolates the vessel
 * along the Catmull-Rom curve through its taxi path; the server never reproduces that
 * curve, and never has. The waypoint walk in Update() exists for exactly two purposes:
 * to know when to fire the map seam, and to keep the vessel near enough the right place
 * to find its observers. It is a tracking token. It is not a pose, and any code that
 * treats it as one -- notably, transforming the hull into world space and firing rays at
 * it -- is computing collisions against a ship that is not there.
 *
 * So a transport is not a body IN the world. It IS a world: a small one, at rest in its
 * own coordinates, advertised at a drifting point on a map. And it owns everything a Map
 * owns:
 *
 *     Map                                Transport
 *     --------------------------------   ---------------------------------------------
 *     grid cells full of objects         its passenger list          (TransportBase)
 *     the ObjectUpdater cell visit       the passenger update tick   (UpdateCrew)
 *     FusedTerrain + DynamicCollision    ONE baked deck mesh, queried UNTRANSFORMED
 *     visibility from cell neighbours    one broadcast to the vessel's observers
 *
 * A crew member therefore has NO GRID CELL, is never CreatureRelocated, and is ticked by
 * the ship. Its deck-local position is the truth. The world position it also carries is
 * a cache, maintained for one reason only: so that somebody standing on the pier can
 * measure a distance to it.
 *
 * Players aboard are the opposite case and stay exactly as they were: ordinary grid
 * citizens, authoritative on their own position, sent by their own client. That
 * asymmetry is a gift rather than an inconsistency -- see ObservePose.
 */
class Transport : public GameObject, public TransportBase
{
    public:
        explicit Transport();
        ~Transport();

        bool Create(uint32 guidlow, uint32 mapid, float x, float y, float z, float ang, uint32 animprogress);
        bool GenerateWaypoints(uint32 pathid, std::set<uint32>& mapids);
        void Update(uint32 update_diff, uint32 p_time) override;

        // --- Players aboard: normal grid citizens, driven by their own client --------
        bool AddPassenger(Player* passenger);
        bool RemovePassenger(Player* passenger);

        typedef std::set<Player*> PlayerSet;
        PlayerSet const& GetPlayerPassengers() const { return m_playerPassengers; }

        // --- Crew: creatures that live in the vessel's frame -------------------------

        /// Spawn the vessel's crew from `creature_transport`. Their coordinates in that
        /// table are DECK OFFSETS -- the only coordinates a crew member ever really has.
        void LoadCrew();

        /// Board a creature at a deck-local position. It leaves the grid (it was never
        /// meant to be in one) and becomes ours to tick, broadcast and collide.
        bool BoardCreature(Creature* crew, float lx, float ly, float lz, float lo);
        void UnBoardCreature(Creature* crew);

        std::vector<Creature*> const& GetCrew() const { return m_crew; }

        /**
         * @brief THE SHIP IS THE CREW'S GRID CELL.
         *
         * A crew member is in no map cell, so nothing that walks cells can find it -- a
         * fireball on the deck would pass straight through it and two deckhands would never
         * notice each other. Putting them back in a cell is not the answer: their world
         * position is an estimate of a pose we do not know, and a moving ship would churn
         * cells all voyage.
         *
         * So their GridReference is linked into a container the VESSEL owns, and a search
         * near the vessel visits that container as if it were one more cell
         * (MaNGOS::VisitTransportCrew). Every searcher works on it unmodified, because they
         * all take a GridRefManager<T>.
         */
        CreatureMapType& GetCrewMap() { return m_crewMap; }
        bool HasCrew() const { return !m_crew.empty(); }

        /**
         * @brief Keep every player passenger's MINIONS aboard with them -- pet, mini-pet,
         *        guardians, totems.
         *
         * A minion is neither crew nor player, and the distinction matters. It is
         * server-driven like crew, so it needs the deck frame: without it, it resolves its Z
         * against the sea floor under the hull and tries to path to its master across open
         * ocean, where there is no navmesh -- so the route fails, the leg is refused, and it
         * stands on the pier for the rest of the voyage.
         *
         * But it belongs to the WORLD, not to us. It fights, it is targeted, it is its
         * master's. So it is boarded as a GRID RESIDENT: it keeps its cell, we neither tick
         * it nor broadcast it, and the only thing it takes from the vessel is a floor.
         *
         * Reconciled every tick rather than hooked onto the boarding event, because there
         * are half a dozen ways a minion comes to be standing on a deck -- its master walks
         * aboard, it is summoned at sea, its master logs in mid-voyage, a totem is dropped
         * on the forecastle -- and only one way to be sure we caught them all.
         */
        void UpdateMinions();

        /// Board one minion, if it is over the deck. See UpdateMinions.
        void BoardMinion(Unit* minion);

        /**
         * @brief RECOVER THE VESSEL'S TRUE POSE FROM A PLAYER STANDING ON IT.
         *
         * The lie has a cure, and the client hands it to us unasked. A player on a
         * transport sends MOVEFLAG_ONTRANSPORT, and its movement packet carries BOTH
         * coordinate systems -- the world position `pos` AND the deck offset `t_pos`,
         * orientations included. That is over-determined, so we can just solve for the
         * vessel:
         *
         *     yaw = pos.o - t_pos.o
         *     org = pos   - R(yaw) * t_pos
         *
         * Every packet from anybody aboard re-derives the ship's EXACT pose -- the real
         * one, off the Catmull path the client is drawing. And when nobody is aboard,
         * nobody is looking, so the stale waypoint estimate is good enough.
         *
         * The recovered pose is a bookkeeping value: it fixes the world token the grid
         * and the broadcast need. It never enters the deck frame, and the crew never see
         * it.
         */
        void ObservePose(MovementInfo const& mi);

        /// True while a player aboard is still telling us where the vessel really is, so the
        /// pose (and everything composed from it) is EXACT. False once the last of them has
        /// stepped off and the waypoint token has taken back over -- at which point the pose
        /// is a guess, and nobody is close enough to care.
        bool HasFreshPose() const;

        // --- The deck: local-only collision ------------------------------------------
        //
        // Every one of these speaks DECK-LOCAL coordinates, in and out, and raycasts the
        // baked mesh in the space it was baked in. No world transform is applied, ever,
        // because there is no trustworthy world transform to apply. This is the whole
        // reason a crew can exist here and cannot exist on other cores.

        bool HasDeck() const { return m_deck && !m_deck->empty(); }

        /// The deck under a local point: the highest deck surface at or below it, found
        /// by dropping a ray from `searchUp` above. Nothing when there is no deck there
        /// -- which is a REJECTION, not an invitation to fall back to world terrain.
        std::optional<float> DeckHeightAt(float lx, float ly, float lz,
                                          float searchUp, float searchDown) const;

        /// True when the vessel's own geometry -- a bulkhead, a mast, a crate -- stands
        /// between two local points.
        bool IsDeckBlocked(Geometry::Vector3 const& from, Geometry::Vector3 const& to) const;

        /// The hull's extent, from the baked mesh. Used to size the observer search: a
        /// ship is not a point, and a player at the bow must still see the stern.
        float GetHullRadius() const { return m_hullRadius; }

        // --- Visibility: the vessel advertises itself, and its crew with it -----------
        //
        // A transport is in no grid cell, so the grid's visibility pass has never had
        // anything to say about it -- which is why it used to be blasted to every player
        // on the map, once, and never reconsidered. It now keeps its own observer set from
        // the cells AROUND it, exactly as a Map keeps a player's from the cells around
        // them, and the crew ride along in the same packet. Nothing else knows the crew
        // exist, so nothing else could advertise them.

        /// How far the vessel is worth hearing about: visibility distance, widened by the
        /// hull's real extent (a ship is not a point) and by a margin for the fact that our
        /// own position is only as fresh as the last player who told us where we are.
        float GetBroadcastRadius() const;

        /// Append the crew's create blocks to a packet that is already carrying ours.
        void AppendCrewCreateBlocks(UpdateData& data, Player* observer);

        void AddObserver(Player* observer);
        void RemoveObserver(Player* observer);

        /// Keep what this vessel has at an observer's client from being swept away by the
        /// grid's visibility pass, which cannot see into a cell that does not exist.
        void RetainAtClient(Player* observer, GuidSet& clientGuids) const;

    private:
        struct WayPoint
        {
            WayPoint() : mapid(0), x(0), y(0), z(0), teleport(false) {}
            WayPoint(uint32 _mapid, float _x, float _y, float _z, bool _teleport, uint32 _arrivalEventID = 0, uint32 _departureEventID = 0)
                : mapid(_mapid), x(_x), y(_y), z(_z), teleport(_teleport),
                  arrivalEventID(_arrivalEventID), departureEventID(_departureEventID)
            {
            }

            uint32 mapid;
            float x;
            float y;
            float z;
            bool teleport;
            uint32 arrivalEventID;
            uint32 departureEventID;
        };

        typedef std::map<uint32, WayPoint> WayPointMap;

        WayPointMap::const_iterator m_curr;
        WayPointMap::const_iterator m_next;
        uint32 m_pathTime;
        uint32 m_timer;

        PlayerSet m_playerPassengers;

        /// The baked hull, in ITS OWN space. Never posed into the world.
        std::shared_ptr<const world::terrain::ICollisionModel> m_deck;
        float m_hullRadius;

        /// Crew, in board order, so the tick is deterministic. TransportBase owns the
        /// passenger->TransportInfo map; this is just the iteration order.
        std::vector<Creature*> m_crew;

        /// The same crew, as a grid container -- this vessel's "cell". See GetCrewMap.
        CreatureMapType m_crewMap;

        /// How long since a player aboard last told us where the vessel really is. Past
        /// POSE_TRUST_MS the observation is stale and the waypoint token takes back over.
        uint32 m_poseAge;

        /// The players currently being told about this vessel and its crew.
        GuidSet m_observers;
        uint32 m_visibilityTimer;

    public:
        WayPointMap m_WayPoints;
        uint32 m_nextNodeTime;
        uint32 m_period;

    private:
        void TeleportTransport(uint32 newMapid, float x, float y, float z);

        /// Carry the crew across a map seam with the ship. They stay boarded; only their map
        /// registration moves.
        void MoveCrewToMap(Map* newMap);
        void UpdateForMap(Map const* map);
        void DoEventIfAny(WayPointMap::value_type const& node, bool departure);
        void MoveToNextWayPoint();                          // move m_next/m_cur to next points

        /// The vessel's own update tick over its crew -- this is the ObjectUpdater that
        /// the grid would have run, had the crew been in a grid.
        void UpdateCrew(uint32 diff);

        /// The vessel's own visibility pass -- this is the VisibleNotifier that the grid
        /// would have run, had the vessel been in a grid.
        void UpdateVisibility(uint32 diff);
};
#endif
