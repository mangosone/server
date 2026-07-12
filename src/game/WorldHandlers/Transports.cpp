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

#include "Common.h"

#include "Transports.h"
#include "CellImpl.h"
#include "Creature.h"
#include "GameObjectModel.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "MapManager.h"
#include "MotionGenerators/MotionMaster.h"
#include "ObjectMgr.h"
#include "ObjectGuid.h"
#include "Path.h"
#include "Player.h"
#include "GameTime.h"
#include "TransportCrewSearch.h"
#include "World.h"

#include "WorldPacket.h"
#include "DBCStores.h"
#include "ProgressBar.h"
#include "ScriptMgr.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
/**
 * @brief Loads and initializes all configured global transports.
 */
void MapManager::LoadTransports()
{
    QueryResult* result = WorldDatabase.Query("SELECT `entry`, `name`, `period` FROM `transports`");

    uint32 count = 0;

    if (!result)
    {
        BarGoLink bar(1);
        bar.step();
        sLog.outString(">> Loaded %u transports", count);
        sLog.outString();
        return;
    }

    BarGoLink bar(result->GetRowCount());

    do
    {
        bar.step();

        Transport* t = new Transport;

        Field* fields = result->Fetch();

        uint32 entry = fields[0].GetUInt32();
        std::string name = fields[1].GetCppString();
        t->m_period = fields[2].GetUInt32();

        const GameObjectInfo* goinfo = ObjectMgr::GetGameObjectInfo(entry);

        if (!goinfo)
        {
            sLog.outErrorDb("Transport ID:%u, Name: %s, will not be loaded, gameobject_template missing", entry, name.c_str());
            delete t;
            continue;
        }

        if (goinfo->type != GAMEOBJECT_TYPE_MO_TRANSPORT)
        {
            sLog.outErrorDb("Transport ID:%u, Name: %s, will not be loaded, gameobject_template type wrong", entry, name.c_str());
            delete t;
            continue;
        }

        // sLog.outString("Loading transport %d between %s, %s", entry, name.c_str(), goinfo->name);

        std::set<uint32> mapsUsed;

        if (!t->GenerateWaypoints(goinfo->moTransport.taxiPathId, mapsUsed))
            // skip transports with empty waypoints list
        {
            sLog.outErrorDb("Transport (path id %u) path size = 0. Transport ignored, check DBC files or transport GO data0 field.", goinfo->moTransport.taxiPathId);
            delete t;
            continue;
        }

        float x, y, z, o;
        uint32 mapid;
        x = t->m_WayPoints[0].x; y = t->m_WayPoints[0].y; z = t->m_WayPoints[0].z; mapid = t->m_WayPoints[0].mapid; o = 1;

        // current code does not support transports in dungeon!
        const MapEntry* pMapInfo = sMapStore.LookupEntry(mapid);
        if (!pMapInfo || pMapInfo->Instanceable())
        {
            delete t;
            continue;
        }

        // creates the Gameobject
        if (!t->Create(entry, mapid, x, y, z, o, GO_ANIMPROGRESS_DEFAULT))
        {
            delete t;
            continue;
        }

        m_Transports.insert(t);

        for (std::set<uint32>::const_iterator i = mapsUsed.begin(); i != mapsUsed.end(); ++i)
        {
            m_TransportsByMap[*i].insert(t);
        }

        // The transport is deliberately NOT put in a grid cell. It is not a body in the
        // world; it is a world. It gets a Map so it has somewhere to live and somewhere to
        // find its observers, and nothing more.
        t->SetMap(sMapMgr.CreateMap(mapid, t));

        // Its inhabitants. They join the vessel, not the grid.
        t->LoadCrew();

        ++count;
    }
    while (result->NextRow());
    delete result;

    // check transport data DB integrity
    result = WorldDatabase.Query("SELECT `gameobject`.`guid`,`gameobject`.`id`,`transports`.`name` FROM `gameobject`,`transports` WHERE `gameobject`.`id` = `transports`.`entry`");
    if (result)                                             // wrong data found
    {
        do
        {
            Field* fields = result->Fetch();

            uint32 guid  = fields[0].GetUInt32();
            uint32 entry = fields[1].GetUInt32();
            std::string name = fields[2].GetCppString();
            sLog.outErrorDb("Transport %u '%s' have record (GUID: %u) in `gameobject`. Transports DON'T must have any records in `gameobject` or its behavior will be unpredictable/bugged.", entry, name.c_str(), guid);
        }
        while (result->NextRow());

        delete result;
    }

    sLog.outString(">> Loaded %u transports", count);
    sLog.outString();
}

namespace
{
    /// How long a pose observed from a player aboard is trusted before the (much worse)
    /// waypoint token takes over again. Any player aboard refreshes it several times a
    /// second, so this only expires when the last one steps off -- at which point nobody
    /// is watching the vessel closely enough to care.
    constexpr uint32 POSE_TRUST_MS = 2000;

    /// How often the vessel re-derives its observer set. This is its VisibleNotifier, and
    /// it runs at the cadence the grid's own visibility pass runs at, for the same reason:
    /// a create/destroy at the client is expensive, and nobody closes 100 yards in half a
    /// second.
    constexpr uint32 VISIBILITY_UPDATE_MS = 500;

    /// A pet is boarded when it is standing over the deck. The search is generous upward
    /// (it may be mid-stride, or on a gangplank) and downward (a pet on an upper deck).
    constexpr float DECK_BOARD_SEARCH_UP = 4.0f;
    constexpr float DECK_BOARD_SEARCH_DOWN = 8.0f;

    /**
     * @brief A player within range of a vessel.
     *
     * This CANNOT be MaNGOS::AnyPlayerInObjectRangeCheck, and the reason is a trap worth
     * remembering: that check calls IsWithinDistInMap, which insists on IsInMap -- and
     * IsInMap requires IsInWorld() on BOTH sides.
     *
     * A transport is DELIBERATELY never AddToWorld'd. It is in no grid cell (see the class
     * comment in Transports.h); it only ever gets a SetMap. So IsInWorld() is false for it,
     * IsInMap() is false against every player alive, and the stock check rejects EVERY
     * observer of EVERY vessel, forever -- which makes every ship in the game invisible.
     *
     * So the test is done by hand, on the two things that actually matter: same map, and
     * within range in 2D. Two dimensions on purpose -- a player on a clifftop above the
     * harbour is still looking at the boat.
     *
     * No IsAlive() filter either (the stock check has one): a ghost running back to their
     * corpse still needs to see the ship they drowned next to.
     */
    class AnyPlayerNearVessel
    {
        public:
            AnyPlayerNearVessel(Transport const* vessel, float range)
                : m_vessel(vessel), m_range(range) {}

            WorldObject const& GetFocusObject() const { return *m_vessel; }

            bool operator()(Player* player) const
            {
                if (!player->IsInWorld() || player->GetMapId() != m_vessel->GetMapId())
                {
                    return false;
                }

                const float dx = player->GetPositionX() - m_vessel->GetPositionX();
                const float dy = player->GetPositionY() - m_vessel->GetPositionY();

                return (dx * dx + dy * dy) <= (m_range * m_range);
            }

        private:
            Transport const* m_vessel;
            float m_range;
    };

    /// A crew member's spawn record. Deck offsets, not map coordinates.
    struct CrewSpawn
    {
        uint32 npcEntry = 0;
        float  lx = 0.0f, ly = 0.0f, lz = 0.0f, lo = 0.0f;
        float  wanderDistance = 0.0f;
        uint8  movementType = 0;
        uint32 emote = 0;
    };

    /// transport_entry -> its crew. Filled once at startup by LoadTransportCrew().
    std::unordered_map<uint32, std::vector<CrewSpawn> > s_crewByTransport;
}

/**
 * @brief Load `creature_transport`: the crew rosters, in deck-local coordinates.
 */
void MapManager::LoadTransportCrew()
{
    s_crewByTransport.clear();

    QueryResult* result = WorldDatabase.Query(
        "SELECT `transport_entry`, `npc_entry`, `TransOffsetX`, `TransOffsetY`, `TransOffsetZ`, "
        "`TransOffsetO`, `wander_distance`, `MovementType`, `emote` FROM `creature_transport`");

    if (!result)
    {
        BarGoLink bar(1);
        bar.step();
        sLog.outString(">> Loaded 0 transport crew members");
        sLog.outString();
        return;
    }

    BarGoLink bar(result->GetRowCount());
    uint32 count = 0;

    do
    {
        bar.step();
        Field* fields = result->Fetch();

        const uint32 transportEntry = fields[0].GetUInt32();

        CrewSpawn spawn;
        spawn.npcEntry       = fields[1].GetUInt32();
        spawn.lx             = fields[2].GetFloat();
        spawn.ly             = fields[3].GetFloat();
        spawn.lz             = fields[4].GetFloat();
        spawn.lo             = fields[5].GetFloat();
        spawn.wanderDistance = fields[6].GetFloat();
        spawn.movementType   = fields[7].GetUInt8();
        spawn.emote          = fields[8].GetUInt32();

        if (!ObjectMgr::GetCreatureTemplate(spawn.npcEntry))
        {
            sLog.outErrorDb("Table `creature_transport` has crew member with non-existing creature entry %u on transport %u, skipped.",
                            spawn.npcEntry, transportEntry);
            continue;
        }

        s_crewByTransport[transportEntry].push_back(spawn);
        ++count;
    }
    while (result->NextRow());

    delete result;

    sLog.outString(">> Loaded %u transport crew members", count);
    sLog.outString();
}

Transport::Transport()
    : GameObject(), TransportBase(this),
      m_pathTime(0), m_timer(0), m_hullRadius(0.0f), m_poseAge(POSE_TRUST_MS),
      m_visibilityTimer(0), m_nextNodeTime(0), m_period(0)
{
    // 2.3.2 - 0x5A
    m_updateFlag = (UPDATEFLAG_TRANSPORT | UPDATEFLAG_LOWGUID | UPDATEFLAG_HIGHGUID | UPDATEFLAG_HAS_POSITION);
}

Transport::~Transport()
{
    // The crew belong to us, not to a grid -- so nothing else will ever clean them up.
    // Unboard first: ~TransportBase asserts the passenger list is empty, and rightly so.
    for (Creature* crew : m_crew)
    {
        UnBoardPassenger(crew);
        crew->GetGridRef().unlink();
        crew->RemoveFromWorld();
        delete crew;
    }

    m_crew.clear();
}

bool Transport::Create(uint32 guidlow, uint32 mapid, float x, float y, float z, float ang, uint32 animprogress)
{
    Relocate(x, y, z, ang);

    if (!IsPositionValid())
    {
        sLog.outError("Transport (GUID: %u) not created. Suggested coordinates isn't valid (X: %f Y: %f)",
                      guidlow, x, y);
        return false;
    }

    Object::_Create(guidlow, 0, HIGHGUID_MO_TRANSPORT);

    GameObjectInfo const* goinfo = ObjectMgr::GetGameObjectInfo(guidlow);

    if (!goinfo)
    {
        sLog.outErrorDb("Transport not created: entry in `gameobject_template` not found, guidlow: %u map: %u  (X: %f Y: %f Z: %f) ang: %f", guidlow, mapid, x, y, z, ang);
        return false;
    }

    m_goInfo = goinfo;

    SetObjectScale(goinfo->size);

    SetUInt32Value(GAMEOBJECT_FACTION, goinfo->faction);
    SetUInt32Value(GAMEOBJECT_FLAGS, goinfo->flags);

    SetEntry(goinfo->id);

    //SetDisplayId(goinfo->displayId);
    // Use SetDisplayId only if we have the GO assigned to a proper map!
    SetUInt32Value(GAMEOBJECT_DISPLAYID, goinfo->displayId);
    m_displayInfo = sGameObjectDisplayInfoStore.LookupEntry(goinfo->displayId);

    SetGoState(GO_STATE_READY);
    SetGoType(GameobjectTypes(goinfo->type));

    SetGoAnimProgress(animprogress);

    SetName(goinfo->name);

    // The deck. Taken straight from the model store and held AS BAKED -- no placement,
    // no world transform, no DynamicCollision membership. This mesh is the vessel's
    // terrain, and the vessel's own coordinates are the only ones it is ever queried in.
    m_deck = GameObjectModel::AcquireModel(goinfo->displayId);

    if (m_deck && !m_deck->empty())
    {
        // The hull's real extent, for sizing the observer search later. A ship is not a
        // point: a player at the bow must still be told about the stern.
        const world::terrain::Aabb& b = m_deck->bounds();
        const float hx = std::max(std::fabs(b.lo.x), std::fabs(b.hi.x));
        const float hy = std::max(std::fabs(b.lo.y), std::fabs(b.hi.y));
        m_hullRadius = std::sqrt(hx * hx + hy * hy);
    }
    else
    {
        sLog.outErrorDb("Transport %u (%s, display %u) has no baked collision model. Its crew "
                        "would have no deck to stand on, so it will carry none.",
                        goinfo->id, goinfo->name, goinfo->displayId);
    }

    return true;
}

/* ******************************** The deck: local-only collision ******************** */

std::optional<float> Transport::DeckHeightAt(float lx, float ly, float lz,
                                             float searchUp, float searchDown) const
{
    if (!HasDeck())
    {
        return std::nullopt;
    }

    // Straight down, in the mesh's own space. This is the entire trick: no transform is
    // applied because none is needed and none would be trustworthy. The ray starts a
    // little above the point so a shallow step or a ramp is still found underfoot.
    const Geometry::Vector3 origin(lx, ly, lz + searchUp);
    const Geometry::Vector3 down(0.0f, 0.0f, -1.0f);

    const auto t = m_deck->raycastNearest(origin, down, searchUp + searchDown);
    if (!t || *t < 0.0f)
    {
        return std::nullopt;
    }

    return origin.z - *t;
}

bool Transport::IsDeckBlocked(Geometry::Vector3 const& from, Geometry::Vector3 const& to) const
{
    if (!HasDeck())
    {
        return false;
    }

    Geometry::Vector3 seg = to - from;
    const float len = std::sqrt(seg.x * seg.x + seg.y * seg.y + seg.z * seg.z);
    if (len < 1e-4f)
    {
        return false;
    }

    const Geometry::Vector3 dir(seg.x / len, seg.y / len, seg.z / len);

    const auto t = m_deck->raycastNearest(from, dir, len);
    return t.has_value() && *t >= 0.0f && *t < len;
}

/* ******************************** The crew ******************************************* */

void Transport::LoadCrew()
{
    auto roster = s_crewByTransport.find(GetEntry());
    if (roster == s_crewByTransport.end())
    {
        return;
    }

    if (!HasDeck())
    {
        sLog.outErrorDb("Transport %u has crew in `creature_transport` but no deck mesh; crew not spawned.",
                        GetEntry());
        return;
    }

    for (CrewSpawn const& spawn : roster->second)
    {
        CreatureInfo const* cinfo = ObjectMgr::GetCreatureTemplate(spawn.npcEntry);
        if (!cinfo)
        {
            continue;
        }

        // A crew member's guid comes from the STATIC space, not the map-local one, and that
        // is not a detail -- it is the difference between a crew that survives a map seam and
        // one that silently corrupts the map it sails into.
        //
        // Map::GenerateLocalLowGuid draws from a per-map counter, and EVERY map seeds that
        // counter to the same value (Map.cpp: m_CreatureGuids.Set(GetFirstTemporaryCreatureLowGuid())).
        // Two maps therefore hand out the SAME low guids, which is harmless only for as long
        // as an object stays on the map that issued it. A vessel's crew do not: they cross to
        // another map with the ship, and are registered in that map's object store under a guid
        // that map is also busy handing out to its own creatures. The stores are keyed by guid,
        // so one of the two would quietly overwrite the other.
        //
        // The static space is a single server-wide counter (the one `.npc add` uses for DB
        // spawns), so a guid drawn from it is unique on every map at once. Which is exactly
        // what a creature that lives on a moving ship needs to be.
        const uint32 crewGuid = sObjectMgr.GenerateStaticCreatureLowGuid();

        if (!crewGuid)
        {
            sLog.outErrorDb("Transport %u: out of static creature guids; crew %u not spawned.",
                            GetEntry(), spawn.npcEntry);
            continue;
        }

        Creature* crew = new Creature;

        // Created at the vessel's own world token, purely so Creature::Create has a valid
        // place to put it for an instant. It is about to stop having a world position that
        // means anything at all.
        CreatureCreatePos cPos(GetMap(), GetPositionX(), GetPositionY(),
                               GetPositionZ(), GetOrientation());

        if (!crew->Create(crewGuid, cPos, cinfo))
        {
            delete crew;
            continue;
        }

        // Its home is the DECK OFFSET, not a place on the map. That single line is what
        // makes Home (evade) and Random (wander) anchor themselves in the vessel's frame
        // for nothing: both of them read the respawn coord, and for a crew member the
        // respawn coord was never world data in the first place.
        crew->SetRespawnCoord(spawn.lx, spawn.ly, spawn.lz, spawn.lo);
        crew->SetRespawnRadius(spawn.wanderDistance);
        crew->SetDefaultMovementType(MovementGeneratorType(spawn.movementType));

        if (spawn.emote)
        {
            crew->SetUInt32Value(UNIT_NPC_EMOTESTATE, spawn.emote);
        }

        BoardCreature(crew, spawn.lx, spawn.ly, spawn.lz, spawn.lo);

        // Only now, with GetTransportInfo() set, is the motion master allowed to pick a
        // generator: what it picks will immediately ask Motion::FrameFor for its frame,
        // and it must get the deck's, not the world's.
        crew->GetMotionMaster()->Initialize();

        DETAIL_FILTER_LOG(LOG_FILTER_TRANSPORT_MOVES,
                          "Transport %s boarded crew %u at deck offset (%f, %f, %f)",
                          GetName(), spawn.npcEntry, spawn.lx, spawn.ly, spawn.lz);
    }
}

bool Transport::BoardCreature(Creature* crew, float lx, float ly, float lz, float lo)
{
    if (!crew)
    {
        return false;
    }

    // Board FIRST. From this instant GetTransportInfo() is set, which is the single
    // switch that puts the creature into the vessel's frame: Motion::FrameFor routes its
    // movement through TransportFrame, MoveSplineInit::Launch sends its splines as
    // SMSG_MONSTER_MOVE_TRANSPORT, and Unit::UpdateSplineMovement writes its position
    // back as a deck offset instead of a place on the map.
    BoardPassenger(crew, lx, ly, lz, lo);

    // Stamp the world cache once so anything asking from off-ship has a sane answer
    // before the first tick.
    UpdateGlobalPositionOf(crew, lx, ly, lz, lo);

    // Into the GUID store, so a player can click it and a spell can resolve it -- but NOT
    // into a grid cell. It has no business in one: we own its tick, its broadcast and its
    // collision. AddToWorld does the store; Map::Add would do the cell, and Map::Add is
    // exactly what we are not calling. (Creature::Create has already done SetMap.)
    crew->AddToWorld();

    // ...except that it DOES need to be findable. So it goes into OUR container instead of
    // a cell's: the ship is the crew's cell. A grid search near the vessel visits this as
    // if it were one more cell, and every searcher works on it unmodified.
    crew->GetGridRef().link(&m_crewMap, crew);

    m_crew.push_back(crew);
    return true;
}

uint32 MaNGOS::GatherCrewContainersNear(Map* map, float x, float y, float radius,
                                        CreatureMapType** out, uint32 maxOut)
{
    // The overwhelming majority of maps have no transports at all, and this runs inside
    // every grid search on the server -- so get out of the way fast.
    MapManager::TransportMap& byMap = sMapMgr.m_TransportsByMap;

    MapManager::TransportMap::const_iterator vessels = byMap.find(map->GetId());
    if (vessels == byMap.end())
    {
        return 0;
    }

    uint32 found = 0;

    for (Transport* vessel : vessels->second)
    {
        if (found >= maxOut)
        {
            break;
        }

        if (!vessel->HasCrew() || vessel->GetMapId() != map->GetId())
        {
            continue;
        }

        // The HULL, not the origin: a ship is a hundred yards long, so a blast at the bow
        // overlaps a vessel whose centre is well outside it.
        const float dx = vessel->GetPositionX() - x;
        const float dy = vessel->GetPositionY() - y;
        const float reach = radius + vessel->GetHullRadius();

        if (dx * dx + dy * dy > reach * reach)
        {
            continue;
        }

        out[found++] = &vessel->GetCrewMap();
    }

    return found;
}

void Transport::UnBoardCreature(Creature* crew)
{
    if (!crew)
    {
        return;
    }

    UnBoardPassenger(crew);

    // Out of the vessel's "cell" as well, or a search would keep finding a creature that no
    // longer has a frame -- and would read its now-meaningless world position.
    crew->GetGridRef().unlink();

    m_crew.erase(std::remove(m_crew.begin(), m_crew.end(), crew), m_crew.end());

    crew->RemoveFromWorld();
    delete crew;
}

/**
 * @brief The vessel's own update tick over its crew.
 *
 * This IS the ObjectUpdater. The grid runs one over the objects in its cells
 * (Map::Update -> TypeContainerVisitor<ObjectUpdater>); the crew are in no cell, so
 * nothing would ever tick them -- no AI, no MotionMaster, no spline advance, no auras.
 * They would be statues. The vessel is their map, so the vessel runs their update loop.
 */
void Transport::UpdateCrew(uint32 diff)
{
    for (Creature* crew : m_crew)
    {
        if (crew->IsInWorld())
        {
            crew->Update(diff, diff);
        }
    }
}

void Transport::BoardMinion(Unit* minion)
{
    if (!minion || !minion->IsInWorld() || !minion->IsAlive() || minion->GetTransportInfo())
    {
        return;                                         // gone, or already aboard something
    }

    // Where the minion is standing, expressed as a deck offset. The vessel's pose was
    // solved from its master's last movement packet, so this is exact -- and if the minion
    // is not actually over the deck yet (still on the pier, mid-follow), the drop fails and
    // we simply try again next tick, once it has caught up.
    float lx, ly, lz, lo;
    CalculateLocalPositionOf(minion->GetPositionX(), minion->GetPositionY(), minion->GetPositionZ(),
                             minion->GetOrientation(), lx, ly, lz, lo);

    const auto deckZ = DeckHeightAt(lx, ly, lz, DECK_BOARD_SEARCH_UP, DECK_BOARD_SEARCH_DOWN);
    if (!deckZ)
    {
        return;                                         // not over the deck
    }

    // Grid resident: it keeps its cell, and its world position keeps being maintained
    // through the grid. All we lend it is a floor.
    BoardPassenger(minion, lx, ly, *deckZ, lo, true);

    // Its motion is now expressed in our frame, so whatever it had planned in the world's
    // frame is meaningless. Start it over.
    //
    // (A totem never moves, so this costs it nothing -- but it still needs the frame, or
    // its Z would be resolved against the sea floor and it would sink out of the deck.)
    minion->GetMotionMaster()->Initialize();

    DETAIL_FILTER_LOG(LOG_FILTER_TRANSPORT_MOVES, "Transport %s boarded minion %s at deck (%f, %f, %f)",
                      GetName(), minion->GetGuidStr().c_str(), lx, ly, *deckZ);
}

void Transport::UpdateMinions()
{
    if (!HasDeck())
    {
        return;    // no floor to offer; leave them to the world, broken as that is
    }

    // Everything a player aboard controls comes aboard with them: the pet, the mini-pet,
    // the guardians (a warlock's infernal, a druid's treants) and the totems. All of them
    // are server-driven, so all of them need the deck frame -- without it they resolve
    // their Z against the sea floor under the hull and try to path across open ocean.
    for (Player* master : m_playerPassengers)
    {
        master->CallForAllControlledUnits(
            [this](Unit* minion) { BoardMinion(minion); },
            CONTROLLED_PET | CONTROLLED_MINIPET | CONTROLLED_GUARDIANS | CONTROLLED_TOTEMS);
    }

    // And put ashore any minion whose master is no longer aboard (or which has died, or
    // been dismissed). One left boarded after its master walked off would keep walking a
    // deck that is sailing out from under it.
    std::vector<Unit*> leaving;

    for (PassengerMap::value_type const& entry : GetPassengers())
    {
        if (!entry.second->IsGridResident())
        {
            continue;                                   // crew: ours, and staying
        }

        Unit* minion = static_cast<Unit*>(entry.first);
        Unit* master = minion->GetOwner();

        const bool masterAboard =
            master && master->GetTypeId() == TYPEID_PLAYER &&
            m_playerPassengers.find(static_cast<Player*>(master)) != m_playerPassengers.end();

        if (!masterAboard || !minion->IsInWorld() || !minion->IsAlive())
        {
            leaving.push_back(minion);
        }
    }

    for (Unit* minion : leaving)
    {
        UnBoardPassenger(minion);
        minion->GetMotionMaster()->Initialize();
    }
}

/* ******************************** Pose recovery ************************************** */

bool Transport::HasFreshPose() const
{
    return m_poseAge < POSE_TRUST_MS;
}

void Transport::ObservePose(MovementInfo const& mi)
{
    // yaw = worldO - localO, and the origin follows by un-rotating the offset out of the
    // world position. One player aboard is enough: the client sends both coordinate
    // systems, orientations included, so the system is not merely solvable but
    // over-determined.
    const float yaw = MapManager::NormalizeOrientation(mi.GetPos()->o - mi.GetTransportPos()->o);

    const float s = sin(yaw);
    const float c = cos(yaw);

    const float lx = mi.GetTransportPos()->x;
    const float ly = mi.GetTransportPos()->y;

    const float ox = mi.GetPos()->x - (lx * c - ly * s);
    const float oy = mi.GetPos()->y - (lx * s + ly * c);
    const float oz = mi.GetPos()->z - mi.GetTransportPos()->z;

    Relocate(ox, oy, oz, yaw);
    m_poseAge = 0;

    // The crew's world cache is now stale by exactly the correction we just made. Refresh
    // it -- their DECK positions did not move an inch, and must not.
    UpdateGlobalPositions();
}

struct keyFrame
{
    explicit keyFrame(TaxiPathNodeEntry const& _node) : node(&_node),
        distSinceStop(-1.0f), distUntilStop(-1.0f), distFromPrev(-1.0f), tFrom(0.0f), tTo(0.0f)
    {
    }

    TaxiPathNodeEntry const* node;

    float distSinceStop;
    float distUntilStop;
    float distFromPrev;
    float tFrom, tTo;
};

/**
 * @brief Builds the waypoint timeline used by a global transport route.
 *
 * @return true if waypoint generation succeeded.
 */
bool Transport::GenerateWaypoints(uint32 pathid, std::set<uint32>& mapids)
{
    if (pathid >= sTaxiPathNodesByPath.size())
    {
        return false;
    }

    TaxiPathNodeList const& path = sTaxiPathNodesByPath[pathid];

    std::vector<keyFrame> keyFrames;
    int mapChange = 0;
    mapids.clear();
    for (size_t i = 1; i < path.size() - 1; ++i)
    {
        if (mapChange == 0)
        {
            TaxiPathNodeEntry const& node_i = path[i];
            if (node_i.ContinentID == path[i + 1].ContinentID)
            {
                keyFrame k(node_i);
                keyFrames.push_back(k);
                mapids.insert(k.node->ContinentID);
            }
            else
            {
                mapChange = 1;
            }
        }
        else
        {
            --mapChange;
        }
    }

    int lastStop = -1;
    int firstStop = -1;

    // first cell is arrived at by teleportation :S
    keyFrames[0].distFromPrev = 0;
    if (keyFrames[0].node->Flags == 2)
    {
        lastStop = 0;
    }

    // find the rest of the distances between key points
    for (size_t i = 1; i < keyFrames.size(); ++i)
    {
        if ((keyFrames[i].node->Flags == 1) || (keyFrames[i].node->ContinentID != keyFrames[i - 1].node->ContinentID))
        {
            keyFrames[i].distFromPrev = 0;
        }
        else
        {
            keyFrames[i].distFromPrev =
                sqrt(pow(keyFrames[i].node->LocX - keyFrames[i - 1].node->LocX, 2) +
                     pow(keyFrames[i].node->LocY - keyFrames[i - 1].node->LocY, 2) +
                     pow(keyFrames[i].node->LocZ - keyFrames[i - 1].node->LocZ, 2));
        }
        if (keyFrames[i].node->Flags == 2)
        {
            // remember first stop frame
            if (firstStop == -1)
            {
                firstStop = i;
            }
            lastStop = i;
        }
    }

    float tmpDist = 0;
    for (size_t i = 0; i < keyFrames.size(); ++i)
    {
        int j = (i + lastStop) % keyFrames.size();
        if (keyFrames[j].node->Flags == 2)
        {
            tmpDist = 0;
        }
        else
        {
            tmpDist += keyFrames[j].distFromPrev;
        }
        keyFrames[j].distSinceStop = tmpDist;
    }

    for (int i = int(keyFrames.size()) - 1; i >= 0; --i)
    {
        int j = (i + (firstStop + 1)) % keyFrames.size();
        tmpDist += keyFrames[(j + 1) % keyFrames.size()].distFromPrev;
        keyFrames[j].distUntilStop = tmpDist;
        if (keyFrames[j].node->Flags == 2)
        {
            tmpDist = 0;
        }
    }

    for (size_t i = 0; i < keyFrames.size(); ++i)
    {
        if (keyFrames[i].distSinceStop < (30 * 30 * 0.5f))
        {
            keyFrames[i].tFrom = sqrt(2 * keyFrames[i].distSinceStop);
        }
        else
        {
            keyFrames[i].tFrom = ((keyFrames[i].distSinceStop - (30 * 30 * 0.5f)) / 30) + 30;
        }

        if (keyFrames[i].distUntilStop < (30 * 30 * 0.5f))
        {
            keyFrames[i].tTo = sqrt(2 * keyFrames[i].distUntilStop);
        }
        else
        {
            keyFrames[i].tTo = ((keyFrames[i].distUntilStop - (30 * 30 * 0.5f)) / 30) + 30;
        }

        keyFrames[i].tFrom *= 1000;
        keyFrames[i].tTo *= 1000;
    }

    //    for (int i = 0; i < keyFrames.size(); ++i) {
    //        sLog.outString("%f, %f, %f, %f, %f, %f, %f", keyFrames[i].x, keyFrames[i].y, keyFrames[i].distUntilStop, keyFrames[i].distSinceStop, keyFrames[i].distFromPrev, keyFrames[i].tFrom, keyFrames[i].tTo);
    //    }

    // Now we're completely set up; we can move along the length of each waypoint at 100 ms intervals
    // speed = max(30, t) (remember x = 0.5s^2, and when accelerating, a = 1 unit/s^2
    int t = 0;
    bool teleport = false;
    if (keyFrames[keyFrames.size() - 1].node->ContinentID != keyFrames[0].node->ContinentID)
    {
        teleport = true;
    }

    WayPoint pos(keyFrames[0].node->ContinentID, keyFrames[0].node->LocX, keyFrames[0].node->LocY, keyFrames[0].node->LocZ, teleport,
                 keyFrames[0].node->ArrivalEventID, keyFrames[0].node->DepartureEventID);
    m_WayPoints[0] = pos;
    t += keyFrames[0].node->Delay * 1000;

    uint32 cM = keyFrames[0].node->ContinentID;
    for (size_t i = 0; i < keyFrames.size() - 1; ++i)
    {
        float d = 0;
        float tFrom = keyFrames[i].tFrom;
        float tTo = keyFrames[i].tTo;

        // keep the generation of all these points; we use only a few now, but may need the others later
        if (((d < keyFrames[i + 1].distFromPrev) && (tTo > 0)))
        {
            while ((d < keyFrames[i + 1].distFromPrev) && (tTo > 0))
            {
                tFrom += 100;
                tTo -= 100;

                if (d > 0)
                {
                    float newX, newY, newZ;
                    newX = keyFrames[i].node->LocX + (keyFrames[i + 1].node->LocX - keyFrames[i].node->LocX) * d / keyFrames[i + 1].distFromPrev;
                    newY = keyFrames[i].node->LocY + (keyFrames[i + 1].node->LocY - keyFrames[i].node->LocY) * d / keyFrames[i + 1].distFromPrev;
                    newZ = keyFrames[i].node->LocZ + (keyFrames[i + 1].node->LocZ - keyFrames[i].node->LocZ) * d / keyFrames[i + 1].distFromPrev;

                    bool teleport = false;
                    if (keyFrames[i].node->ContinentID != cM)
                    {
                        teleport = true;
                        cM = keyFrames[i].node->ContinentID;
                    }

                    //                    sLog.outString("T: %d, D: %f, x: %f, y: %f, z: %f", t, d, newX, newY, newZ);
                    WayPoint pos(keyFrames[i].node->ContinentID, newX, newY, newZ, teleport);
                    if (teleport)
                    {
                        m_WayPoints[t] = pos;
                    }
                }

                if (tFrom < tTo)                            // caught in tFrom dock's "gravitational pull"
                {
                    if (tFrom <= 30000)
                    {
                        d = 0.5f * (tFrom / 1000) * (tFrom / 1000);
                    }
                    else
                    {
                        d = 0.5f * 30 * 30 + 30 * ((tFrom - 30000) / 1000);
                    }
                    d = d - keyFrames[i].distSinceStop;
                }
                else
                {
                    if (tTo <= 30000)
                    {
                        d = 0.5f * (tTo / 1000) * (tTo / 1000);
                    }
                    else
                    {
                        d = 0.5f * 30 * 30 + 30 * ((tTo - 30000) / 1000);
                    }
                    d = keyFrames[i].distUntilStop - d;
                }
                t += 100;
            }
            t -= 100;
        }

        if (keyFrames[i + 1].tFrom > keyFrames[i + 1].tTo)
        {
            t += 100 - ((long)keyFrames[i + 1].tTo % 100);
        }
        else
        {
            t += (long)keyFrames[i + 1].tTo % 100;
        }

        bool teleport = false;
        if ((keyFrames[i + 1].node->Flags == 1) || (keyFrames[i + 1].node->ContinentID != keyFrames[i].node->ContinentID))
        {
            teleport = true;
            cM = keyFrames[i + 1].node->ContinentID;
        }

        WayPoint pos(keyFrames[i + 1].node->ContinentID, keyFrames[i + 1].node->LocX, keyFrames[i + 1].node->LocY, keyFrames[i + 1].node->LocZ, teleport,
                     keyFrames[i + 1].node->ArrivalEventID, keyFrames[i + 1].node->DepartureEventID);

        //        sLog.outString("T: %d, x: %f, y: %f, z: %f, t:%d", t, pos.x, pos.y, pos.z, teleport);

        // if (teleport)
        m_WayPoints[t] = pos;

        t += keyFrames[i + 1].node->Delay * 1000;
        //        sLog.outString("------");
    }

    uint32 timer = t;

    //    sLog.outDetail("    Generated %lu waypoints, total time %u.", (unsigned long)m_WayPoints.size(), timer);

    m_next = m_WayPoints.begin();                           // will used in MoveToNextWayPoint for init m_curr
    MoveToNextWayPoint();                                   // m_curr -> first point
    MoveToNextWayPoint();                                   // skip first point

    m_pathTime = timer;

    m_nextNodeTime = m_curr->first;

    return true;
}

/**
 * @brief Advances the current and next transport waypoint pointers.
 */
void Transport::MoveToNextWayPoint()
{
    m_curr = m_next;

    ++m_next;
    if (m_next == m_WayPoints.end())
    {
        m_next = m_WayPoints.begin();
    }
}

/**
 * @brief Teleports the transport and its player passengers to another map position.
 *
 * @param newMapid The destination map id.
 * @param x The destination X coordinate.
 * @param y The destination Y coordinate.
 * @param z The destination Z coordinate.
 */
void Transport::TeleportTransport(uint32 newMapid, float x, float y, float z)
{
    Map const* oldMap = GetMap();
    Relocate(x, y, z);

    for (PlayerSet::iterator itr = m_playerPassengers.begin(); itr != m_playerPassengers.end();)
    {
        PlayerSet::iterator it2 = itr;
        ++itr;

        Player* plr = *it2;
        if (!plr)
        {
            m_playerPassengers.erase(it2);
            continue;
        }

        if (plr->IsDead() && !plr->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST))
        {
            plr->ResurrectPlayer(1.0);
        }
        plr->TeleportTo(newMapid, x, y, z, GetOrientation(), TELE_TO_NOT_LEAVE_TRANSPORT);

        // WorldPacket data(SMSG_811, 4);
        // data << uint32(0);
        // plr->GetSession()->SendPacket(&data);
    }

    // we need to create and save new Map object with 'newMapid' because if not done -> lead to invalid Map object reference...
    // player far teleport would try to create same instance, but we need it NOW for transport...
    // correct me if I'm wrong O.o
    Map* newMap = sMapMgr.CreateMap(newMapid, this);
    SetMap(newMap);

    // The crew sail WITH the ship. Nothing else moves them: SetMap above moves the VESSEL,
    // and a crew member is a Creature of its own -- carrying its own m_currMap and registered
    // in that map's object store. Leave them behind and they are still, as far as the server
    // is concerned, on the far side of the seam: every visibility test fails IsInMap against
    // the very players standing on the deck beside them, and the whole crew vanishes at the
    // first crossing.
    MoveCrewToMap(newMap);

    if (oldMap != newMap)
    {
        UpdateForMap(oldMap);
        UpdateForMap(newMap);
    }
}

/**
 * @brief Carry the crew across a map seam with their ship.
 *
 * This deliberately does NOT go through Creature::RemoveFromWorld / AddToWorld, which is the
 * obvious way to re-register an object with a different map and is quietly wrong here:
 * RemoveFromWorld UNBOARDS a passenger (it must -- a dying pet would otherwise leave the
 * vessel holding a freed pointer). Run it on the crew and they would step off the deck,
 * losing their TransportInfo, their deck offset and their ONTRANSPORT flag, and arrive as
 * ordinary creatures adrift at sea.
 *
 * They stay boarded. Only their map registration moves. Their GridReference is left alone as
 * well: it is linked into the VESSEL's own container (m_crewMap), which is the ship's
 * standing "cell" and belongs to no map at all.
 */
void Transport::MoveCrewToMap(Map* newMap)
{
    for (Creature* crew : m_crew)
    {
        Map* oldMap = crew->GetMap();

        if (oldMap == newMap)
        {
            continue;
        }

        const bool registered = crew->IsInWorld() && crew->GetObjectGuid().IsCreature();

        if (registered && oldMap)
        {
            oldMap->GetObjectsStore().erase<Creature>(crew->GetObjectGuid(), (Creature*)NULL);
        }

        crew->SetMap(newMap);

        if (registered)
        {
            newMap->GetObjectsStore().insert<Creature>(crew->GetObjectGuid(), crew);
        }
    }

    // The vessel has just jumped a long way. Re-derive the crew's world cache from the new
    // pose now, rather than leaving it pointing at the old continent until the next tick
    // happens to notice the ship moved.
    UpdateGlobalPositions();
}

bool Transport::AddPassenger(Player* passenger)
{
    if (m_playerPassengers.find(passenger) == m_playerPassengers.end())
    {
        DETAIL_LOG("Player %s boarded transport %s.", passenger->GetName(), GetName());
        m_playerPassengers.insert(passenger);
    }
    return true;
}

bool Transport::RemovePassenger(Player* passenger)
{
    if (m_playerPassengers.erase(passenger))
    {
        DETAIL_LOG("Player %s removed from transport %s.", passenger->GetName(), GetName());
    }

    // The last pair of eyes just left. The pose we were being told is now unrefreshed,
    // and will age out of trust on its own -- after which the waypoint token takes back
    // over, which is fine, because there is nobody left to notice it is only an estimate.
    return true;
}


/**
 * @brief Updates global transport position along its generated path.
 */
void Transport::Update(uint32 update_diff, uint32 /*p_time*/)
{
    // The crew are ticked whatever the vessel is doing -- they are its inhabitants, not a
    // side effect of its motion. Nothing else in the server will ever tick them.
    UpdateCrew(update_diff);

    // And nothing else in the server will ever advertise them.
    UpdateVisibility(update_diff);

    // Pets are the world's, not ours -- but they need our floor.
    UpdateMinions();

    // Keep the crew's world cache roughly honest between pose observations (it is only
    // ever read from off-ship, so a little staleness costs nothing).
    TransportBase::Update(update_diff);

    m_poseAge += update_diff;

    if (m_WayPoints.size() <= 1)
    {
        return;
    }

    // While a player is aboard we KNOW where the vessel is -- ObservePose solved it from
    // their movement packet. The waypoint token must not then overwrite the truth with an
    // estimate. We still walk the node list, because that is what fires the seam teleport
    // and the arrival/departure events; we simply do not let it move the ship.
    const bool poseKnown = HasFreshPose();

    m_timer = GameTime::GetGameTimeMS() % m_period;
    while (((m_timer - m_curr->first) % m_pathTime) > ((m_next->first - m_curr->first) % m_pathTime))
    {
        DoEventIfAny(*m_curr, true);

        MoveToNextWayPoint();

        DoEventIfAny(*m_curr, false);

        // first check help in case client-server transport coordinates de-synchronization
        if (m_curr->second.mapid != GetMapId() || m_curr->second.teleport)
        {
            TeleportTransport(m_curr->second.mapid, m_curr->second.x, m_curr->second.y, m_curr->second.z);
        }
        else if (!poseKnown)
        {
            Relocate(m_curr->second.x, m_curr->second.y, m_curr->second.z);
            UpdateGlobalPositions();
        }

        /*
        for(PlayerSet::const_iterator itr = m_passengers.begin(); itr != m_passengers.end();)
        {
            PlayerSet::const_iterator it2 = itr;
            ++itr;
            //(*it2)->SetPosition( m_curr->second.x + (*it2)->GetTransOffsetX(), m_curr->second.y + (*it2)->GetTransOffsetY(), m_curr->second.z + (*it2)->GetTransOffsetZ(), (*it2)->GetTransOffsetO() );
        }
        */

        m_nextNodeTime = m_curr->first;

        if (m_curr == m_WayPoints.begin())
        {
            DETAIL_FILTER_LOG(LOG_FILTER_TRANSPORT_MOVES, " ************ BEGIN ************** %s", GetName());
        }

        DETAIL_FILTER_LOG(LOG_FILTER_TRANSPORT_MOVES, "%s moved to %f %f %f %d", GetName(), m_curr->second.x, m_curr->second.y, m_curr->second.z, m_curr->second.mapid);
    }
}

/**
 * @brief Sends create or out-of-range updates for this transport to players on a map.
 *
 * @param targetMap The map whose players should receive transport visibility updates.
 */
float Transport::GetBroadcastRadius() const
{
    // A WHOLE GRID, not the ordinary visibility distance, and the hull on top of it.
    //
    // A ship is not a mob. It is enormous, it is seen from far further away than anything
    // else in the world, and -- decisively -- the server only ESTIMATES where it is whenever
    // nobody is aboard to tell us (see ObservePose). Advertise it at the normal ~100y unit
    // distance and it does not sail into view; it snaps into existence in front of a player
    // who should have watched it come in over the water, and it does so at a position we were
    // guessing at anyway.
    //
    // A grid is the natural unit: it is the span the map is streamed in at, so anyone who can
    // be looking at us at all is inside one.
    return SIZE_OF_GRIDS + m_hullRadius;
}

void Transport::AppendCrewCreateBlocks(UpdateData& data, Player* observer)
{
    // The crew have no grid cell, so no VisibleNotifier will ever find them and no other
    // code path can announce them. They are announced by their ship, in the ship's packet,
    // and the observer's client-GUID set is stamped here for the same reason.
    for (Creature* crew : m_crew)
    {
        if (!crew->IsInWorld())
        {
            continue;
        }

        crew->BuildCreateUpdateBlockForPlayer(&data, observer);
        observer->m_clientGUIDs.insert(crew->GetObjectGuid());
    }
}

void Transport::AddObserver(Player* observer)
{
    m_observers.insert(observer->GetObjectGuid());
}

void Transport::RemoveObserver(Player* observer)
{
    m_observers.erase(observer->GetObjectGuid());

    observer->m_clientGUIDs.erase(GetObjectGuid());
    for (Creature* crew : m_crew)
    {
        observer->m_clientGUIDs.erase(crew->GetObjectGuid());
    }
}

/**
 * @brief Vouch for the GUIDs this vessel has placed at an observer's client.
 *
 * MaNGOS::VisibleNotifier works by elimination: it copies everything the client is known to
 * have, erases whatever the cell visit re-finds, and destroys the remainder as out of range.
 * That is sound for a body in a grid -- and lethal for a vessel, which is deliberately in no
 * cell at all, and whose crew are in no cell either. The visit re-finds neither, so both are
 * still in the leftovers, and the sweep would destroy every ship in the game on the tick
 * after it was created -- permanently, because the vessel's observer set still says the
 * player knows about it and so it is never re-sent.
 *
 * The observer set IS the authority for a vessel (UpdateVisibility adds to it and sends the
 * creates; it removes from it and sends the out-of-range). So anything it still vouches for
 * is taken out of the notifier's leftovers before the sweep sees them.
 */
void Transport::RetainAtClient(Player* observer, GuidSet& clientGuids) const
{
    if (m_observers.find(observer->GetObjectGuid()) == m_observers.end())
    {
        return;                                     // we do not claim this client
    }

    clientGuids.erase(GetObjectGuid());

    for (Creature* crew : m_crew)
    {
        clientGuids.erase(crew->GetObjectGuid());
    }
}

/**
 * @brief The vessel's own visibility pass.
 *
 * This is the VisibleNotifier the grid would run, if the vessel were in a grid. It is not, so
 * it runs its own -- every tick, rather than once on map entry and then never again, which is
 * what the old code did.
 */
void Transport::UpdateVisibility(uint32 diff)
{
    if (m_visibilityTimer > diff)
    {
        m_visibilityTimer -= diff;
        return;
    }

    m_visibilityTimer = VISIBILITY_UPDATE_MS;

    const float radius = GetBroadcastRadius();

    // Deliberately NOT a cell visit. Cell::Visit silently clamps its search radius to 333
    // yards when it works out which cells to walk (see CellImpl.h), and a vessel now
    // advertises across a whole grid -- so a cell visit would quietly miss precisely the
    // distant players the wide radius exists to reach, and the ship would go on popping into
    // view with nothing in the code to show why.
    //
    // The map's own player list is the honest answer, it is short, and this runs twice a
    // second for a handful of vessels.
    AnyPlayerNearVessel check(this, radius);

    GuidSet stillHere;

    Map::PlayerList const& everyone = GetMap()->GetPlayers();

    for (Map::PlayerList::const_iterator itr = everyone.begin(); itr != everyone.end(); ++itr)
    {
        Player* observer = itr->getSource();

        if (!observer || !check(observer))
        {
            continue;
        }

        stillHere.insert(observer->GetObjectGuid());

        if (m_observers.find(observer->GetObjectGuid()) != m_observers.end())
        {
            continue;                                   // already knows
        }

        // A player standing ON the vessel already has the vessel itself -- the login and
        // teleport paths send it, and re-creating the deck under their own feet would be
        // worse than useless. But they must still be told about the CREW: the crew are in
        // no grid, so no visibility pass will ever find them, and the player aboard is the
        // one person guaranteed to be looking straight at them.
        const bool aboard = (this == observer->GetTransport());

        if (aboard && m_crew.empty())
        {
            m_observers.insert(observer->GetObjectGuid());
            continue;
        }

        UpdateData transData;

        if (!aboard)
        {
            BuildCreateUpdateBlockForPlayer(&transData, observer);
            observer->m_clientGUIDs.insert(GetObjectGuid());
        }

        AppendCrewCreateBlocks(transData, observer);

        WorldPacket packet;
        transData.BuildPacket(&packet, true);
        observer->SendDirectMessage(&packet);

        m_observers.insert(observer->GetObjectGuid());
    }

    // Anybody who has drifted out of range, or left the map, is told the vessel and its
    // crew are gone. Without this the ship would stay at their client forever -- which is
    // precisely what it used to do.
    for (GuidSet::iterator itr = m_observers.begin(); itr != m_observers.end();)
    {
        if (stillHere.find(*itr) != stillHere.end())
        {
            ++itr;
            continue;
        }

        if (Player* gone = GetMap()->GetPlayer(*itr))
        {
            UpdateData transData;
            BuildOutOfRangeUpdateBlock(&transData);
            for (Creature* crew : m_crew)
            {
                crew->BuildOutOfRangeUpdateBlock(&transData);
            }

            WorldPacket packet;
            transData.BuildPacket(&packet, true);
            gone->SendDirectMessage(&packet);

            gone->m_clientGUIDs.erase(GetObjectGuid());
            for (Creature* crew : m_crew)
            {
                gone->m_clientGUIDs.erase(crew->GetObjectGuid());
            }
        }

        itr = m_observers.erase(itr);
    }
}

void Transport::UpdateForMap(Map const* targetMap)
{
    // Only the map SEAM comes through here now: the vessel has just changed maps, so
    // everyone who could see it on the old one must be told it is gone, and the new map's
    // observers will pick it up on the very next visibility pass.
    if (GetMapId() == targetMap->GetId())
    {
        m_visibilityTimer = 0;      // advertise into the new map immediately
        return;
    }

    Map::PlayerList const& pl = targetMap->GetPlayers();
    if (pl.isEmpty())
    {
        m_observers.clear();
        return;
    }

    UpdateData transData;
    BuildOutOfRangeUpdateBlock(&transData);
    for (Creature* crew : m_crew)
    {
        crew->BuildOutOfRangeUpdateBlock(&transData);
    }

    WorldPacket out_packet;
    transData.BuildPacket(&out_packet, true);

    for (Map::PlayerList::const_iterator itr = pl.begin(); itr != pl.end(); ++itr)
    {
        Player* observer = itr->getSource();
        if (this == observer->GetTransport())
        {
            continue;
        }

        if (m_observers.find(observer->GetObjectGuid()) == m_observers.end())
        {
            continue;
        }

        observer->SendDirectMessage(&out_packet);

        observer->m_clientGUIDs.erase(GetObjectGuid());
        for (Creature* crew : m_crew)
        {
            observer->m_clientGUIDs.erase(crew->GetObjectGuid());
        }
    }

    m_observers.clear();
}

void Transport::DoEventIfAny(WayPointMap::value_type const& node, bool departure)
{
    if (uint32 eventid = departure ? node.second.departureEventID : node.second.arrivalEventID)
    {
        DEBUG_FILTER_LOG(LOG_FILTER_TRANSPORT_MOVES, "Taxi %s event %u of node %u of %s \"%s\") path", departure ? "departure" : "arrival", eventid, node.first, GetGuidStr().c_str(), GetName());

        if (!sScriptMgr.OnProcessEvent(eventid, this, this, departure))
        {
            GetMap()->ScriptsStart(DBS_ON_EVENT, eventid, this, this);
        }
    }
}
