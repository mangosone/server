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

/**
 * @file WorldSocket.cpp
 * @brief World server network socket implementation
 *
 * This file implements WorldSocket which handles individual client
 * connections to the world server. It manages:
 *
 * - TCP socket communication using ACE
 * - Packet encryption/decryption (SRP6)
 * - Packet fragmentation and reassembly
 * - Session creation (account lookup is asynchronous; see
 *   HandleAuthSession()/HandleAuthSessionCallback())
 *
 * The socket uses the SRP6 authentication protocol for secure
 * client-server communication. Ping/pong and keep-alive are handled by
 * WorldSession (see WorldSession::HandlePingOpcode()/HandleKeepAliveOpcode()),
 * not here, so nothing on this network-thread-owned class runs game logic.
 *
 * @see WorldSocket for the socket class
 * @see WorldSession for the player session
 * @see WorldSocketMgr for the socket manager
 */
#include "WorldSocket.h"
#include "Common.h"

#include "Util.h"
#include "World.h"
#include "WorldPacket.h"
#include "SharedDefines.h"
#include "ByteBuffer.h"
#include "AddonHandler.h"
#include "Opcodes.h"
#include "Database/DatabaseEnv.h"
#include "Auth/BigNumber.h"
#include "Auth/Sha1.h"
#include "WorldSession.h"
#include "WorldSocketMgr.h"
#include "Log.h"
#include "DBCStores.h"
#ifdef ENABLE_ELUNA
#include "LuaEngine.h"
#endif /* ENABLE_ELUNA */

#include <cstring>
#include <memory>
#include <utility>

#if defined( __GNUC__ )
#pragma pack(1)
#else
#pragma pack(push,1)
#endif

/**
 * @brief Server packet header structure
 *
 * Header for packets sent from server to client.
 */
struct ServerPktHeader
{
    uint16 size; ///< Packet size
    uint16 cmd;  ///< Opcode
};

/**
 * @brief Client packet header structure
 *
 * Header for packets sent from client to server.
 */
struct ClientPktHeader
{
    uint16 size; ///< Packet size
    uint32 cmd;  ///< Opcode
};

#if defined( __GNUC__ )
#pragma pack()
#else
#pragma pack(pop)
#endif

/**
 * @brief WorldSocket constructor
 *
 * Initializes a new client socket with default values:
 * - Last ping time: zero
 * - Overspeed pings: 0
 * - Session: NULL
 * - Output buffer size: 64KB
 * - Random seed for encryption
 */
WorldSocket::WorldSocket() :
    m_Session(0),
    m_closed(false),
    m_headerPending(false),
    m_recvOpcode(0),
    m_recvSize(0),
    m_Seed(static_cast<uint32>(rand32())),
    m_AuthPending(false),
    m_AuthBuildNumber(0),
    m_AuthClientSeed(0),
    m_AuthDigest{},
    m_AuthAddonData()
{
}

WorldSocket::~WorldSocket()
{
}

/**
 * @brief Mark the connection dead and ask the transport to close it.
 *
 * Idempotent, and safe from any thread: the Closer is disarmed by the transport at
 * teardown, so a late call from the world thread is a no-op rather than a use-after-free.
 */
void WorldSocket::CloseSocket()
{
    if (m_closed.exchange(true))
    {
        return;
    }

    // Detach the session first, so the network thread can never route a packet into a
    // session that is being torn down.
    SetSession(NULL);

    if (m_closer)
    {
        m_closer();
    }
}

/**
 * @brief Serialise a packet into an encrypted-header frame ready for the wire.
 *
 * Wire format is a 4-byte ServerPktHeader -- big-endian size (payload + 2), little-endian
 * opcode -- with the whole header encrypted, followed by the raw payload.
 */
std::vector<uint8_t> WorldSocket::EncodePacket(const WorldPacket& pct)
{
    ServerPktHeader header;
    header.cmd  = pct.GetOpcode();
    header.size = static_cast<uint16>(pct.size() + 2);

    EndianConvertReverse(header.size);
    EndianConvert(header.cmd);

    std::vector<uint8_t> frame;
    frame.reserve(sizeof(header) + pct.size());

    {
        // EncryptSend mutates the cipher state, and SendPacket is reachable from both
        // the world thread and the network thread, so the encrypt-and-append must be
        // atomic: two packets encrypted out of order would decrypt to garbage.
        std::lock_guard<std::mutex> guard(m_CryptSendLock);

        m_Crypt.EncryptSend(reinterpret_cast<uint8*>(&header), sizeof(header));

        const uint8_t* raw = reinterpret_cast<const uint8_t*>(&header);
        frame.insert(frame.end(), raw, raw + sizeof(header));

        if (!pct.empty())
        {
            frame.insert(frame.end(), pct.contents(), pct.contents() + pct.size());
        }
    }

    return frame;
}

/**
 * @brief Send a packet. Reentrant, and callable from any thread.
 *
 * The bytes are handed straight to the transport, which appends them to this
 * connection's outbound buffer. That buffer coalesces everything queued between two
 * writes into one send, so the per-packet output buffer and overflow queue this class
 * used to carry are no longer needed.
 *
 * @return -1 if the connection is gone.
 */
int WorldSocket::SendPacket(const WorldPacket& pct)
{
    if (m_closed.load() || !m_sender)
    {
        return -1;
    }

    if (sLog.IsPacketLoggingEnabled())
    {
        sLog.outWorldPacketDump(0, pct.GetOpcode(), pct.GetOpcodeName(), &pct, false);
    }

    const std::vector<uint8_t> frame = EncodePacket(pct);
    m_sender(frame.data(), frame.size());

    return 0;
}

/// Greet the client with SMSG_AUTH_CHALLENGE, carrying our seed (network thread).
std::vector<uint8_t> WorldSocket::onConnect()
{
    WorldPacket packet(SMSG_AUTH_CHALLENGE, 4);
    packet << m_Seed;

    return EncodePacket(packet);
}

/// The transport is tearing the connection down (network thread).
void WorldSocket::onClose()
{
    m_closed.store(true);

    // Drop the session link so a world tick still holding this socket stops routing
    // through it. The WorldSession itself outlives us; it notices via IsClosed().
    SetSession(NULL);
}

/**
 * @brief Reassemble the TCP stream into packets (network thread).
 *
 * A recv may deliver half a header, several whole packets, or anything between, so bytes
 * accumulate in m_recvBuf until a complete packet can be cut out. A header is decrypted
 * exactly once -- m_headerPending remembers that we already did it -- because decryption
 * advances the cipher and doing it twice would corrupt every packet after it.
 */
std::vector<uint8_t> WorldSocket::onData(const uint8_t* data, size_t len)
{
    if (m_closed.load())
    {
        return {};
    }

    m_recvBuf.insert(m_recvBuf.end(), data, data + len);

    size_t pos = 0;
    for (;;)
    {
        if (!m_headerPending)
        {
            if (m_recvBuf.size() - pos < sizeof(ClientPktHeader))
            {
                break;      // not even a full header yet
            }

            ClientPktHeader header;
            memcpy(&header, m_recvBuf.data() + pos, sizeof(header));
            pos += sizeof(header);

            m_Crypt.DecryptRecv(reinterpret_cast<uint8*>(&header), sizeof(header));

            EndianConvertReverse(header.size);
            EndianConvert(header.cmd);

            if ((header.size < 4) || (header.size > 10240) || (header.cmd > 10240))
            {
                sLog.outError("WorldSocket::onData: client sent malformed packet size = %d , cmd = %d",
                              header.size, header.cmd);
                CloseSocket();
                return {};
            }

            m_recvOpcode    = static_cast<uint16>(header.cmd);
            m_recvSize      = header.size - 4u;   // the opcode's own 4 bytes are counted in
            m_headerPending = true;
        }

        if (m_recvBuf.size() - pos < m_recvSize)
        {
            break;          // header is in hand, payload still incomplete
        }

        WorldPacket* pct = new WorldPacket(OpcodesList(m_recvOpcode), m_recvSize);
        if (m_recvSize > 0)
        {
            pct->resize(m_recvSize);
            memcpy(const_cast<uint8*>(pct->contents()), m_recvBuf.data() + pos, m_recvSize);
            pos += m_recvSize;
        }
        m_headerPending = false;

        if (ProcessIncoming(pct) == -1)
        {
            CloseSocket();
            return {};
        }

        if (m_closed.load())
        {
            return {};
        }
    }

    // Drop what we consumed; whatever is left is the start of the next packet.
    if (pos > 0)
    {
        m_recvBuf.erase(m_recvBuf.begin(), m_recvBuf.begin() + pos);
    }

    // Nothing is ever answered synchronously: packets are queued to the session and
    // handled on the world thread, and replies go back out through the Sender.
    return {};
}

/**
 * @brief Dispatches a fully assembled incoming packet.
 *
 * @param new_pct The packet to process.
 * @return int Zero on success; otherwise -1.
 */
int WorldSocket::ProcessIncoming(WorldPacket* new_pct)
{
    MANGOS_ASSERT(new_pct);

    // manage memory ;)
    std::unique_ptr<WorldPacket> aptr(new_pct);

    const uint16 opcode = new_pct->GetOpcode();

    if (opcode >= NUM_MSG_TYPES)
    {
        sLog.outError("SESSION: received nonexistent opcode 0x%.4X", opcode);
        return -1;
    }

    if (m_closed.load())
    {
        return -1;
    }

    // Dump received packet (opt-in via PacketLoggingEnabled; off by default).
    if (sLog.IsPacketLoggingEnabled())
    {
        sLog.outWorldPacketDump(0, new_pct->GetOpcode(), new_pct->GetOpcodeName(), new_pct, true);
    }

    try
    {
        switch (opcode)
        {
            case CMSG_AUTH_SESSION:
                if (GetSession() || m_AuthPending)
                {
                    sLog.outError("WorldSocket::ProcessIncoming: Player send CMSG_AUTH_SESSION again");
                    return -1;
                }

#ifdef ENABLE_ELUNA
                if (Eluna* e = sWorld.GetEluna())
                {
                    // No session exists yet at this point, so pass NULL to the hook.
                    if (!e->OnPacketReceive(NULL, *new_pct))
                    {
                        return 0;
                    }
                }
#endif /* ENABLE_ELUNA */
                return HandleAuthSession(*new_pct);
            // CMSG_PING and CMSG_KEEP_ALIVE are intentionally not special-cased
            // here: they fall through to the default case below like every
            // other opcode, so they are queued to the session and handled by
            // WorldSession::Update() on the world/map thread instead of
            // in-place on the network thread.
            default:
            {
                // Hold the session lock across QueuePacket so the session cannot be
                // cleared (and later destroyed) while we hand the packet to it.
                // QueuePacket only touches its own queue lock, so no lock order is at
                // risk here.
                std::lock_guard<std::mutex> Guard(m_SessionLock);

                if (m_Session != NULL)
                {
                    // OK ,give the packet to WorldSession
                    aptr.release();
                    m_Session->QueuePacket(new_pct);
                    return 0;
                }
                else
                {
                    sLog.outError("WorldSocket::ProcessIncoming: Client not authed opcode = %u", uint32(opcode));
                    return -1;
                }
            }
        }
    }
    catch (ByteBufferException&)
    {
        WorldSession* session = GetSession();
        sLog.outError("WorldSocket::ProcessIncoming ByteBufferException occured while parsing an instant handled packet (opcode: %u) from client %s, accountid=%i.",
                      opcode, GetRemoteAddress().c_str(), session ? session->GetAccountId() : -1);
        if (sLog.HasLogLevelOrHigher(LOG_LVL_DEBUG))
        {
            DEBUG_LOG("Dumping error-causing packet:");
            new_pct->hexlike();
        }

        if (sWorld.getConfig(CONFIG_BOOL_KICK_PLAYER_ON_BAD_PACKET))
        {
            DETAIL_LOG("Disconnecting session [account id %i / address %s] for badly formatted packet.",
                       session ? session->GetAccountId() : -1, GetRemoteAddress().c_str());

            return -1;
        }
        else
        {
            return 0;
        }
    }

    return 0;
}

/**
 * @brief Validates a CMSG_AUTH_SESSION packet synchronously, then issues an
 * async account lookup so the network thread never blocks on a DB round-trip.
 *
 * @param recvPacket The authentication packet.
 * @return int Zero on success; otherwise -1.
 */
int WorldSocket::HandleAuthSession(WorldPacket& recvPacket)
{
    uint32 clientSeed;
    uint32 unk2;
    uint32 BuiltNumberClient;
    uint8 digest[SHA_DIGEST_LENGTH];
    std::string account;

    // Read the content of the packet
    recvPacket >> BuiltNumberClient;
    recvPacket >> unk2;
    recvPacket >> account;
    recvPacket >> clientSeed;
    recvPacket.read(digest, SHA_DIGEST_LENGTH);

    DEBUG_LOG("WorldSocket::HandleAuthSession: client %u, unk2 %u, account %s, clientseed %u",
              BuiltNumberClient,
              unk2,
              account.c_str(),
              clientSeed);

    // Check the version of client trying to connect
    if (!IsAcceptableClientBuild(BuiltNumberClient))
    {
        WorldPacket packet(SMSG_AUTH_RESPONSE, 1);
        packet << uint8(AUTH_VERSION_MISMATCH);
        SendPacket(packet);

        sLog.outError("WorldSocket::HandleAuthSession: Sent Auth Response (version mismatch).");
        return -1;
    }

    // Get the account information from the realmd database
    std::string safe_account = account; // Duplicate, else will screw the SHA hash verification below
    LoginDatabase.escape_string(safe_account);
    // No SQL injection, username escaped.

    // Stash everything HandleAuthSessionCallback() will need once the async
    // account lookup below completes, since recvPacket does not outlive this
    // function and the socket is otherwise single-threaded during a login.
    m_AuthPending = true;
    m_AuthBuildNumber = BuiltNumberClient;
    m_AuthAccountName = account;
    m_AuthClientSeed = clientSeed;
    memcpy(m_AuthDigest, digest, SHA_DIGEST_LENGTH);
    m_AuthAddonData = recvPacket;

    // Keep the socket alive until HandleAuthSessionCallback() runs. Capturing a
    // shared_ptr is what the transport's shared ownership is for: the connection may be
    // torn down while the query is still in flight, and the callback must still find a
    // live object (it will simply observe closed() and bail).
    auto self = std::static_pointer_cast<WorldSocket>(shared_from_this());

    // Account lookup and ban check in a single round-trip: the account_banned
    // check needs the account id, which this same query produces, so it is
    // expressed as a correlated subquery instead of a second chained query.
    LoginDatabase.AsyncPQuery([self](QueryResult* result)
                              {
                                  self->HandleAuthSessionCallback(result);
                              },
                              "SELECT "
                              "`a`.`id`, "                  // 0
                              "`a`.`gmlevel`, "              // 1
                              "`a`.`sessionkey`, "           // 2
                              "`a`.`last_ip`, "              // 3
                              "`a`.`locked`, "               // 4
                              "`a`.`v`, "                    // 5
                              "`a`.`s`, "                    // 6
                              "`a`.`expansion`, "            // 7
                              "`a`.`mutetime`, "             // 8
                              "`a`.`locale`, "               // 9
                              "`a`.`os`, "                   // 10
                              "(SELECT 1 FROM `account_banned` WHERE `id` = `a`.`id` AND `active` = 1 "
                              "AND (`unbandate` > UNIX_TIMESTAMP() OR `unbandate` = `bandate`) LIMIT 1), " // 11
                              "(SELECT 1 FROM `ip_banned` WHERE (`unbandate` = `bandate` OR `unbandate` > UNIX_TIMESTAMP()) "
                              "AND `ip` = '%s' LIMIT 1) "    // 12
                              "FROM `account` AS `a` "
                              "WHERE `a`.`username` = '%s'",
                              GetRemoteAddress().c_str(), safe_account.c_str());

    return 0;
}

/**
 * @brief Async callback for the account lookup started by HandleAuthSession().
 *
 * Invoked from Database::ProcessResultQueue() (called each tick from
 * World::Update()), so this runs on the world thread, never on the network
 * thread. Finishes what HandleAuthSession() used to do synchronously:
 * ban/lock checks, SHA verification, WorldSession creation and registration,
 * Warden init, and sending the addon packet.
 *
 * @param result The query result; this function takes ownership of it.
 */
void WorldSocket::HandleAuthSessionCallback(QueryResult* result)
{
    std::unique_ptr<QueryResult> resultGuard(result);

    m_AuthPending = false;

    if (m_closed.load())
    {
        return;
    }

    // Stop if the account is not found
    if (!result)
    {
        WorldPacket packet(SMSG_AUTH_RESPONSE, 1);
        packet << uint8(AUTH_UNKNOWN_ACCOUNT);
        SendPacket(packet);

        sLog.outError("WorldSocket::HandleAuthSessionCallback: Sent Auth Response (unknown account).");
        CloseSocket();
        return;
    }

    const bool wardenActive = (sWorld.getConfig(CONFIG_BOOL_WARDEN_WIN_ENABLED) || sWorld.getConfig(CONFIG_BOOL_WARDEN_OSX_ENABLED));
    BigNumber v, s, g, N, K;

    const Field* fields = result->Fetch();

    // Account/IP ban check (evaluated as correlated subqueries in the lookup).
    if (fields[11].GetUInt32() || fields[12].GetUInt32())
    {
        WorldPacket packet(SMSG_AUTH_RESPONSE, 1);
        packet << uint8(AUTH_BANNED);
        SendPacket(packet);

        sLog.outError("WorldSocket::HandleAuthSessionCallback: Sent Auth Response (Account banned).");
        CloseSocket();
        return;
    }

    uint8 expansion = ((sWorld.getConfig(CONFIG_UINT32_EXPANSION) > fields[7].GetUInt8()) ? fields[7].GetUInt8() : sWorld.getConfig(CONFIG_UINT32_EXPANSION));

    N.SetHexStr("894B645E89E1535BBDAD5B8B290650530801B18EBFBF5E8FAB3C82872A3E9BB7");
    g.SetDword(7);

    v.SetHexStr(fields[5].GetString());
    s.SetHexStr(fields[6].GetString());

    const char* sStr = s.AsHexStr();                        // Must be freed by OPENSSL_free()
    const char* vStr = v.AsHexStr();                        // Must be freed by OPENSSL_free()

    DEBUG_LOG("WorldSocket::HandleAuthSessionCallback: (s,v) check s: %s v: %s",
              sStr,
              vStr);

    OPENSSL_free((void*) sStr);
    OPENSSL_free((void*) vStr);

    ///- Re-check ip locking (same check as in realmd).
    if (fields[4].GetBool())
    {
        if (strcmp(fields[3].GetString(), GetRemoteAddress().c_str()))
        {
            WorldPacket packet(SMSG_AUTH_RESPONSE, 1);
            packet << uint8(AUTH_FAILED);
            SendPacket(packet);

            BASIC_LOG("WorldSocket::HandleAuthSessionCallback: Sent Auth Response (Account IP differs).");
            CloseSocket();
            return;
        }
    }

    uint32 id = fields[0].GetUInt32();
    uint32 security = fields[1].GetUInt16();
    if (security > SEC_ADMINISTRATOR)                       // prevent invalid security settings in DB
    {
        security = SEC_ADMINISTRATOR;
    }

    K.SetHexStr(fields[2].GetString());

    time_t mutetime = time_t (fields[8].GetUInt64());

    uint8 tmpLoc = fields[9].GetUInt8();
    LocaleConstant locale = tmpLoc >= MAX_LOCALE ? LOCALE_enUS : LocaleConstant(tmpLoc);

    std::string os = fields[10].GetString();

    // Check locked state for server
    AccountTypes allowedAccountType = sWorld.GetPlayerSecurityLimit();

    if (allowedAccountType > SEC_PLAYER && AccountTypes(security) < allowedAccountType)
    {
        WorldPacket packet(SMSG_AUTH_RESPONSE, 1);
        packet << uint8(AUTH_UNAVAILABLE);
        SendPacket(packet);

        BASIC_LOG("WorldSocket::HandleAuthSessionCallback: User tries to login but his security level is not enough");
        CloseSocket();
        return;
    }

    // Warden: Must be done before WorldSession is created
    if (wardenActive && os != "Win" && os != "OSX")
    {
        WorldPacket packet(SMSG_AUTH_RESPONSE, 1);
        packet << uint8(AUTH_REJECT);
        SendPacket(packet);

        BASIC_LOG("WorldSocket::HandleAuthSessionCallback: Client %s attempted to log in using invalid client OS (%s).", GetRemoteAddress().c_str(), os.c_str());
        CloseSocket();
        return;
    }

    // Check that Key and account name are the same on client and server
    uint8 t[4]{ 0 };
    uint32 seed = m_Seed;

    Sha1Hash sha;
    sha.UpdateData(m_AuthAccountName);
    sha.UpdateData((uint8*) & t, 4);
    sha.UpdateData((uint8*) & m_AuthClientSeed, 4);
    sha.UpdateData((uint8*) & seed, 4);
    sha.UpdateBigNumbers(&K, nullptr);
    sha.Finalize();

    if (memcmp(sha.GetDigest(), m_AuthDigest, SHA_DIGEST_LENGTH))
    {
        WorldPacket packet(SMSG_AUTH_RESPONSE, 1);
        packet << uint8(AUTH_FAILED);
        SendPacket(packet);

        sLog.outError("WorldSocket::HandleAuthSessionCallback: Sent Auth Response (authentification failed).");
        CloseSocket();
        return;
    }

    std::string address = GetRemoteAddress();

    DEBUG_LOG("WorldSocket::HandleAuthSessionCallback: Client '%s' authenticated successfully from %s.",
              m_AuthAccountName.c_str(),
              address.c_str());

    // Update the last_ip in the database
    // No SQL injection, username escaped.
    static SqlStatementID updAccount;

    SqlStatement stmt = LoginDatabase.CreateStatement(updAccount, "UPDATE `account` SET `last_ip` = ? WHERE `username` = ?");
    stmt.PExecute(address.c_str(), m_AuthAccountName.c_str());

    WorldSession* session = new WorldSession(id, std::static_pointer_cast<WorldSocket>(shared_from_this()),
                                            AccountTypes(security), expansion, mutetime, locale);

    // Publish the session under the lock so the network thread routes incoming
    // packets to it consistently.
    SetSession(session);

    m_Crypt.Init(&K);

    session->LoadTutorialsData();

    // Warden: Initialize Warden system only if it is enabled by config
    if (wardenActive)
    {
        session->InitWarden(uint16(m_AuthBuildNumber), &K, os);
    }

    sWorld.AddSession(session);

    // Create and send the Addon packet
    WorldPacket SendAddonPacked;
    if (sAddOnHandler.BuildAddonPacket(&m_AuthAddonData, &SendAddonPacked))
    {
        SendPacket(SendAddonPacked);
    }
}

/**
 * @brief Returns the current session pointer under m_SessionLock.
 *
 * The returned pointer is either a live session or NULL, never a dangling
 * pointer, because the session is always cleared under the same lock before it
 * is destroyed.
 *
 * @return WorldSession* The current session, or NULL if none/closed.
 */
WorldSession* WorldSocket::GetSession()
{
    std::lock_guard<std::mutex> Guard(m_SessionLock);
    return m_Session;
}

/**
 * @brief Stores the session pointer under m_SessionLock.
 *
 * @param session The session to associate with this socket, or NULL to clear.
 */
void WorldSocket::SetSession(WorldSession* session)
{
    std::lock_guard<std::mutex> Guard(m_SessionLock);
    m_Session = session;
}
