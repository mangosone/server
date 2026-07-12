# MaNGOS is a full featured server for World of Warcraft, supporting
# the following clients: 1.12.x, 2.4.3, 3.3.5a, 4.3.4a and 5.4.8
#
# Copyright (C) 2005-2026 MaNGOS <https://www.getmangos.eu>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

set(TOOLS_DIR "tools")

# Offline baker. It reads the client MPQs and writes the fused .tile files the
# runtime consumes. There is exactly ONE copy of the terrain collision engine, in
# src/game/terrain -- the baker (writer) and the server (reader) compile the same
# sources, so the on-disk format can never drift between them.
#
# src/baker/terrain holds only the *extraction* half, which the runtime never needs:
# the MPQ container reader (StormLib, behind IMpqArchive), the ADT/WDT/WMO/M2 chunk
# loaders, and the tile sources.
#
# DBCs are read with the SERVER's reader: src/shared/DataStores/DBCFileLoader.cpp compiled
# in directly, driven by the format strings in src/game/Server/DBCfmt.h. It cannot be
# linked from the `shared` library instead, because `shared` exports ACE (and MySQL, and
# OpenSSL) as PUBLIC dependencies. Compiling the one .cpp is why DBCFileLoader.h and
# Utilities/ByteConverter.h were made free of Platform/Define.h -- keep them that way.
# src/baker/stores is now only the two thin MPQ-backed stores over that reader.
#
# src/baker/nav bakes the Detour navmesh straight from the fused tiles, replacing the
# retired Movemap-Generator: the surface the pathfinder walks on is exactly the surface
# FusedTerrain collides against. Hence the Recast (builder) + Detour (serializer) links.
#
# Includes: "terrain/AdtParser.hpp" & "stores/..." resolve against src/baker, while
# "terrain/Geometry.hpp" and the rest of the engine resolve against src/game.

# The shared engine core, compiled into every target that touches a .tile.
set(TERRAIN_ENGINE_SRC
    ${CMAKE_SOURCE_DIR}/src/game/terrain/CollisionModel.cpp
    ${CMAKE_SOURCE_DIR}/src/game/terrain/WmoModel.cpp
    ${CMAKE_SOURCE_DIR}/src/game/terrain/TileSerializer.cpp
    ${CMAKE_SOURCE_DIR}/src/game/terrain/Accelerators.cpp
)

# Extraction-only sources (MPQ + chunk parsers + tile sources).
file(GLOB BAKER_EXTRACT_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/terrain/*.cpp
)

# The server's own DBC reader, compiled in
set(DBC_READER_SRC
    ${CMAKE_SOURCE_DIR}/src/shared/DataStores/DBCFileLoader.cpp
)

# Navmesh bake. Reads the baked .tile files (no MPQ) and emits the Detour navmesh the
# server's MMAP layer loads, so pathfinding walks the exact geometry FusedTerrain
# collides against. Replaces the retired Movemap-Generator.
file(GLOB BAKER_NAV_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/nav/*.cpp
)

add_executable(mangos-baker
    baker.cpp
    ${BAKER_EXTRACT_SRC}
    ${BAKER_NAV_SRC}
    ${TERRAIN_ENGINE_SRC}
    ${DBC_READER_SRC}
)

target_compile_definitions(mangos-baker
    PRIVATE
        WITH_MPQ
)

target_include_directories(mangos-baker
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}          # baker's own terrain/ + stores/
        ${CMAKE_SOURCE_DIR}/src/game         # the shared terrain/ engine + Server/DBCfmt.h
        ${CMAKE_SOURCE_DIR}/src/shared       # DataStores/DBCFileLoader.h, Utilities/
        ${CMAKE_SOURCE_DIR}/dep/StormLib/src
)

target_link_libraries(mangos-baker
    PRIVATE
        stormlib
        RecastNavigation::Recast
        RecastNavigation::Detour
        Threads::Threads
)

# Offline probes for baked .tile files. They drive the RUNTIME query path (FusedTerrain
# + the shared terrain engine), NOT the baker's extraction half, so a query here is
# exactly what mangosd computes. No StormLib: they read the baked tiles, they do not
# touch the client MPQs. FusedTerrain.cpp holds only the engine-side queries -- its
# DBC-flavoured server half lives in GridMap.cpp, which these targets never compile.
set(TILE_QUERY_SRC
    ${CMAKE_SOURCE_DIR}/src/game/WorldHandlers/FusedTerrain.cpp
    ${TERRAIN_ENGINE_SRC}
)

# mangos-tiletest       : single-point probe (Z/liquid/area/indoor for one x,y[,z]).
# mangos-height-check   : batch CSV probe with PASS/FAIL scoring against expected floors.
# mangos-accelbench     : BVH/BIH/kd-tree vs Blizzard's WMO BSP -- correctness, then speed.
# mangos-losbench       : segment/LoS probe. The three above all drive DOWNWARD column
#                         queries and are structurally blind to a change in the LoS ray;
#                         this one pairs real spawns into sightlines and scores them.
# mangos-bvhdebug       : bisects an accelbench mismatch to the node that causes it --
#                         accelbench says THAT the BVH is wrong, this says WHY.
add_executable(mangos-tiletest     tiletest.cpp             ${TILE_QUERY_SRC})
add_executable(mangos-height-check mpq_height_check.cpp     ${TILE_QUERY_SRC})
add_executable(mangos-accelbench   accelbench.cpp           ${TILE_QUERY_SRC})
add_executable(mangos-losbench     losbench.cpp             ${TILE_QUERY_SRC})
add_executable(mangos-bvhdebug     bvhdebug.cpp             ${TILE_QUERY_SRC})

foreach(_probe mangos-tiletest mangos-height-check mangos-accelbench mangos-losbench
               mangos-bvhdebug)
    target_include_directories(${_probe}
        PRIVATE
            ${CMAKE_SOURCE_DIR}/src/game
            ${CMAKE_SOURCE_DIR}/src/game/WorldHandlers
    )
    target_link_libraries(${_probe} PRIVATE Threads::Threads)
endforeach()

install(
    TARGETS mangos-baker mangos-tiletest mangos-height-check mangos-accelbench
            mangos-losbench mangos-bvhdebug
    DESTINATION ${BIN_DIR}/${TOOLS_DIR}
)

if(WIN32 AND MSVC)
    install(
        FILES
            $<TARGET_PDB_FILE:mangos-baker>
            $<TARGET_PDB_FILE:mangos-tiletest>
            $<TARGET_PDB_FILE:mangos-height-check>
            $<TARGET_PDB_FILE:mangos-accelbench>
        DESTINATION ${BIN_DIR}/${TOOLS_DIR}
        OPTIONAL
    )
endif()
