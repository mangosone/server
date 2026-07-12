#pragma once

// Backward-compatibility umbrella for the terrain collision engine.
//
// The geometry primitives now live in ONE server-wide module, namespace Geometry
// (src/game/terrain/Geometry/*): Vector2/3/4, Matrix4 and Quat for movement/rotation,
// plus Aabb, Mat3, Transform, Tri and the ray/triangle test for collision/placement.
// A position therefore never needs converting as it crosses between the movement code,
// the collision engine and the offline baker — it is the same type everywhere.
//
// The engine historically spelled these world::terrain::Vec3 / Aabb / Mat3 / Transform /
// Tri (and the free dot/cross/rayTri). This header re-exports the Geometry:: types under
// those names so the existing engine sources compile unchanged. New code should prefer
// the Geometry:: names directly. See [[project_g3d_removal]] for the unification.
//
// The engine still has no vendor math dependency; the only vendored piece is the MPQ
// *container* reader (StormLib, behind IMpqArchive, offline baker only — the runtime
// never links it).

#include "terrain/Geometry/Vector3.h"
#include "terrain/Geometry/Shapes.h"

namespace world::terrain
{
    using Vec3 = ::Geometry::Vector3;
    using Aabb = ::Geometry::Aabb;
    using Mat3 = ::Geometry::Mat3;
    using Transform = ::Geometry::Transform;
    using Tri = ::Geometry::Tri;

    using ::Geometry::cross;
    using ::Geometry::dot;
    using ::Geometry::rayTri;
}
