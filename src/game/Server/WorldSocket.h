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
 * @{
 * \file WorldSocket.h
 * \author Derex <derex101@gmail.com>
 */

#ifndef MANGOS_H_WORLDSOCKET
#define MANGOS_H_WORLDSOCKET

#include "Common.h"
#include "Auth/AuthCrypt.h"
#include "Auth/Sha1.h"
#include "Threading/LeasedPtr.h"
#include "WorldPacket.h"

#include "net/ISession.hpp"

#include <atomic>
#include <cstddef>
#include <mutex>
#include <string>
#include <vector>
#include <cstdint>
#include <utility>

class WorldSession;
class QueryResult;

/**
 * @brief The world protocol spoken over one client connection.
 *
 * A pure protocol object: the shared networking engine (net::Server) owns the socket
 * and the byte plumbing and hands the bytes here. Inbound, onData() reassembles the TCP
 * stream into WorldPackets and queues them for the world thread. Outbound, SendPacket()
 * encrypts a header and pushes the bytes through the Sender, which may be called from
 * any thread.
 *
 * @note There is no output buffer, packet overflow queue or write cork here any more.
 * The transport's net::SendQueue already coalesces everything queued between two writes
 * into a single contiguous send — which is exactly what the old 64 KB m_OutBuffer plus
 * its 10 ms cork existed to achieve, and it does so without a heap allocation per packet.
 *
 * @note Lifetime: the transport holds this by shared_ptr, and so does the WorldSession
 * once one is attached. The socket may therefore outlive its connection — deliberately,
 * and safely, because the transport disarms the Sender on teardown, so a world thread
 * still ticking a dying session merely sends into a no-op.
 */
class WorldSocket : public net::ISession
{
    public:

        WorldSocket();
        ~WorldSocket() override;

        // ── net::ISession ────────────────────────────────────────────────────────

        /// Peer address, handed over by the transport right after accept.
        void setPeerAddress(const std::string& address) override { m_Address = address; }

        void setSender(net::Sender sender) override { m_sender = std::move(sender); }
        void setCloser(net::Closer closer) override { m_closer = std::move(closer); }

        /// Greets the client with SMSG_AUTH_CHALLENGE, carrying our seed.
        std::vector<uint8_t> onConnect() override;

        /// Feed received bytes into the reassembler (network thread).
        std::vector<uint8_t> onData(const uint8_t* data, size_t len) override;

        /// The transport is tearing the connection down (network thread).
        void onClose() override;

        bool closed() const override { return m_closed.load(); }

        // ── Used by WorldSession ─────────────────────────────────────────────────

        /// Check if the socket is closed.
        bool IsClosed() const { return m_closed.load(); }

        /// Mark the connection dead and ask the transport to close it.
        void CloseSocket();

        /// Address of the connected peer.
        const std::string& GetRemoteAddress() const { return m_Address; }

        /// Send a packet on the socket. Reentrant; callable from any thread.
        /// @return -1 on failure.
        int SendPacket(const WorldPacket& pct);

    private:

        friend class WorldSession;
        using SessionLease = LeasedPtr<WorldSession>::Lease;

        /// Serialise one packet into an encrypted-header frame ready for the wire.
        std::vector<uint8_t> EncodePacket(const WorldPacket& pct);

        /// Process one fully assembled incoming packet. Takes ownership of @p new_pct.
        int ProcessIncoming(WorldPacket* new_pct);

        /// Called by ProcessIncoming() on CMSG_AUTH_SESSION. Validates what it can
        /// synchronously, then issues an async account lookup so the network thread
        /// never blocks on a DB round-trip.
        int HandleAuthSession(WorldPacket& recvPacket);

        /// Async callback for the account lookup started by HandleAuthSession(). Runs on
        /// the world thread (via Database::ProcessResultQueue), not the network thread.
        /// Takes ownership of (and must delete) @p result.
        void HandleAuthSessionCallback(QueryResult* result);

        /// Current session, held live for the lifetime of the returned lease.
        SessionLease GetSession();
        void SetSession(WorldSession* session);
        void DetachSessionAndWait();

    private:

        /// Address of the remote peer
        std::string m_Address;

        /// Manages encryption of the packet headers
        AuthCrypt m_Crypt;

        /// Serialises EncryptSend, which mutates the cipher state and is reached from
        /// both the world thread (SendPacket) and the network thread (onConnect).
        std::mutex m_CryptSendLock;

        /// Serialises DecryptRecv against Init, which runs on the world thread after
        /// the asynchronous account lookup completes.
        std::mutex m_CryptRecvLock;

        /// Publishes the non-owning session link while preventing deletion during a
        /// network callback that has acquired a lease.
        LeasedPtr<WorldSession> m_session;

        /// Set once the connection is finished with; the transport polls closed().
        std::atomic<bool> m_closed;

        /// Outbound channel and teardown request, armed by the transport before
        /// onConnect(). Both are lifetime-safe: after teardown they become no-ops.
        net::Sender m_sender;
        net::Closer m_closer;

        // ── Inbound reassembly (network thread only) ─────────────────────────────
        //
        // TCP is a stream, so one recv may carry half a header, several whole packets, or
        // anything in between. Bytes accumulate in m_recvBuf until a whole packet can be
        // cut out of them. m_headerPending records that a header has already been
        // decrypted: decryption mutates the cipher, so a header must never be decrypted
        // twice while we wait for its payload to arrive.
        std::vector<uint8_t> m_recvBuf;
        bool                 m_headerPending;
        uint16               m_recvOpcode;
        uint32               m_recvSize;      ///< Payload bytes still expected

        const uint32 m_Seed;

        /// Set once HandleAuthSession() issues the async account lookup and kept set for
        /// the rest of the socket lifetime. A successful callback publishes a session,
        /// while every failure closes the socket, so clearing this during the callback
        /// would only reopen a window for a second auth packet to clobber the fields below.
        bool m_AuthPending;

        /// Captured from CMSG_AUTH_SESSION and read back once the async account lookup
        /// completes. Written once on the network thread before the query is issued, then
        /// read once on the world thread when the callback runs, so it needs no locking.
        uint32      m_AuthBuildNumber;
        std::string m_AuthAccountName;
        uint32      m_AuthClientSeed;
        uint8       m_AuthDigest[SHA_DIGEST_LENGTH];

        /// Remainder of CMSG_AUTH_SESSION (the addon block), copied because the original
        /// packet is freed once HandleAuthSession() returns but the addon list is only
        /// consumed once the account lookup completes.
        WorldPacket m_AuthAddonData;
};

#endif  /* MANGOS_H_WORLDSOCKET */

/// @}
