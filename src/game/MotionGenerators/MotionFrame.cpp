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

#include "MotionFrame.h"
#include "Map.h"
#include "MapManager.h"
#include "PathFinder.h"
#include "Player.h"
#include "TransportSystem.h"
#include "Transports.h"
#include "Unit.h"
#include "Util.h"

#include <algorithm>
#include <cmath>

namespace Motion
{
    namespace
    {
        /// The default ceiling on a routed path, in yards. Re-applied on every query
        /// because the limit is sticky on a reused PathFinder, so an unlimited request
        /// after a capped one (a flee leg) would otherwise inherit the cap.
        constexpr float DEFAULT_PATH_LENGTH =
            float(MAX_POINT_PATH_LENGTH) * SMOOTH_PATH_STEP_SIZE;

        /**
         * @brief The world frame's router: the Detour navmesh, behind IPathQuery.
         */
        class WorldPathQuery final : public IPathQuery
        {
            public:
                explicit WorldPathQuery(Unit const& mover) : m_path(&mover) {}

                bool Calculate(Vector3 const& start, Vector3 const& goal,
                               bool forceDestination, float lengthLimit) override
                {
                    m_path.setPathLengthLimit(lengthLimit > 0.0f ? lengthLimit
                                                                 : DEFAULT_PATH_LENGTH);

                    if (!m_path.calculate(start.x, start.y, start.z,
                                          goal.x, goal.y, goal.z, forceDestination))
                    {
                        return false;
                    }

                    // A failed route still leaves a straight-line shortcut in the
                    // points, which some movement kinds want and others refuse -- so
                    // report it (Failed) rather than deciding here.
                    return m_path.getPath().size() >= 2;
                }

                PointsArray const& Points() const override { return m_path.getPath(); }

                bool Failed() const override
                {
                    return (m_path.getPathType() & PATHFIND_NOPATH) != 0;
                }

                bool Routed() const override
                {
                    return (m_path.getPathType() &
                            (PATHFIND_NOPATH | PATHFIND_NOT_USING_PATH)) == 0;
                }

                bool Reachable() const override
                {
                    return (m_path.getPathType() & PATHFIND_NORMAL) != 0;
                }

            private:
                /// getPath() is non-const on PathFinder, though reading the routed
                /// points does not mutate the query as far as callers are concerned.
                mutable PathFinder m_path;
        };

        /**
         * @brief The world's own coordinate system: navmesh routing, terrain heights.
         *        What the movement code always did, now behind the frame interface so
         *        a transport frame can replace it wholesale.
         */
        class WorldFrame final : public IMotionFrame
        {
            public:
                FrameKind Kind() const override { return FrameKind::World; }

                std::unique_ptr<IPathQuery> CreatePathQuery(Unit const& mover) const override
                {
                    return std::make_unique<WorldPathQuery>(mover);
                }

                Vector3 MoverPosition(Unit const& mover) const override
                {
                    Vector3 p;
                    mover.GetPosition(p.x, p.y, p.z);
                    return p;
                }

                /// The world frame IS world space, so both conversions are the identity.
                /// This is what lets every generator convert its anchors unconditionally
                /// and cost nothing for the 99.99% of units that are not on a boat.
                Vector3 FromWorld(Unit const& /*mover*/, Vector3 const& world) const override
                {
                    return world;
                }

                Vector3 ToWorld(Unit const& /*mover*/, Vector3 const& local) const override
                {
                    return local;
                }

                Vector3 ObjectPosition(Unit const& /*mover*/, WorldObject const& obj) const override
                {
                    Vector3 p;
                    obj.GetPosition(p.x, p.y, p.z);
                    return p;
                }

                float ObjectOrientation(Unit const& /*mover*/, WorldObject const& obj) const override
                {
                    return obj.GetOrientation();
                }

                Vector3 NearPoint(Unit const& mover, WorldObject const& target,
                                  float searcherBounding, float distance2d,
                                  float absAngle) const override
                {
                    Vector3 p;
                    target.GetNearPoint(&mover, p.x, p.y, p.z, searcherBounding,
                                        distance2d, absAngle);
                    return p;
                }

                std::optional<Vector3> RandomPoint(Unit& mover, Vector3 const& centre,
                                                   float radius) const override
                {
                    Vector3 p = centre;
                    if (!mover.GetMap()->GetReachableRandomPosition(&mover, p.x, p.y, p.z, radius))
                    {
                        return std::nullopt;
                    }

                    return p;
                }

                std::optional<Vector3> GroundPoint(Unit& mover, Vector3 const& from,
                                                   Vector3 const& guess) const override
                {
                    Map* map = mover.GetMap();

                    Vector3 p = guess;
                    if (!map->GetHeightInRange(p.x, p.y, p.z))
                    {
                        return std::nullopt;
                    }

                    // A player is pulled back to the first obstruction on the way, so a
                    // feared player cannot be shoved through a wall. The half-yard lift
                    // avoids false hits against the ground itself and small clutter.
                    if (mover.GetTypeId() == TYPEID_PLAYER)
                    {
                        float testZ = p.z + 0.5f;
                        if (map->GetHitPosition(from.x, from.y, from.z + 0.5f,
                                                p.x, p.y, testZ, -0.1f))
                        {
                            p.z = testZ;
                            if (!map->GetHeightInRange(p.x, p.y, p.z))
                            {
                                return std::nullopt;
                            }
                        }
                    }

                    return p;
                }
        };

        /* ***************************** The transport frame **************************
         *
         * A boarded unit is not standing on the map. It is standing in a little world of
         * its own, which happens to be advertised at a drifting point on one. Everything
         * below therefore speaks DECK-LOCAL coordinates, in and out, and every collision
         * question is answered by the vessel's baked mesh IN THE SPACE IT WAS BAKED IN.
         *
         * No world transform is ever applied to that mesh, and this is not an
         * optimisation -- it is the only correct thing to do. The server does not know
         * where the ship is: the client interpolates it along a Catmull-Rom curve that
         * the server never reproduces. Posing the hull into world coordinates and firing
         * rays at it -- which is what every other core does, and why none of them can keep
         * a crew on a deck -- computes collisions against a ship that is not there.
         */

        /// The step, in yards, between the samples a deck leg is built from.
        constexpr float DECK_STEP = 2.0f;

        /// Each sample is sought from this far above the last one -- so a low step or a
        /// ramp is found underfoot -- and no further than this below it, so a hatch does
        /// not silently drop the leg onto the deck two levels down.
        constexpr float DECK_SEARCH_UP = 2.0f;
        constexpr float DECK_SEARCH_DOWN = 6.0f;

        /// Chest height for the obstruction probe, so the deck a unit is standing ON is
        /// not itself read as the thing blocking it.
        constexpr float DECK_PROBE_HEIGHT = 1.0f;

        /// Tries before a random deck point is given up on. A deck is small and mostly
        /// clutter; rejecting a bad point is cheaper than being clever about picking one.
        constexpr int DECK_RANDOM_TRIES = 8;

        /// The vessel a boarded unit stands in, or NULL.
        Transport* VesselOf(Unit const& mover)
        {
            TransportInfo* info = mover.GetTransportInfo();
            if (!info)
            {
                return NULL;
            }

            WorldObject* owner = info->GetTransport();
            if (!owner || owner->GetTypeId() != TYPEID_GAMEOBJECT)
            {
                return NULL;
            }

            return static_cast<Transport*>(owner);
        }

        Vector3 LocalPositionOf(TransportInfo const& info)
        {
            return Vector3(info.GetLocalPositionX(),
                           info.GetLocalPositionY(),
                           info.GetLocalPositionZ());
        }

        /**
         * @brief The deck offset of an object aboard `vessel`, if it is aboard at all.
         *
         * Both ways of being on a ship have to be read -- a passenger's TransportInfo and a
         * player's own client-sent offset -- and Transport::LocalPositionOf is where that
         * rule lives. A pet following its master needs the player case: without it the
         * chase would aim at the master's WORLD position brought into the deck frame
         * through a vessel pose we are only estimating, when the exact answer was sitting
         * in the master's movement packet all along.
         */
        std::optional<Vector3> LocalPositionAboard(Transport const& vessel, WorldObject const& obj)
        {
            if (const auto local = vessel.LocalPositionOf(obj))
            {
                return Vector3(local->x, local->y, local->z);
            }

            return std::nullopt;
        }

        std::optional<float> LocalOrientationAboard(Transport const& vessel, WorldObject const& obj)
        {
            if (const auto local = vessel.LocalPositionOf(obj))
            {
                return local->o;
            }

            return std::nullopt;
        }

        /// Drop a deck-local point onto the deck. Local in, local out.
        std::optional<Vector3> DeckDrop(Transport const& vessel, Vector3 const& local)
        {
            const auto z = vessel.DeckHeightAt(local.x, local.y, local.z,
                                               DECK_SEARCH_UP, DECK_SEARCH_DOWN);
            if (!z)
            {
                return std::nullopt;
            }

            return Vector3(local.x, local.y, *z);
        }

        /// Does the vessel's own geometry stand between these two deck points?
        bool DeckBlocked(Transport const& vessel, Vector3 const& from, Vector3 const& to)
        {
            return vessel.IsDeckBlocked(Vector3(from.x, from.y, from.z + DECK_PROBE_HEIGHT),
                                        Vector3(to.x, to.y, to.z + DECK_PROBE_HEIGHT));
        }

        /**
         * @brief The deck's router.
         *
         * There is no navmesh on a ship, so a deck leg is the straight line to the goal --
         * but SAMPLED, with every sample dropped onto the deck, and cut short at the first
         * one that has no deck under it or that cannot be reached from the sample before.
         *
         * That sampling is the entire value of the thing. It is what makes a leg follow a
         * ramp or a companionway instead of shearing through it, and what stops a
         * deckhand strolling off the bow into the sea.
         */
        class DeckPathQuery final : public IPathQuery
        {
            public:
                explicit DeckPathQuery(Unit const& mover) : m_mover(&mover) {}

                bool Calculate(Vector3 const& start, Vector3 const& goal,
                               bool forceDestination, float /*lengthLimit*/) override
                {
                    m_points.clear();
                    m_failed = true;
                    m_reachable = false;

                    Transport* vessel = VesselOf(*m_mover);
                    if (!vessel)
                    {
                        return false;
                    }

                    m_points.push_back(start);

                    // No baked mesh: there is no deck to walk on, and we must NOT quietly
                    // fall back to the world terrain under the hull -- that is the sea
                    // floor. All we can honestly offer is the straight line, reported as
                    // Failed, exactly as a world route with no navmesh is. The movement
                    // kinds that refuse a shortcut then refuse this too.
                    if (!vessel->HasDeck())
                    {
                        m_points.push_back(goal);
                        return true;
                    }

                    const Vector3 delta = goal - start;
                    const float span = std::sqrt(delta.x * delta.x + delta.y * delta.y);
                    const uint32 steps =
                        std::max<uint32>(1, uint32(std::ceil(span / DECK_STEP)));

                    Vector3 prev = start;
                    bool complete = true;

                    for (uint32 i = 1; i <= steps; ++i)
                    {
                        const float t = float(i) / float(steps);

                        // Sought from the height of the previous sample, so the leg climbs
                        // a ramp one step at a time instead of trying to find the whole
                        // rise from where it started.
                        const Vector3 guess(start.x + delta.x * t,
                                            start.y + delta.y * t,
                                            prev.z);

                        const auto onDeck = DeckDrop(*vessel, guess);
                        if (!onDeck || DeckBlocked(*vessel, prev, *onDeck))
                        {
                            complete = false;   // off the deck, or into something solid
                            break;
                        }

                        m_points.push_back(*onDeck);
                        prev = *onDeck;
                    }

                    if (forceDestination && !complete)
                    {
                        // A pet heeling its master may cheat past the ship's SCENERY -- a
                        // crate, a companionway, whatever its master strolled straight
                        // through. It may not cheat through the SHIP. The spot it lands on
                        // still has to be a spot on the deck, so the cheat skips the
                        // obstruction test and only that; the floor is not negotiable.
                        //
                        // Taking the goal raw -- which is what the world frame safely does,
                        // because a world goal was dropped onto the terrain by GetNearPoint
                        // long before it reached any router -- would walk the pet into a
                        // bulkhead and leave it standing inside the hull, where no client
                        // can draw it. On a deck the sampling above IS the ground
                        // resolution, and NearPoint hands back goals it could not resolve
                        // (see the note there) precisely because it trusts the router to
                        // refuse them.
                        //
                        // No deck under the goal at all -- the master is leaning over the
                        // rail and the heel spot is out over the water -- and the leg simply
                        // stays short: the pet crowds up against the rail, as close as the
                        // deck allows. That is what it does on retail, and it beats swimming.
                        if (const auto onDeck = DeckDrop(*vessel, goal))
                        {
                            m_points.push_back(*onDeck);
                            complete = true;
                        }
                    }

                    if (m_points.size() < 2)
                    {
                        // Not one step was walkable.
                        m_points.push_back(goal);
                        return true;
                    }

                    m_failed = false;
                    m_reachable = complete;
                    return true;
                }

                PointsArray const& Points() const override { return m_points; }
                bool Failed() const override { return m_failed; }

                /// A deck leg is NEVER a route: there is no navmesh to route through, so
                /// it is a straight line that merely follows the floor. Saying so is what
                /// stops waypoint smoothing from welding deck legs together on the
                /// strength of a route it never actually had.
                bool Routed() const override { return false; }

                bool Reachable() const override { return m_reachable; }

            private:
                Unit const* m_mover;
                PointsArray m_points;
                bool m_failed = true;
                bool m_reachable = false;
        };

        class TransportFrame final : public IMotionFrame
        {
            public:
                FrameKind Kind() const override { return FrameKind::Transport; }

                std::unique_ptr<IPathQuery> CreatePathQuery(Unit const& mover) const override
                {
                    return std::make_unique<DeckPathQuery>(mover);
                }

                Vector3 MoverPosition(Unit const& mover) const override
                {
                    TransportInfo const* info = mover.GetTransportInfo();
                    return info ? LocalPositionOf(*info) : Vector3();
                }

                Vector3 FromWorld(Unit const& mover, Vector3 const& world) const override
                {
                    Transport* vessel = VesselOf(mover);
                    if (!vessel)
                    {
                        return world;
                    }

                    float lx, ly, lz, lo;
                    vessel->CalculateLocalPositionOf(world.x, world.y, world.z, 0.0f,
                                                     lx, ly, lz, lo);
                    return Vector3(lx, ly, lz);
                }

                Vector3 ToWorld(Unit const& mover, Vector3 const& local) const override
                {
                    Transport* vessel = VesselOf(mover);
                    if (!vessel)
                    {
                        return local;
                    }

                    float gx, gy, gz, go;
                    vessel->CalculateGlobalPositionOf(local.x, local.y, local.z, 0.0f,
                                                      gx, gy, gz, go);
                    return Vector3(gx, gy, gz);
                }

                Vector3 ObjectPosition(Unit const& mover, WorldObject const& obj) const override
                {
                    Transport* vessel = VesselOf(mover);

                    // A fellow passenger's deck offset is exact and needs no conversion. It
                    // is also the only reading that stays right while the vessel moves under
                    // both of them -- which is precisely why chase and follow work on a
                    // rolling deck with no changes of their own, and why a pet keeps up with
                    // its master instead of swimming after the boat.
                    if (vessel)
                    {
                        if (auto local = LocalPositionAboard(*vessel, obj))
                        {
                            return *local;
                        }
                    }

                    // Someone ashore, or on another vessel. Read where they are in the
                    // world and bring that into our frame. The leg to them will be cut off
                    // at the deck's edge by the router -- which is the right answer: a mob
                    // may not walk off a ship to reach you.
                    Vector3 p;
                    obj.GetPosition(p.x, p.y, p.z);
                    return FromWorld(mover, p);
                }

                float ObjectOrientation(Unit const& mover, WorldObject const& obj) const override
                {
                    Transport* vessel = VesselOf(mover);
                    if (!vessel)
                    {
                        return obj.GetOrientation();
                    }

                    if (auto local = LocalOrientationAboard(*vessel, obj))
                    {
                        return *local;
                    }

                    return MapManager::NormalizeOrientation(obj.GetOrientation() -
                                                            vessel->GetOrientation());
                }

                Vector3 NearPoint(Unit const& mover, WorldObject const& target,
                                  float /*searcherBounding*/, float distance2d,
                                  float absAngle) const override
                {
                    // absAngle is a FRAME angle -- the generators derive it from
                    // ObjectOrientation or from frame positions -- so the offset is applied
                    // in the deck's own 2D system and no yaw correction belongs here.
                    const Vector3 t = ObjectPosition(mover, target);
                    const Vector3 guess(t.x + distance2d * std::cos(absAngle),
                                        t.y + distance2d * std::sin(absAngle),
                                        t.z);

                    Transport* vessel = VesselOf(mover);
                    if (!vessel)
                    {
                        return guess;
                    }

                    if (auto onDeck = DeckDrop(*vessel, guess))
                    {
                        return *onDeck;
                    }

                    // Off the edge. Handed back unresolved on purpose: the router will
                    // refuse the leg and the generator hears `blocked` and picks somewhere
                    // else. Quietly pulling it back onto the deck here would leave a chase
                    // standing still, convinced it had arrived.
                    return guess;
                }

                std::optional<Vector3> RandomPoint(Unit& mover, Vector3 const& centre,
                                                   float radius) const override
                {
                    Transport* vessel = VesselOf(mover);
                    if (!vessel || !vessel->HasDeck())
                    {
                        return std::nullopt;
                    }

                    for (int attempt = 0; attempt < DECK_RANDOM_TRIES; ++attempt)
                    {
                        const float angle = frand(0.0f, 2 * M_PI_F);
                        const float dist = radius * std::sqrt(frand(0.0f, 1.0f));

                        const Vector3 guess(centre.x + dist * std::cos(angle),
                                            centre.y + dist * std::sin(angle),
                                            centre.z);

                        const auto onDeck = DeckDrop(*vessel, guess);
                        if (onDeck && !DeckBlocked(*vessel, centre, *onDeck))
                        {
                            return onDeck;
                        }
                    }

                    return std::nullopt;
                }

                std::optional<Vector3> GroundPoint(Unit& mover, Vector3 const& from,
                                                   Vector3 const& guess) const override
                {
                    Transport* vessel = VesselOf(mover);
                    if (!vessel || !vessel->HasDeck())
                    {
                        return std::nullopt;
                    }

                    const auto onDeck = DeckDrop(*vessel, guess);
                    if (!onDeck)
                    {
                        return std::nullopt;   // no deck there: a panicking unit may not
                                               // bolt over the rail
                    }

                    // Unlike the world frame, the obstruction test applies to EVERYONE and
                    // rejects rather than pulls back. A deck is a warren of bulkheads and
                    // the units on it are inches from them; shoving a feared creature
                    // through one is no better than shoving a player through one.
                    if (DeckBlocked(*vessel, from, *onDeck))
                    {
                        return std::nullopt;
                    }

                    return onDeck;
                }
        };

        /// The two shared frames. Both are stateless, so one instance of each serves every
        /// mover on every map and on every vessel.
        const WorldFrame s_worldFrame;
        const TransportFrame s_transportFrame;
    }

    IMotionFrame const& FrameFor(Unit const& mover)
    {
        // The one place in the server where "which world am I in?" is decided. A boarded
        // unit moves in its vessel's frame; everything else in the map's. Nothing above
        // this call knows the difference, and nothing above it needs to.
        if (mover.GetTransportInfo())
        {
            return s_transportFrame;
        }

        return s_worldFrame;
    }
}
