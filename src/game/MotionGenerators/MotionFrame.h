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

#ifndef MANGOS_MOTIONFRAME_H
#define MANGOS_MOTIONFRAME_H

#include "Common.h"
#include "movement/MoveSplineInitArgs.h"

#include <cmath>
#include <memory>
#include <optional>

class Unit;
class WorldObject;

/**
 * @brief The COORDINATE FRAME a movement leg lives in — the seam that makes the
 *        spline and the path calculus terrain-agnostic.
 *
 * Every terrain-dependent question the movement stack asks — route me, where is my
 * target, give me a reachable point nearby, drop this guess onto the ground — is
 * answered by the frame, not by a direct call into Map or PathFinder. That is the
 * whole point: nothing above this interface knows, or may know, whether the unit is
 * walking on world terrain or on the deck of a moving ship.
 *
 *   * WorldFrame     — world coordinates. Routing through the Detour navmesh, heights
 *                      from the terrain. Behaves exactly as the movement code did
 *                      before the seam existed.
 *
 *   * TransportFrame — a vessel's LOCAL system. A boarded unit's position, its target's
 *                      position and the leg it walks are all LOCAL coordinates the
 *                      client composes with the live vessel pose; the floor comes from
 *                      the deck mesh rather than the world, so a passenger can never
 *                      sink to the sea floor. The wire is already frame-aware:
 *                      MoveSplineInit::Launch feeds the spline the local position and
 *                      switches to SMSG_MONSTER_MOVE_TRANSPORT.
 *
 * The invariant that makes this safe: A LEG NEVER SPANS TWO FRAMES. Boarding or
 * leaving a transport ends the current leg; the next is laid in the new frame. The
 * driver enforces it by dropping its router whenever the frame under it changes.
 *
 * The other half of that invariant is FromWorld/ToWorld. Every anchor a generator
 * captures — a respawn coord, a DB waypoint, a script's MovePoint — is WORLD data,
 * because world data is the only kind the database and the script API speak. Read as
 * a deck offset it would send the unit somewhere absurd, so it must come through
 * FromWorld. Under WorldFrame both calls are the identity, which is why the
 * generators can do this unconditionally.
 */
namespace Motion
{
    using Movement::PointsArray;
    using Movement::Vector3;

    /// Which coordinate system a frame speaks.
    enum class FrameKind : uint8
    {
        World,    ///< World coordinates (terrain + navmesh).
        Transport ///< A transport's model-local coordinates (deck mesh).
    };

    /**
     * @brief The 2D bearing from one FRAME point to another, normalised to [0, 2*PI)
     *        exactly as WorldObject::GetAngle normalises.
     *
     * Generators need this because GetAngle itself reads world positions — and a boarded
     * unit's world position is only a cache, derived from a vessel pose the server is
     * merely estimating. A chase bearing computed from it would aim at where the deck is
     * not. Distances survive the change of frame (a rigid transform preserves them, which
     * is why GetDistance is still fine); ANGLES do not, because the deck is rotated.
     */
    inline float AngleBetween(Vector3 const& from, Vector3 const& to)
    {
        const float a = std::atan2(to.y - from.y, to.x - from.x);
        return (a >= 0.0f) ? a : (2 * M_PI_F + a);
    }

    /**
     * @brief A reusable router for one mover, in one frame.
     *
     * Kept alive across legs (as the PathFinder each targeted generator used to own),
     * so a chase re-routing several times a second does not rebuild its query state.
     */
    class IPathQuery
    {
        public:
            virtual ~IPathQuery() = default;

            /**
             * @brief Route a leg from `start` to `goal` within this frame.
             * @param forceDestination Arrive exactly at `goal` even when it is not
             *        cleanly routable (a pet heeling its master may cheat).
             * @param lengthLimit Cap the path length in yards (0 = default).
             * @return True when usable geometry came out — which INCLUDES the
             *         straight-line fallback used when routing failed (see Failed).
             *         False only when nothing at all could be produced.
             */
            virtual bool Calculate(Vector3 const& start, Vector3 const& goal,
                                   bool forceDestination, float lengthLimit) = 0;

            /// The routed polyline from the last successful Calculate.
            virtual PointsArray const& Points() const = 0;

            /**
             * @brief Routing failed and Points() is only a straight line through
             *        whatever is in the way.
             *
             * Whether that is acceptable is the CALLER's decision, and the movement
             * kinds genuinely disagree — hence MOVE_REQUIRE_PATH on the intent rather
             * than the router quietly picking one behaviour for everybody.
             */
            virtual bool Failed() const = 0;

            /**
             * @brief The last Calculate produced a REAL route, not a straight line —
             *        a different question from Failed, because a map with no routing
             *        data answers every query with a straight line that did not "fail".
             *
             * Waypoint smoothing needs this: it may only weld several patrol legs into
             * one spline when it knows each leg actually goes around what is between
             * the nodes.
             */
            virtual bool Routed() const = 0;

            /// False when the last route only got partway to the goal.
            virtual bool Reachable() const = 0;
    };

    /**
     * @brief One coordinate frame's answers to every terrain question the movement
     *        stack asks. Stateless: one shared instance per kind.
     */
    class IMotionFrame
    {
        public:
            virtual ~IMotionFrame() = default;

            virtual FrameKind Kind() const = 0;

            /// A router for `mover` in this frame.
            virtual std::unique_ptr<IPathQuery> CreatePathQuery(Unit const& mover) const = 0;

            /// The mover's own position, in frame coordinates.
            virtual Vector3 MoverPosition(Unit const& mover) const = 0;

            /**
             * @brief Bring a WORLD-space point into this frame.
             *
             * Spawn coordinates, DB waypoints and every script that says "walk to
             * (x,y,z)" speak world space and always will — so an anchor a generator
             * captures from them has to be converted before it can be used as a goal.
             * The identity under WorldFrame.
             */
            virtual Vector3 FromWorld(Unit const& mover, Vector3 const& world) const = 0;

            /// The inverse: a frame point back out to world space, for anything that has
            /// to talk to the map (grid placement, a spell's destination, a summon).
            virtual Vector3 ToWorld(Unit const& mover, Vector3 const& local) const = 0;

            /// Another object's position, in the MOVER's frame — this is what lets
            /// chase and follow track a target without caring which frame either of
            /// them stands in, and why there is no need for deck-local twins of them.
            virtual Vector3 ObjectPosition(Unit const& mover, WorldObject const& obj) const = 0;

            /// Another object's facing, in the mover's frame.
            virtual float ObjectOrientation(Unit const& mover, WorldObject const& obj) const = 0;

            /**
             * @brief The point `distance2d` yards from `target` at `absAngle`, dropped
             *        onto whatever this frame considers ground. The destination of
             *        every chase and follow leg.
             */
            virtual Vector3 NearPoint(Unit const& mover, WorldObject const& target,
                                      float searcherBounding, float distance2d,
                                      float absAngle) const = 0;

            /**
             * @brief A reachable random point within `radius` of `centre`, on this
             *        frame's ground. Drives wander and the confused stagger.
             * @return Nothing when no reachable point was found (retry later).
             */
            virtual std::optional<Vector3> RandomPoint(Unit& mover, Vector3 const& centre,
                                                       float radius) const = 0;

            /**
             * @brief Drop `guess` onto this frame's ground, pulled back to the first
             *        obstruction on the way from `from`. Drives the flee-point search.
             * @return Nothing when there is no ground there (reject the point).
             */
            virtual std::optional<Vector3> GroundPoint(Unit& mover, Vector3 const& from,
                                                       Vector3 const& guess) const = 0;
    };

    /**
     * @brief The frame `mover` is currently moving in.
     *
     * Always the world frame today. When TransportFrame lands, a boarded unit resolves
     * to its vessel's frame HERE and nothing above this call has to change — that is
     * the payoff of routing every terrain question through IMotionFrame.
     */
    IMotionFrame const& FrameFor(Unit const& mover);
}

#endif // MANGOS_MOTIONFRAME_H
