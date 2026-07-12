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

#include "Chat.h"
#include "Creature.h"
#include "Language.h"
#include "MotionGenerators/MotionMaster.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Transports.h"
#include "TransportSystem.h"
#include "World.h"

/**
 * @brief .trans npc add <entry> [<movement_type>] [<wander_distance>]
 *
 * Spawn a crew member on the vessel the GM is standing on, at the GM's own DECK OFFSET,
 * and persist it to `creature_transport`.
 *
 * The offset is not something we measure or guess: the GM's own client is sending it, in
 * every movement packet, as MovementInfo::t_pos. That is ground truth by construction --
 * the client is authoritative for where it is standing, and it composes the offset with the
 * vessel pose it is drawing. Reading it back is the only way to author these numbers that
 * cannot be wrong.
 *
 * Z is then RE-RESOLVED against the deck mesh rather than taken from the client, so the
 * crew member is planted exactly on the floor even if the GM was mid-jump or hovering.
 */
bool ChatHandler::HandleTransNpcAddCommand(char* args)
{
    if (!*args)
    {
        return false;
    }

    Player* gm = m_session->GetPlayer();

    Transport* vessel = gm->GetTransport();
    if (!vessel)
    {
        SendSysMessage("You must be standing ON a transport to add crew to it.");
        SetSentErrorMessage(true);
        return false;
    }

    if (!vessel->HasDeck())
    {
        PSendSysMessage("Transport %u (%s) has no baked collision model, so it has no deck. "
                        "Crew cannot stand on it.", vessel->GetEntry(), vessel->GetName());
        SetSentErrorMessage(true);
        return false;
    }

    uint32 entry;
    if (!ExtractUint32KeyFromLink(&args, "Hcreature_entry", entry))
    {
        return false;
    }

    CreatureInfo const* cinfo = ObjectMgr::GetCreatureTemplate(entry);
    if (!cinfo)
    {
        PSendSysMessage(LANG_COMMAND_INVALIDCREATUREID, entry);
        SetSentErrorMessage(true);
        return false;
    }

    uint32 movementType = IDLE_MOTION_TYPE;
    if (!ExtractOptUInt32(&args, movementType, IDLE_MOTION_TYPE))
    {
        return false;
    }

    if (movementType >= MAX_DB_MOTION_TYPE)
    {
        PSendSysMessage("Bad movement type %u. Use 0 (idle), 1 (random) or 2 (waypoint).",
                        movementType);
        SetSentErrorMessage(true);
        return false;
    }

    uint32 wanderDistance = 0;
    if (!ExtractOptUInt32(&args, wanderDistance, 0))
    {
        return false;
    }

    if (movementType == RANDOM_MOTION_TYPE && wanderDistance == 0)
    {
        wanderDistance = 5;         // a wanderer with no radius would simply stand still
    }

    // THE DECK OFFSET, STRAIGHT FROM THE CLIENT. Not derived, not estimated -- read.
    const float lx = gm->GetTransOffsetX();
    const float ly = gm->GetTransOffsetY();
    const float lo = gm->GetTransOffsetO();

    // ...except Z, which we re-resolve against the deck mesh so the crew member is planted
    // on the floor rather than wherever the GM's feet happened to be.
    const auto deckZ = vessel->DeckHeightAt(lx, ly, gm->GetTransOffsetZ(), 3.0f, 6.0f);
    if (!deckZ)
    {
        SendSysMessage("No deck found beneath you. Stand on a walkable part of the vessel.");
        SetSentErrorMessage(true);
        return false;
    }

    const float lz = *deckZ;

    // Persist first: if the DB write fails there is no point spawning something that will
    // vanish on the next restart.
    WorldDatabase.BeginTransaction();
    WorldDatabase.PExecute(
        "INSERT INTO `creature_transport` "
        "(`transport_entry`, `npc_entry`, `TransOffsetX`, `TransOffsetY`, `TransOffsetZ`, "
        "`TransOffsetO`, `wander_distance`, `MovementType`, `emote`) "
        "VALUES ('%u', '%u', '%f', '%f', '%f', '%f', '%f', '%u', '0')",
        vessel->GetEntry(), entry, lx, ly, lz, lo, float(wanderDistance), movementType);
    WorldDatabase.CommitTransaction();

    // And put one aboard right now, so it can be looked at without a restart.
    //
    // From the STATIC guid space, exactly as Transport::LoadCrew does and for the same reason:
    // a map-local guid is only unique on the map that issued it, and this creature is about to
    // sail to a different one. See the comment there.
    const uint32 crewGuid = sObjectMgr.GenerateStaticCreatureLowGuid();

    Creature* crew = crewGuid ? new Creature : NULL;

    CreatureCreatePos cPos(vessel->GetMap(), vessel->GetPositionX(), vessel->GetPositionY(),
                           vessel->GetPositionZ(), vessel->GetOrientation());

    if (!crew || !crew->Create(crewGuid, cPos, cinfo))
    {
        delete crew;
        SendSysMessage("Crew member was written to `creature_transport`, but could not be "
                       "spawned right now. It will appear on the next restart.");
        SetSentErrorMessage(true);
        return false;
    }

    // Its home is the deck offset. Never world data -- which is exactly what lets Home
    // (evade) and Random (wander) anchor themselves in the vessel's frame for free.
    crew->SetRespawnCoord(lx, ly, lz, lo);
    crew->SetRespawnRadius(float(wanderDistance));
    crew->SetDefaultMovementType(MovementGeneratorType(movementType));

    vessel->BoardCreature(crew, lx, ly, lz, lo);
    crew->GetMotionMaster()->Initialize();

    PSendSysMessage("Crew %u (%s) added to transport %u (%s) at deck offset "
                    "(%.3f, %.3f, %.3f) o %.3f, movement %u, wander %u.",
                    entry, cinfo->Name, vessel->GetEntry(), vessel->GetName(),
                    lx, ly, lz, lo, movementType, wanderDistance);

    return true;
}
