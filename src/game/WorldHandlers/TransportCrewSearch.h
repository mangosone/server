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

#ifndef MANGOS_H_TRANSPORTCREWSEARCH
#define MANGOS_H_TRANSPORTCREWSEARCH

// Letting the grid's searchers see a vessel's crew.
//
// A crew member is in no grid cell -- see Transports.h for why -- so nothing that walks
// cells can find it. That is fine for the things the vessel owns (its tick, its
// broadcast), but it is NOT fine for being FOUND: a fireball dropped on the deck would
// pass straight through a crew that the grid cannot see, and two deckhands would not
// notice each other.
//
// The answer is not to put the crew back in a cell -- their world position is only an
// estimate of a pose the server does not know, and the ship would churn cells all voyage.
// It is to notice that THE SHIP IS THEIR CELL. Their GridReference is linked into a
// container the vessel owns, rather than one a map cell owns, and a search near the vessel
// simply visits it too.
//
// This works with EVERY searcher, unmodified, because the searchers in GridNotifiers.h
// take any GridRefManager<T> and compile themselves away for types they do not want.

#include "GridDefines.h"

class Map;

namespace MaNGOS
{
    /// A search cannot overlap more vessels than this. There are a handful per map.
    enum { MAX_CREWS_PER_QUERY = 8 };

    /**
     * @brief The crew containers of every vessel whose HULL overlaps the search circle.
     *
     * Hull, not centre: a ship is a hundred yards long, so a fireball at the bow overlaps a
     * vessel whose origin is well outside the blast. Returns how many were written.
     * Costs nothing on the overwhelming majority of maps, which have no transports at all.
     */
    uint32 GatherCrewContainersNear(Map* map, float x, float y, float radius,
                                    CreatureMapType** out, uint32 maxOut);

    /// Feed a grid searcher the crew of any vessel near the search circle.
    template<class VISITOR>
    void VisitTransportCrew(Map* map, float x, float y, float radius, VISITOR& visitor)
    {
        CreatureMapType* crews[MAX_CREWS_PER_QUERY];

        const uint32 found = GatherCrewContainersNear(map, x, y, radius, crews, MAX_CREWS_PER_QUERY);

        for (uint32 i = 0; i < found; ++i)
        {
            visitor.Visit(*crews[i]);
        }
    }
}

#endif // MANGOS_H_TRANSPORTCREWSEARCH
