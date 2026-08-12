# SPDX-License-Identifier: GPL-3.0-or-later
#
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
# along with this program. If not, see <https://www.gnu.org/licenses/>.

# =============================================================================
# mangos-wmo-probe -- prints the indoor/outdoor and liquid decisions the terrain
# engine makes, with the intermediate values it has no reason to keep.
#
# It links `extractor_mpq`, which carries the baker's MPQ reader and WMO parser
# and brings `terrain` with it, so the tool reads the CLIENT and the BAKE with
# the same code that wrote them -- the only way "the extractor is faithful" is a
# measurement rather than an assumption. No database, no game, no server.
# =============================================================================

add_executable(mangos-wmo-probe WmoProbe.cpp)

target_link_libraries(mangos-wmo-probe PRIVATE extractor_mpq)

set_target_properties(mangos-wmo-probe PROPERTIES FOLDER "tools")

install(TARGETS mangos-wmo-probe DESTINATION ${BIN_DIR}/tools)
