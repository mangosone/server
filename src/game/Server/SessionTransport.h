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

/** \addtogroup u2w User to World Communication
 * @{
 * \file SessionTransport.h
 */

#ifndef MANGOS_H_SESSIONTRANSPORT
#define MANGOS_H_SESSIONTRANSPORT

#include <string>

class WorldPacket;

/**
 * @brief Abstract outgoing transport used by WorldSession.
 *
 * WorldSession talks to the network only through this interface, so it does
 * not depend on the concrete WorldSocket type. This keeps the session's
 * networking surface small and explicit, and lets alternative transports plug
 * in without changing session code.
 *
 * The reference-counting methods let a holder (the session) keep the transport
 * alive for as long as it needs it; WorldSocket implements them on top of the
 * ACE event-handler reference counting.
 */
class SessionTransport
{
    public:
        virtual ~SessionTransport() {}

        /// Send a packet to the peer.
        /// @param pct packet to send
        /// @return -1 on failure
        virtual int SendPacket(const WorldPacket& pct) = 0;

        /// Close the transport.
        virtual void CloseSocket(void) = 0;

        /// Check if the transport is closed.
        virtual bool IsClosed(void) const = 0;

        /// Get the address of the connected peer.
        virtual const std::string& GetRemoteAddress(void) const = 0;

        /// Add a reference to keep this transport alive.
        virtual long AddReference(void) = 0;

        /// Remove a previously added reference.
        virtual long RemoveReference(void) = 0;
};

#endif /* MANGOS_H_SESSIONTRANSPORT */

/// @}
