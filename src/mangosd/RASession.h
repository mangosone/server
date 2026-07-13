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

/// \addtogroup mangosd
/// @{
/// \file

#ifndef MANGOS_H_RASESSION
#define MANGOS_H_RASESSION

#include "Common.h"
#include "SharedDefines.h"

#include "net/Server.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

/**
 * @brief One remote-access (telnet) connection.
 *
 * A line-oriented login shell: username, password, then commands, which are queued to
 * the world thread exactly as the local CLI's are. This is the second protocol the
 * shared networking engine was built to carry — the ACE reactor, acceptor and
 * hand-rolled output buffer it used to need are all gone, and the transport supplies
 * them for free.
 */
class RASession : public net::ISession
{
    public:

        RASession();
        ~RASession() override;

        // ── net::ISession ────────────────────────────────────────────────────────

        void setPeerAddress(const std::string& address) override { m_address = address; }
        void setSender(net::Sender sender) override { m_sender = std::move(sender); }
        void setCloser(net::Closer closer) override { m_closer = std::move(closer); }

        std::vector<uint8_t> onConnect() override;
        std::vector<uint8_t> onData(const uint8_t* data, size_t len) override;
        void onClose() override;

        bool closed() const override { return m_closed.load(); }

    private:

        /// Stages of the login handshake.
        enum Stage
        {
            NONE,   ///< nothing entered yet; awaiting the username
            LG,     ///< username accepted; awaiting the password
            OK      ///< authenticated; accepting commands
        };

        /// Write a line to the peer. Thread-safe: the world thread calls this from the
        /// command callbacks, and the Sender is a no-op once the connection is gone.
        void Send(const char* message);

        /// Ask the transport to close this connection.
        void Close();

        /// Handle one complete line of input (network thread).
        void HandleLine(const std::string& line);

        void HandleUsername(const std::string& line);
        void HandlePassword(const std::string& line);
        void HandleCommand(const std::string& line);

        /// World-thread callbacks handed to CliCommandHolder.
        static void CommandPrint(void* callbackArg, const char* text);
        static void CommandFinished(void* callbackArg, bool success);

        /// Release the keep-alive taken when a command was queued.
        void ReleaseCommand();

        std::string  m_address;
        net::Sender  m_sender;
        net::Closer  m_closer;

        std::atomic<bool> m_closed;

        /// Partial line carried over between reads (telnet is a byte stream too).
        std::string m_input;

        Stage        m_stage;
        uint32       m_accountId;
        AccountTypes m_accessLevel;

        bool         m_secure;    ///< drop the connection on a bad user/password
        bool         m_stricted;  ///< forbid SEC_CONSOLE-only commands remotely
        AccountTypes m_minLevel;  ///< lowest account level allowed to connect

        // A queued command is executed later, on the world thread, and hands back a raw
        // pointer to this session. Hold a reference to ourselves for as long as any
        // command is outstanding, so the session cannot be destroyed underneath a
        // callback if the peer disconnects mid-command.
        std::mutex                 m_commandLock;
        std::shared_ptr<RASession> m_keepAlive;
        int                        m_commandsPending;
};

/**
 * @brief Owns the remote-access listening socket.
 */
class RaServer
{
    public:

        RaServer() : m_started(false) {}
        ~RaServer() { Stop(); }

        /// Bind and start accepting RA connections. Returns false on failure.
        bool Start(uint16_t port, const std::string& bindIp);
        void Stop();

    private:

        net::Server m_server;
        bool        m_started;
};

#endif
/// @}
