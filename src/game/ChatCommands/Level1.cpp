/**
 * MaNGOS is a full featured server for World of Warcraft, supporting
 * the following clients: 1.12.x, 2.4.3, 3.3.5a, 4.3.4a and 5.4.8
 *
 * Copyright (C) 2005-2020 MaNGOS <https://getmangos.eu>
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

#include "Common.h"
#include "Database/DatabaseEnv.h"
#include "DBCStores.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include "World.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Opcodes.h"
#include "Chat.h"
#include "Log.h"
#include "MapManager.h"
#include "ObjectAccessor.h"
#include "Language.h"
#include "CellImpl.h"
#include "MapPersistentStateMgr.h"
#include "Mail.h"
#include "Util.h"
#include "SpellMgr.h"
#ifdef _DEBUG_VMAPS
#include "VMapFactory.h"
#endif

//-----------------------Npc Commands-----------------------

// make npc whisper to player

//----------------------------------------------------------

// Enable\Dissable GM Mode



// Summon Player

// Teleport player to last position

// Edit Player HP

// Edit Player Fly
bool ChatHandler::HandleModifyFlyCommand(char* args)
{
    if (!*args)
    {
        return false;
    }

    float modSpeed = (float)atof(args);

    if (modSpeed > 10.0f || modSpeed < 0.1f)
    {
        SendSysMessage(LANG_BAD_VALUE);
        SetSentErrorMessage(true);
        return false;
    }

    Player* chr = getSelectedPlayer();
    if (chr == NULL)
    {
        SendSysMessage(LANG_NO_CHAR_SELECTED);
        SetSentErrorMessage(true);
        return false;
    }

    // check online security
    if (HasLowerSecurity(chr))
    {
        return false;
    }

    PSendSysMessage(LANG_YOU_CHANGE_FLY_SPEED, modSpeed, GetNameLink(chr).c_str());
    if (needReportToTarget(chr))
    {
        ChatHandler(chr).PSendSysMessage(LANG_YOURS_FLY_SPEED_CHANGED, GetNameLink().c_str(), modSpeed);
    }

    chr->UpdateSpeed(MOVE_FLIGHT, true, modSpeed);

    return true;
}

