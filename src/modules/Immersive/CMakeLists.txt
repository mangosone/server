#
# Copyright (C) 2005-2018 MaNGOS project <http://getmangos.com/>
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
#

set(LIBRARY_NAME Immersive)

#Add PCH Files
set(immersive_PCH "${CMAKE_CURRENT_SOURCE_DIR}/immersivepch.h")
if(PCH)
    LIST(APPEND immersive_PCH "${CMAKE_CURRENT_SOURCE_DIR}/immersivepch.cpp")
endif()

#Base files
file(GLOB Immersive_Source ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/*.h)
    source_group("Immersive" FILES ${Immersive_Source})
        
#Define base source library
set(LIBRARY_SRCS 
    ${Immersive_Source}
    ${immersive_PCH}
)

include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/src/game
  ${CMAKE_SOURCE_DIR}/src/game/AuctionHouseBot
  ${CMAKE_SOURCE_DIR}/src/game/BattleGround
  ${CMAKE_SOURCE_DIR}/src/game/ChatCommands
  ${CMAKE_SOURCE_DIR}/src/game/Maps
  ${CMAKE_SOURCE_DIR}/src/game/MotionGenerators
  ${CMAKE_SOURCE_DIR}/src/game/movement
  ${CMAKE_SOURCE_DIR}/src/game/Object
  ${CMAKE_SOURCE_DIR}/src/game/OutdoorPvP
  ${CMAKE_SOURCE_DIR}/src/game/References
  ${CMAKE_SOURCE_DIR}/src/game/Server
  ${CMAKE_SOURCE_DIR}/src/game/Tools
  ${CMAKE_SOURCE_DIR}/src/game/vmap
  ${CMAKE_SOURCE_DIR}/src/game/WorldHandlers
  ${CMAKE_SOURCE_DIR}/src/shared
  ${CMAKE_SOURCE_DIR}/src/shared/Config
  ${CMAKE_SOURCE_DIR}/src/shared/Common
  ${CMAKE_SOURCE_DIR}/src/shared/Database
  ${CMAKE_SOURCE_DIR}/src/shared/DataStores
  ${CMAKE_SOURCE_DIR}/src/shared/Log
  ${CMAKE_SOURCE_DIR}/src/shared/Threading
  ${CMAKE_SOURCE_DIR}/src/shared/Utilities
  ${CMAKE_SOURCE_DIR}/src/framework
  ${CMAKE_SOURCE_DIR}/src/framework/Platform
  ${CMAKE_BINARY_DIR}
  ${CMAKE_BINARY_DIR}/src/shared
  ${MYSQL_INCLUDE_DIR}
  ${ACE_INCLUDE_DIR}
)

if(PCH)
  include_directories(${CMAKE_CURRENT_BINARY_DIR})
endif()

add_library(${LIBRARY_NAME} STATIC ${LIBRARY_SRCS})

target_link_libraries(${LIBRARY_NAME} shared detour)

if(UNIX)
  # Both systems don't have libdl and don't need them
  if (NOT (CMAKE_SYSTEM_NAME STREQUAL "FreeBSD" OR CMAKE_SYSTEM_NAME STREQUAL "NetBSD"))
    target_link_libraries(${LIBRARY_NAME} dl)
  endif()
endif()

if(NOT ACE_USE_EXTERNAL)
  add_dependencies(${LIBRARY_NAME} ace)
endif()

# Install config files
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/immersive.conf.dist.in ${CMAKE_CURRENT_BINARY_DIR}/immersive.conf.dist)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/immersive.conf.dist DESTINATION ${CONF_INSTALL_DIR})

if(WIN32 AND MSVC)
  add_custom_command(TARGET ${LIBRARY_NAME}
    POST_BUILD 
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_BINARY_DIR}/immersive.conf.dist" "${CONF_COPY_DIR}"
  )
endif()

# Generate PCH
if(PCH)
    ADD_CXX_PCH(Immersive ${immersive_PCH})
endif()
