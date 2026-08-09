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

#ifndef MANGOS_H_SIMULATIONTIME
#define MANGOS_H_SIMULATIONTIME

#include "Define.h"

/**
 * @brief The clock the simulation runs on -- not a wall clock, and not a copy of one.
 *
 * Advanced by exactly the number handed to the maps, in the same statement, so a map's
 * tick and an object's elapsed time cannot disagree. Measuring the second against
 * GameTime's cached milliseconds is what let them: those are resampled once per
 * housekeeping beat, so on a finer simulation beat an object saw four ticks of nothing
 * and then the whole 50ms. For a real duration -- a round trip, a profiling sample --
 * use getMSTime() instead.
 */
namespace Simulation
{
    /// MapManager::Update is the only caller; a second one breaks the guarantee above.
    void Advance(uint32 ms);

    /// Simulated milliseconds since startup. Monotonic, wide enough never to wrap.
    uint64 Now();
}

#endif
