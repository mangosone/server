/**
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * World of Warcraft, and all World of Warcraft or Warcraft art, images,
 * and lore are copyrighted by Blizzard Entertainment, Inc.
 */

/** \addtogroup u2w User to World Communication
 *  @{
 *  \file WorldSocketMgr.h
 *  \author Derex <derex101@gmail.com>
 */

#ifndef MANGOS_H_WORLDSOCKETMGR
#define MANGOS_H_WORLDSOCKETMGR

#include "Common.h"
#include "Policies/Singleton.h"

#include "net/Server.hpp"

#include <cstdint>
#include <string>

/**
 * @brief Owns the world server's listening socket.
 *
 * A thin lid on the shared networking engine: it starts a net::Server on the world port
 * and gives it a factory that mints one WorldSocket per accepted connection. The engine
 * owns the threads, the sockets and the byte plumbing; everything protocol-shaped lives
 * in WorldSocket.
 *
 * The ACE reactor, its thread pool, the acceptor and the per-socket buffer tuning that
 * used to live here are gone — the engine does that job for every protocol now, and
 * realmd's AuthServer is the same shape.
 */
class WorldSocketMgr : public MaNGOS::Singleton<WorldSocketMgr>
{
        friend class MaNGOS::Singleton<WorldSocketMgr>;

    public:

        /**
         * @brief Bind and start accepting world connections.
         *
         * @param port   TCP port to listen on.
         * @param bindIp Interface to bind to; empty (or "0.0.0.0") listens on all.
         * @return 0 on success, -1 on failure.
         */
        int StartNetwork(uint16_t port, const std::string& bindIp);

        /// Stop accepting, and tear down every live connection.
        void StopNetwork();

    private:

        WorldSocketMgr();
        ~WorldSocketMgr();

        net::Server m_server;
        bool        m_started;
};

#define sWorldSocketMgr MaNGOS::Singleton<WorldSocketMgr>::Instance()

#endif
/// @}
