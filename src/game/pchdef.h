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

#ifndef GAME_PCH_H
#define GAME_PCH_H

// This list is deliberately FAT, and it was measured before it was written.
//
// The tempting "improvement" here is to strip it back to the stable, never-edited headers
// (Common.h and the standard library) on the theory that a precompiled header carrying
// Player.h, Map.h and ObjectMgr.h is rebuilt -- along with all ~350 of this target's
// translation units -- every time one of those is touched. That theory was tried. A clean
// build of `game` went from 70s to 130s: precompiling the big game headers IS the win, and
// giving it up costs nearly 2x.
//
// The incremental cost it was meant to buy back barely exists either: Map.h is a hub header
// that most of the tree reaches anyway, so touching it recompiles essentially everything with
// or without it in here.
//
// If a precompiled build is ever SLOWER than PCH=0, this file is not the culprit -- look at
// cmake/PCHSupport.cmake instead. Clang re-instantiates every template in a precompiled header
// in every TU that loads it unless -fpch-instantiate-templates is passed, which for a header
// set this size hands the entire saving straight back and then some.
//
// Nothing here is load-bearing for correctness: CI builds with PCH=0, so every translation
// unit must already include what it uses. Do not "fix" a missing include by adding a header
// here.

#include "Common.h"
#include "Map.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "WorldPacket.h"
#include "ObjectGuid.h"
#include "WorldSession.h"
#include "Creature.h"
#include "Player.h"

#include <map>
#include <vector>

#endif
