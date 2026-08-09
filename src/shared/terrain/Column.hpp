#pragma once

// Every surface found under one point, in one answer. The engine reports what is there;
// which surface a QUESTION means -- the floor beneath a unit, the water it swims in, the
// ground below a fall -- is a SELECTION over this. One gather, many selections, so no
// caller can be handed a differently pre-selected answer by a differently shaped entry.

#include "terrain/Terrain.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace world::terrain
{
    enum class SurfaceKind : uint8_t
    {
        Terrain,   ///< the ADT heightmap
        Static,    ///< a baked model: a building, a bridge
        Live,      ///< a body posed at runtime: a door, a lift
        Liquid,
    };

    struct Surface
    {
        float z = 0.f;
        SurfaceKind kind = SurfaceKind::Terrain;
        LiquidKind liquid = LiquidKind::None;
        uint16_t liquidEntry = 0;
        bool deep = false;
        bool fromAdt = false;  ///< tile (ADT) liquid, not carried by a model

        bool Solid() const { return kind != SurfaceKind::Liquid; }

        LiquidInfo AsLiquid() const
        {
            LiquidInfo info;
            info.level = z;
            info.kind = liquid;
            info.entry = liquidEntry;
            info.deep = deep;
            return info;
        }
    };

    class Column
    {
        public:
            void AddSolid(float z, SurfaceKind kind)
            {
                Surface s;
                s.z = z;
                s.kind = kind;
                m_surfaces.push_back(s);
            }

            void AddLiquid(const LiquidInfo& info, bool fromAdt = false)
            {
                Surface s;
                s.z = info.level;
                s.kind = SurfaceKind::Liquid;
                s.liquid = info.kind;
                s.liquidEntry = info.entry;
                s.deep = info.deep;
                s.fromAdt = fromAdt;
                m_surfaces.push_back(s);
            }

            void Clear() { m_surfaces.clear(); }

            /// Drops surfaces the caller must not be answered with. WHICH those are is a
            /// server rule, so the engine offers the cut and never makes the choice.
            template <typename Pred>
            void DropIf(Pred pred)
            {
                m_surfaces.erase(
                    std::remove_if(m_surfaces.begin(), m_surfaces.end(), pred),
                    m_surfaces.end());
            }
            bool Empty() const { return m_surfaces.empty(); }
            const std::vector<Surface>& Surfaces() const { return m_surfaces; }

            std::optional<float> HighestSolidAtOrBelow(float z) const
            {
                float best = -std::numeric_limits<float>::max();
                bool found = false;
                for (const Surface& s : m_surfaces)
                {
                    if (s.Solid() && s.z <= z && s.z > best)
                    {
                        best = s.z;
                        found = true;
                    }
                }
                return found ? std::optional<float>(best) : std::nullopt;
            }

            std::optional<float> LowestSolidAbove(float z) const
            {
                float best = std::numeric_limits<float>::max();
                bool found = false;
                for (const Surface& s : m_surfaces)
                {
                    if (s.Solid() && s.z > z && s.z < best)
                    {
                        best = s.z;
                        found = true;
                    }
                }
                return found ? std::optional<float>(best) : std::nullopt;
            }

            std::optional<float> HighestSolid() const
            {
                return HighestSolidAtOrBelow(std::numeric_limits<float>::max());
            }

            /// The floor a point stands on. A query point often sits a little UNDER the
            /// surface -- a spawn buried a yard into a hillside -- so when nothing is
            /// below, the nearest surface ABOVE is the floor it would stand on once
            /// freed. Answering with the highest surface anywhere instead hands back the
            /// roof of whatever the hill is under.
            std::optional<float> Floor(float z, float tolerance = 2.0f) const
            {
                if (auto below = HighestSolidAtOrBelow(z + tolerance))
                {
                    return below;
                }
                return LowestSolidAbove(z + tolerance);
            }

            /// The liquid a point at @p z is actually in: the highest surface it can reach
            /// without passing through a floor or a ceiling. The highest surface ANYWHERE
            /// in the column is a different question, and answering this one with it puts
            /// a player standing dry on the ground floor into the pool one storey up, and
            /// a player standing on a bridge into the river running under it.
            std::optional<Surface> LiquidAt(float z, bool includeAdt = true) const
            {
                std::optional<Surface> best;
                for (const Surface& s : m_surfaces)
                {
                    if (s.kind != SurfaceKind::Liquid || (!includeAdt && s.fromAdt))
                    {
                        continue;
                    }
                    if ((best && s.z <= best->z) || SolidBetween(z, s.z))
                    {
                        continue;
                    }
                    best = s;
                }
                return best;
            }

            /// Strictly between, so a surface resting exactly on a floor -- a pool on the
            /// slab it was built into -- is not walled off from the point standing on it.
            bool SolidBetween(float a, float b) const
            {
                const float lo = std::min(a, b), hi = std::max(a, b);
                for (const Surface& s : m_surfaces)
                {
                    if (s.Solid() && s.z > lo && s.z < hi)
                    {
                        return true;
                    }
                }
                return false;
            }

            std::optional<Surface> HighestLiquid(bool includeAdt = true) const
            {
                std::optional<Surface> best;
                for (const Surface& s : m_surfaces)
                {
                    if (s.kind == SurfaceKind::Liquid &&
                        (includeAdt || !s.fromAdt) && (!best || s.z > best->z))
                    {
                        best = s;
                    }
                }
                return best;
            }

            /// Whether any baked model surface lies in the sweep -- the cheap
            /// pre-test for "could this point be inside a WMO at all".
            bool HasStatic() const
            {
                for (const Surface& s : m_surfaces)
                {
                    if (s.kind == SurfaceKind::Static)
                    {
                        return true;
                    }
                }
                return false;
            }

        private:
            std::vector<Surface> m_surfaces;
    };
}
