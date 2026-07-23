#include "TestSupport.hpp"

#include "Auth/HMACSHA1.h"
#include "Auth/Sha1.h"
#include "ClientConnection.h"
#include "IWorldGateway.h"
#include "Opcodes.h"
#include "PacketCodec.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
static_assert(uint8(proto::AuthStatus::Ok) == 0x0C);
static_assert(uint8(proto::AuthStatus::Failed) == 0x0D);
static_assert(uint8(proto::AuthStatus::Reject) == 0x0E);
static_assert(uint8(proto::AuthStatus::BadServerProof) == 0x0F);
static_assert(uint8(proto::AuthStatus::Unavailable) == 0x10);
static_assert(uint8(proto::AuthStatus::SystemError) == 0x11);
static_assert(uint8(proto::AuthStatus::BillingError) == 0x12);
static_assert(uint8(proto::AuthStatus::BillingExpired) == 0x13);
static_assert(uint8(proto::AuthStatus::VersionMismatch) == 0x14);
static_assert(uint8(proto::AuthStatus::UnknownAccount) == 0x15);
static_assert(uint8(proto::AuthStatus::IncorrectPassword) == 0x16);
static_assert(uint8(proto::AuthStatus::SessionExpired) == 0x17);
static_assert(uint8(proto::AuthStatus::ServerShuttingDown) == 0x18);
static_assert(uint8(proto::AuthStatus::AlreadyLoggingIn) == 0x19);
static_assert(uint8(proto::AuthStatus::LoginServerNotFound) == 0x1A);
static_assert(uint8(proto::AuthStatus::WaitQueue) == 0x1B);
static_assert(uint8(proto::AuthStatus::Banned) == 0x1C);
static_assert(uint8(proto::AuthStatus::AlreadyOnline) == 0x1D);
static_assert(uint8(proto::AuthStatus::NoTime) == 0x1E);
static_assert(uint8(proto::AuthStatus::DatabaseBusy) == 0x1F);
static_assert(uint8(proto::AuthStatus::Suspended) == 0x20);
static_assert(uint8(proto::AuthStatus::ParentalControl) == 0x21);
static_assert(uint8(proto::AuthStatus::LockedEnforced) == 0x22);

std::vector<uint8> ClientFrame(uint32 opcode, std::initializer_list<uint8> payload)
{
    uint16 const size = uint16(4 + payload.size());
    std::vector<uint8> wire = {
        uint8(size >> 8), uint8(size),
        uint8(opcode), uint8(opcode >> 8), uint8(opcode >> 16), uint8(opcode >> 24)
    };
    wire.insert(wire.end(), payload.begin(), payload.end());
    return wire;
}

std::vector<uint8> ClientFrame(uint32 opcode, const uint8* payload, std::size_t payloadSize)
{
    uint16 const size = uint16(4 + payloadSize);
    std::vector<uint8> wire = {
        uint8(size >> 8), uint8(size),
        uint8(opcode), uint8(opcode >> 8), uint8(opcode >> 16), uint8(opcode >> 24)
    };
    wire.insert(wire.end(), payload, payload + payloadSize);
    return wire;
}

class DummyAuthContext final : public proto::AuthContext
{
};

class FakeGateway final : public proto::IWorldGateway
{
public:
    bool filterResult = true;
    proto::AuthLookup lookup;
    proto::SessionId attachResult = 41;
    unsigned filterCalls = 0;
    unsigned lookupCalls = 0;
    unsigned attachCalls = 0;
    unsigned detachCalls = 0;
    std::vector<uint16> delivered;
    std::vector<std::pair<uint16, bool>> traced;
    proto::AuthRequest attachedRequest;
    std::shared_ptr<proto::IClientLink> retainedLink;
    bool sendDuringAttach = false;
    bool throwOnLookup = false;
    bool throwOnTrace = false;
    std::function<void()> duringAttach;

    bool FilterAuthPacket(WorldPacket&) override
    {
        ++filterCalls;
        return filterResult;
    }

    void TracePacket(const WorldPacket& packet, bool incoming) override
    {
        if (throwOnTrace)
            throw std::runtime_error("simulated trace failure");
        traced.emplace_back(packet.GetOpcode(), incoming);
    }

    proto::AuthLookup LookupAccount(const proto::AuthRequest&) override
    {
        ++lookupCalls;
        if (throwOnLookup)
            throw std::runtime_error("simulated gateway failure");
        return lookup;
    }

    proto::SessionId Attach(const proto::AuthRequest& request,
        const std::shared_ptr<proto::IClientLink>& link,
        const std::shared_ptr<proto::AuthContext>&) override
    {
        ++attachCalls;
        if (duringAttach)
            duringAttach();
        attachedRequest = request;
        retainedLink = link;
        if (sendDuringAttach)
        {
            WorldPacket addon(SMSG_ADDON_INFO, 1);
            addon << uint8(0xA5);
            link->SendPacket(addon);
        }
        return attachResult;
    }

    void Deliver(proto::SessionId id, WorldPacket&& packet) override
    {
        CHECK(id == attachResult);
        delivered.push_back(packet.GetOpcode());
    }

    void Detach(proto::SessionId id) override
    {
        CHECK(id == attachResult);
        ++detachCalls;
    }
};

struct ConnectionHarness
{
    FakeGateway gateway;
    std::shared_ptr<proto::ClientConnection> connection =
        std::make_shared<proto::ClientConnection>(gateway);
    std::vector<std::vector<uint8>> sent;
    unsigned closeCalls = 0;

    ConnectionHarness()
    {
        connection->setPeerAddress("127.0.0.1");
        connection->setSender([this](const uint8* data, std::size_t len)
        {
            sent.emplace_back(data, data + len);
        });
        connection->setCloser([this]() { ++closeCalls; });
    }
};

uint16 ServerOpcode(const std::vector<uint8>& frame)
{
    CHECK(frame.size() >= proto::SERVER_HEADER_SIZE);
    return uint16(frame[2]) | (uint16(frame[3]) << 8);
}

uint32 ServerSeed(const std::vector<uint8>& challenge)
{
    CHECK(challenge.size() == proto::SERVER_HEADER_SIZE + 4);
    return uint32(challenge[4])
        | (uint32(challenge[5]) << 8)
        | (uint32(challenge[6]) << 16)
        | (uint32(challenge[7]) << 24);
}

std::array<uint8, 20> MakeProof(const std::string& account, uint32 clientSeed,
    uint32 serverSeed, BigNumber& sessionKey)
{
    uint8 const zero[4] = {0, 0, 0, 0};
    Sha1Hash sha;
    sha.UpdateData(account);
    sha.UpdateData(zero, sizeof(zero));
    sha.UpdateData(reinterpret_cast<const uint8*>(&clientSeed), sizeof(clientSeed));
    sha.UpdateData(reinterpret_cast<const uint8*>(&serverSeed), sizeof(serverSeed));
    sha.UpdateBigNumbers(&sessionKey, nullptr);
    sha.Finalize();

    std::array<uint8, 20> digest{};
    std::copy(sha.GetDigest(), sha.GetDigest() + digest.size(), digest.begin());
    return digest;
}

std::vector<uint8> AuthFrame(uint32 clientSeed, const std::array<uint8, 20>& digest,
    std::initializer_list<uint8> addonData = {})
{
    WorldPacket packet(CMSG_AUTH_SESSION, 64);
    packet << uint32(8606);
    packet << uint32(0x12345678);
    packet << std::string("ACCOUNT");
    packet << clientSeed;
    packet.append(digest.data(), digest.size());
    if (addonData.size() != 0)
        packet.append(addonData.begin(), addonData.size());
    return ClientFrame(CMSG_AUTH_SESSION, packet.contents(), packet.size());
}

class HeaderCipher
{
public:
    explicit HeaderCipher(BigNumber& sessionKey)
    {
        uint8 seed[SEED_KEY_SIZE] = {
            0x38, 0xA7, 0x83, 0x15, 0xF8, 0x92, 0x25, 0x30,
            0x71, 0x98, 0x67, 0xB1, 0x8C, 0x04, 0xE2, 0xAA
        };
        HMACSHA1 hash(SEED_KEY_SIZE, seed);
        hash.UpdateBigNumber(&sessionKey);
        hash.Finalize();
        m_key.assign(hash.GetDigest(), hash.GetDigest() + SHA_DIGEST_LENGTH);
    }

    void EncryptClientHeader(std::vector<uint8>& frame)
    {
        CHECK(frame.size() >= proto::CLIENT_HEADER_SIZE);
        for (std::size_t offset = 0; offset < proto::CLIENT_HEADER_SIZE; ++offset)
        {
            m_clientIndex %= m_key.size();
            uint8 const encrypted = uint8((frame[offset] ^ m_key[m_clientIndex]) + m_clientPrevious);
            ++m_clientIndex;
            frame[offset] = m_clientPrevious = encrypted;
        }
    }

    void DecryptServerHeader(std::vector<uint8>& frame)
    {
        CHECK(frame.size() >= proto::SERVER_HEADER_SIZE);
        for (std::size_t offset = 0; offset < proto::SERVER_HEADER_SIZE; ++offset)
        {
            m_serverIndex %= m_key.size();
            uint8 const encrypted = frame[offset];
            frame[offset] = uint8((encrypted - m_serverPrevious) ^ m_key[m_serverIndex]);
            ++m_serverIndex;
            m_serverPrevious = encrypted;
        }
    }

private:
    std::vector<uint8> m_key;
    std::size_t m_clientIndex = 0;
    std::size_t m_serverIndex = 0;
    uint8 m_clientPrevious = 0;
    uint8 m_serverPrevious = 0;
};

BigNumber SuccessfulLookup(FakeGateway& gateway)
{
    BigNumber sessionKey;
    sessionKey.SetHexStr("0123456789ABCDEF0123456789ABCDEF01234567");
    gateway.lookup.status = proto::AuthStatus::Ok;
    gateway.lookup.sessionKey = sessionKey;
    gateway.lookup.context = std::make_shared<DummyAuthContext>();
    return sessionKey;
}

BigNumber Authenticate(ConnectionHarness& harness, uint32 clientSeed = 0xA1B2C3D4)
{
    BigNumber sessionKey = SuccessfulLookup(harness.gateway);
    std::vector<uint8> const challenge = harness.connection->onConnect();
    std::array<uint8, 20> const proof = MakeProof("ACCOUNT", clientSeed,
        ServerSeed(challenge), sessionKey);
    std::vector<uint8> const auth = AuthFrame(clientSeed, proof, {0xCA, 0xFE});
    harness.connection->onData(auth.data(), auth.size());
    CHECK(harness.gateway.attachCalls == 1);
    CHECK(!harness.connection->closed());
    return sessionKey;
}

void fragmentedFrameDecodesOnce()
{
    proto::PacketCodec codec;
    std::vector<WorldPacket> packets;
    std::vector<uint8> const wire = ClientFrame(CMSG_KEEP_ALIVE, {0x11, 0x22});

    CHECK(codec.Feed(wire.data(), 3, packets) == proto::DecodeStatus::NeedMore);
    CHECK(packets.empty());
    CHECK(codec.Feed(wire.data() + 3, wire.size() - 3, packets) == proto::DecodeStatus::Ready);
    CHECK(packets.size() == 1);
    CHECK(packets[0].GetOpcode() == CMSG_KEEP_ALIVE);
    CHECK(packets[0].size() == 2);
    CHECK(packets[0][0] == 0x11);
    CHECK(packets[0][1] == 0x22);
}

void combinedFramesPreserveOrder()
{
    proto::PacketCodec codec;
    std::vector<WorldPacket> packets;
    std::vector<uint8> wire = ClientFrame(CMSG_PING, {0x01});
    std::vector<uint8> const second = ClientFrame(CMSG_KEEP_ALIVE, {});
    wire.insert(wire.end(), second.begin(), second.end());

    CHECK(codec.Feed(wire.data(), wire.size(), packets) == proto::DecodeStatus::Ready);
    CHECK(packets.size() == 2);
    CHECK(packets[0].GetOpcode() == CMSG_PING);
    CHECK(packets[1].GetOpcode() == CMSG_KEEP_ALIVE);
}

void splitHeadersDecryptExactlyOnce()
{
    for (std::size_t split = 1; split < proto::CLIENT_HEADER_SIZE; ++split)
    {
        unsigned decryptCalls = 0;
        proto::PacketCodec codec([&decryptCalls](uint8*, std::size_t len)
        {
            ++decryptCalls;
            CHECK(len == proto::CLIENT_HEADER_SIZE);
        });
        std::vector<WorldPacket> packets;
        std::vector<uint8> const wire = ClientFrame(CMSG_KEEP_ALIVE, {0x42});

        CHECK(codec.Feed(wire.data(), split, packets) == proto::DecodeStatus::NeedMore);
        CHECK(decryptCalls == 0);
        CHECK(codec.Feed(wire.data() + split, wire.size() - split, packets) == proto::DecodeStatus::Ready);
        CHECK(decryptCalls == 1);
        CHECK(packets.size() == 1);
    }
}

void malformedFramesAreRejected()
{
    auto checkMalformed = [](std::vector<uint8> const& wire)
    {
        proto::PacketCodec codec;
        std::vector<WorldPacket> packets;
        CHECK(codec.Feed(wire.data(), wire.size(), packets) == proto::DecodeStatus::Malformed);
        CHECK(packets.empty());
    };

    checkMalformed({0x00, 0x03, 0, 0, 0, 0});
    checkMalformed({0x28, 0x01, 0, 0, 0, 0});
    checkMalformed(ClientFrame(10241, {}));
}

void serverFramesUseTheFixed243Header()
{
    WorldPacket packet(SMSG_PONG, 2);
    packet << uint8(0xAA) << uint8(0xBB);

    unsigned encryptCalls = 0;
    std::vector<uint8> const wire = proto::PacketCodec::Encode(
        packet, [&encryptCalls](uint8*, std::size_t len)
        {
            ++encryptCalls;
            CHECK(len == proto::SERVER_HEADER_SIZE);
        });

    CHECK(encryptCalls == 1);
    CHECK(wire.size() == proto::SERVER_HEADER_SIZE + 2);
    CHECK(wire[0] == 0x00);
    CHECK(wire[1] == 0x04);
    CHECK(wire[2] == uint8(uint16(SMSG_PONG) & 0xFF));
    CHECK(wire[3] == uint8(uint16(SMSG_PONG) >> 8));
    CHECK(wire[4] == 0xAA);
    CHECK(wire[5] == 0xBB);
}

void connectionChallengeHasTheExpectedShape()
{
    ConnectionHarness harness;
    std::vector<uint8> const challenge = harness.connection->onConnect();

    CHECK(challenge.size() == proto::SERVER_HEADER_SIZE + 4);
    CHECK(challenge[0] == 0x00);
    CHECK(challenge[1] == 0x06);
    CHECK(ServerOpcode(challenge) == SMSG_AUTH_CHALLENGE);
    CHECK(harness.gateway.traced.size() == 1);
    CHECK(harness.gateway.traced[0] == std::make_pair(uint16(SMSG_AUTH_CHALLENGE), false));
    CHECK(harness.connection->GetRemoteAddress() == "127.0.0.1");
}

void preAuthenticationWorldPacketsAreRejected()
{
    ConnectionHarness harness;
    std::vector<uint8> const frame = ClientFrame(CMSG_KEEP_ALIVE, {});

    harness.connection->onData(frame.data(), frame.size());

    CHECK(harness.connection->closed());
    CHECK(harness.closeCalls == 1);
    CHECK(harness.gateway.delivered.empty());
    CHECK(harness.gateway.traced.size() == 1);
    CHECK(harness.gateway.traced[0] == std::make_pair(uint16(CMSG_KEEP_ALIVE), true));
}

void authenticationFilterVetoSkipsLookupWithoutClosing()
{
    ConnectionHarness harness;
    harness.gateway.filterResult = false;
    std::array<uint8, 20> const digest{};
    std::vector<uint8> const frame = AuthFrame(7, digest);

    harness.connection->onData(frame.data(), frame.size());

    CHECK(harness.gateway.filterCalls == 1);
    CHECK(harness.gateway.lookupCalls == 0);
    CHECK(!harness.connection->closed());
}

void authenticationFilterVetoAllowsALaterAcceptedAttempt()
{
    ConnectionHarness harness;
    BigNumber sessionKey = SuccessfulLookup(harness.gateway);
    uint32 const clientSeed = 0x55667788;
    std::vector<uint8> const challenge = harness.connection->onConnect();
    std::array<uint8, 20> const proof = MakeProof("ACCOUNT", clientSeed,
        ServerSeed(challenge), sessionKey);
    std::vector<uint8> const frame = AuthFrame(clientSeed, proof);

    harness.gateway.filterResult = false;
    harness.connection->onData(frame.data(), frame.size());
    harness.gateway.filterResult = true;
    harness.connection->onData(frame.data(), frame.size());

    CHECK(harness.gateway.filterCalls == 2);
    CHECK(harness.gateway.lookupCalls == 1);
    CHECK(harness.gateway.attachCalls == 1);
    CHECK(!harness.connection->closed());
}

void lookupRejectionSendsStatusAndCloses()
{
    ConnectionHarness harness;
    harness.gateway.lookup.status = proto::AuthStatus::UnknownAccount;
    std::vector<uint8> const challenge = harness.connection->onConnect();
    (void)challenge;
    std::array<uint8, 20> const digest{};
    std::vector<uint8> const frame = AuthFrame(7, digest);

    harness.connection->onData(frame.data(), frame.size());

    CHECK(harness.gateway.lookupCalls == 1);
    CHECK(harness.gateway.attachCalls == 0);
    CHECK(harness.sent.size() == 1);
    CHECK(ServerOpcode(harness.sent[0]) == SMSG_AUTH_RESPONSE);
    CHECK(harness.sent[0].size() == proto::SERVER_HEADER_SIZE + 1);
    CHECK(harness.sent[0][4] == uint8(proto::AuthStatus::UnknownAccount));
    CHECK(harness.connection->closed());
    CHECK(harness.closeCalls == 1);
    std::vector<std::pair<uint16, bool>> const expectedTraces = {
        {uint16(SMSG_AUTH_CHALLENGE), false},
        {uint16(CMSG_AUTH_SESSION), true},
        {uint16(SMSG_AUTH_RESPONSE), false}
    };
    CHECK(harness.gateway.traced == expectedTraces);
}

void gatewayExceptionsCloseWithoutEscapingTheTransportBoundary()
{
    ConnectionHarness harness;
    harness.gateway.throwOnLookup = true;
    harness.connection->onConnect();
    std::array<uint8, 20> const digest{};
    std::vector<uint8> const frame = AuthFrame(7, digest);

    bool escaped = false;
    try
    {
        harness.connection->onData(frame.data(), frame.size());
    }
    catch (std::runtime_error const&)
    {
        escaped = true;
    }

    CHECK(!escaped);
    CHECK(harness.gateway.lookupCalls == 1);
    CHECK(harness.connection->closed());
    CHECK(harness.closeCalls == 1);
}

void traceExceptionsCloseWithoutEscapingTheTransportBoundary()
{
    ConnectionHarness connectHarness;
    connectHarness.gateway.throwOnTrace = true;
    bool connectEscaped = false;
    try
    {
        connectHarness.connection->onConnect();
    }
    catch (std::runtime_error const&)
    {
        connectEscaped = true;
    }
    CHECK(!connectEscaped);
    CHECK(connectHarness.connection->closed());
    CHECK(connectHarness.closeCalls == 1);

    ConnectionHarness dataHarness;
    dataHarness.connection->onConnect();
    dataHarness.gateway.throwOnTrace = true;
    std::vector<uint8> const frame = ClientFrame(CMSG_KEEP_ALIVE, {});
    bool dataEscaped = false;
    try
    {
        dataHarness.connection->onData(frame.data(), frame.size());
    }
    catch (std::runtime_error const&)
    {
        dataEscaped = true;
    }
    CHECK(!dataEscaped);
    CHECK(dataHarness.connection->closed());
    CHECK(dataHarness.closeCalls == 1);

    ConnectionHarness sendHarness;
    sendHarness.connection->onConnect();
    sendHarness.gateway.throwOnTrace = true;
    WorldPacket packet(SMSG_PONG, 0);
    bool sendEscaped = false;
    try
    {
        sendHarness.connection->SendPacket(packet);
    }
    catch (std::runtime_error const&)
    {
        sendEscaped = true;
    }
    CHECK(!sendEscaped);
    CHECK(sendHarness.connection->closed());
    CHECK(sendHarness.closeCalls == 1);
}

void closeDuringAttachDetachesThePublishedSession()
{
    ConnectionHarness harness;
    BigNumber sessionKey = SuccessfulLookup(harness.gateway);
    uint32 const clientSeed = 0x1234ABCD;
    std::vector<uint8> const challenge = harness.connection->onConnect();
    std::array<uint8, 20> const proof = MakeProof("ACCOUNT", clientSeed,
        ServerSeed(challenge), sessionKey);
    std::vector<uint8> const auth = AuthFrame(clientSeed, proof);
    harness.gateway.duringAttach = [&harness]() { harness.connection->onClose(); };

    harness.connection->onData(auth.data(), auth.size());

    CHECK(harness.gateway.attachCalls == 1);
    CHECK(harness.gateway.detachCalls == 1);
    CHECK(harness.connection->closed());
}

void invalidProofSendsFailureAndNeverAttaches()
{
    ConnectionHarness harness;
    SuccessfulLookup(harness.gateway);
    harness.connection->onConnect();
    std::array<uint8, 20> const digest{};
    std::vector<uint8> const frame = AuthFrame(7, digest);

    harness.connection->onData(frame.data(), frame.size());

    CHECK(harness.gateway.lookupCalls == 1);
    CHECK(harness.gateway.attachCalls == 0);
    CHECK(harness.sent.size() == 1);
    CHECK(harness.sent[0][4] == uint8(proto::AuthStatus::Failed));
    CHECK(harness.connection->closed());
}

void successfulAuthenticationInitializesCryptBeforeAttach()
{
    ConnectionHarness harness;
    harness.gateway.sendDuringAttach = true;
    BigNumber sessionKey = Authenticate(harness);

    CHECK(harness.gateway.attachedRequest.build == 8606);
    CHECK(harness.gateway.attachedRequest.unknown == 0x12345678);
    CHECK(harness.gateway.attachedRequest.account == "ACCOUNT");
    CHECK(harness.gateway.attachedRequest.peerAddress == "127.0.0.1");
    CHECK(harness.gateway.attachedRequest.addonData == std::vector<uint8>({0xCA, 0xFE}));
    CHECK(harness.sent.size() == 1);

    HeaderCipher cipher(sessionKey);
    cipher.DecryptServerHeader(harness.sent[0]);
    CHECK(ServerOpcode(harness.sent[0]) == SMSG_ADDON_INFO);
    CHECK(harness.gateway.traced.back() == std::make_pair(uint16(SMSG_ADDON_INFO), false));
}

void attachFailureSendsEncryptedSystemErrorWithoutPublishingASession()
{
    ConnectionHarness harness;
    harness.gateway.attachResult = proto::INVALID_SESSION_ID;
    BigNumber sessionKey = SuccessfulLookup(harness.gateway);
    uint32 const clientSeed = 0x11223344;
    std::vector<uint8> const challenge = harness.connection->onConnect();
    std::array<uint8, 20> const proof = MakeProof("ACCOUNT", clientSeed,
        ServerSeed(challenge), sessionKey);
    std::vector<uint8> const auth = AuthFrame(clientSeed, proof);

    harness.connection->onData(auth.data(), auth.size());

    CHECK(harness.gateway.attachCalls == 1);
    CHECK(harness.gateway.detachCalls == 0);
    CHECK(harness.sent.size() == 1);
    if (harness.sent.size() == 1)
    {
        HeaderCipher cipher(sessionKey);
        cipher.DecryptServerHeader(harness.sent[0]);
        CHECK(ServerOpcode(harness.sent[0]) == SMSG_AUTH_RESPONSE);
        CHECK(harness.sent[0][4] == uint8(proto::AuthStatus::SystemError));
    }
    CHECK(harness.connection->closed());
}

void authenticatedPacketsStayOpaqueToTheConnection()
{
    ConnectionHarness harness;
    BigNumber sessionKey = Authenticate(harness);
    HeaderCipher cipher(sessionKey);

    std::vector<uint8> ping = ClientFrame(CMSG_PING, {0x01, 0x02});
    cipher.EncryptClientHeader(ping);
    harness.connection->onData(ping.data(), ping.size());

    std::vector<uint8> keepAlive = ClientFrame(CMSG_KEEP_ALIVE, {});
    cipher.EncryptClientHeader(keepAlive);
    harness.connection->onData(keepAlive.data(), keepAlive.size());

    CHECK(harness.gateway.delivered.size() == 2);
    CHECK(harness.gateway.delivered[0] == CMSG_PING);
    CHECK(harness.gateway.delivered[1] == CMSG_KEEP_ALIVE);
    CHECK(!harness.connection->closed());
    std::vector<std::pair<uint16, bool>> const expectedTraces = {
        {uint16(SMSG_AUTH_CHALLENGE), false},
        {uint16(CMSG_AUTH_SESSION), true},
        {uint16(CMSG_PING), true},
        {uint16(CMSG_KEEP_ALIVE), true}
    };
    CHECK(harness.gateway.traced == expectedTraces);
}

void coalescedAuthenticationActivatesCryptBeforeTheNextFrame()
{
    ConnectionHarness harness;
    BigNumber sessionKey = SuccessfulLookup(harness.gateway);
    uint32 const clientSeed = 0x10203040;
    std::vector<uint8> const challenge = harness.connection->onConnect();
    std::array<uint8, 20> const proof = MakeProof("ACCOUNT", clientSeed,
        ServerSeed(challenge), sessionKey);
    std::vector<uint8> input = AuthFrame(clientSeed, proof);

    HeaderCipher cipher(sessionKey);
    std::vector<uint8> keepAlive = ClientFrame(CMSG_KEEP_ALIVE, {});
    cipher.EncryptClientHeader(keepAlive);
    input.insert(input.end(), keepAlive.begin(), keepAlive.end());

    harness.connection->onData(input.data(), input.size());

    CHECK(harness.gateway.attachCalls == 1);
    CHECK(harness.gateway.delivered.size() == 1);
    CHECK(harness.gateway.delivered[0] == CMSG_KEEP_ALIVE);
    CHECK(!harness.connection->closed());
}

void fragmentedEncryptedHeadersKeepCipherStateSynchronized()
{
    for (std::size_t split = 1; split < proto::CLIENT_HEADER_SIZE; ++split)
    {
        ConnectionHarness harness;
        BigNumber sessionKey = Authenticate(harness);
        HeaderCipher cipher(sessionKey);
        std::vector<uint8> keepAlive = ClientFrame(CMSG_KEEP_ALIVE, {0x42});
        cipher.EncryptClientHeader(keepAlive);

        harness.connection->onData(keepAlive.data(), split);
        CHECK(harness.gateway.delivered.empty());
        CHECK(!harness.connection->closed());
        harness.connection->onData(keepAlive.data() + split, keepAlive.size() - split);

        CHECK(harness.gateway.delivered.size() == 1);
        CHECK(harness.gateway.delivered[0] == CMSG_KEEP_ALIVE);
        CHECK(!harness.connection->closed());
    }
}

void invalidPostAuthenticationOpcodeClosesInsteadOfDropping()
{
    ConnectionHarness harness;
    BigNumber sessionKey = Authenticate(harness);
    HeaderCipher cipher(sessionKey);
    std::vector<uint8> invalid = ClientFrame(NUM_MSG_TYPES, {});
    cipher.EncryptClientHeader(invalid);

    harness.connection->onData(invalid.data(), invalid.size());

    CHECK(harness.gateway.delivered.empty());
    CHECK(harness.connection->closed());
    CHECK(harness.closeCalls == 1);
}

void authenticationAddonTailReconstructsAtPositionZero()
{
    std::array<uint8, 20> digest{};
    for (std::size_t i = 0; i < digest.size(); ++i)
        digest[i] = uint8(i);

    WorldPacket original(CMSG_AUTH_SESSION, 64);
    original << uint32(8606) << uint32(7) << std::string("ACCOUNT") << uint32(9);
    original.append(digest.data(), digest.size());
    uint8 const addon[] = {0x04, 0x00, 0x00, 0x00, 0x78, 0x9C};
    original.append(addon, sizeof(addon));

    uint32 build;
    uint32 unknown;
    uint32 clientSeed;
    std::string account;
    std::array<uint8, 20> parsedDigest{};
    original >> build >> unknown >> account >> clientSeed;
    original.read(parsedDigest.data(), parsedDigest.size());
    std::vector<uint8> const tail(original.contents() + original.rpos(),
        original.contents() + original.size());

    WorldPacket reconstructed(CMSG_AUTH_SESSION, tail.size());
    reconstructed.append(tail.data(), tail.size());

    CHECK(reconstructed.rpos() == 0);
    CHECK(reconstructed.size() == sizeof(addon));
    CHECK(std::equal(reconstructed.contents(),
        reconstructed.contents() + reconstructed.size(), addon));
}

void repeatedAuthenticationClosesWithoutSecondLookup()
{
    ConnectionHarness harness;
    BigNumber sessionKey = Authenticate(harness);
    HeaderCipher cipher(sessionKey);
    std::array<uint8, 20> const digest{};
    std::vector<uint8> repeated = AuthFrame(9, digest);
    cipher.EncryptClientHeader(repeated);

    harness.connection->onData(repeated.data(), repeated.size());

    CHECK(harness.gateway.lookupCalls == 1);
    CHECK(harness.connection->closed());
    CHECK(harness.closeCalls == 1);
}

void closeDetachesOnceAndLateSendsAreIgnored()
{
    ConnectionHarness harness;
    Authenticate(harness);
    CHECK(harness.gateway.retainedLink != nullptr);

    harness.connection->onClose();
    harness.connection->onClose();
    std::size_t const tracesBeforeLateSend = harness.gateway.traced.size();
    std::size_t const sendsBeforeLateSend = harness.sent.size();
    WorldPacket late(SMSG_PONG, 0);
    harness.gateway.retainedLink->SendPacket(late);

    CHECK(harness.gateway.detachCalls == 1);
    CHECK(harness.gateway.traced.size() == tracesBeforeLateSend);
    CHECK(harness.sent.size() == sendsBeforeLateSend);
}
}

int main()
{
    fragmentedFrameDecodesOnce();
    combinedFramesPreserveOrder();
    splitHeadersDecryptExactlyOnce();
    malformedFramesAreRejected();
    serverFramesUseTheFixed243Header();
    connectionChallengeHasTheExpectedShape();
    preAuthenticationWorldPacketsAreRejected();
    authenticationFilterVetoSkipsLookupWithoutClosing();
    authenticationFilterVetoAllowsALaterAcceptedAttempt();
    lookupRejectionSendsStatusAndCloses();
    gatewayExceptionsCloseWithoutEscapingTheTransportBoundary();
    traceExceptionsCloseWithoutEscapingTheTransportBoundary();
    closeDuringAttachDetachesThePublishedSession();
    invalidProofSendsFailureAndNeverAttaches();
    successfulAuthenticationInitializesCryptBeforeAttach();
    attachFailureSendsEncryptedSystemErrorWithoutPublishingASession();
    authenticatedPacketsStayOpaqueToTheConnection();
    coalescedAuthenticationActivatesCryptBeforeTheNextFrame();
    fragmentedEncryptedHeadersKeepCipherStateSynchronized();
    invalidPostAuthenticationOpcodeClosesInsteadOfDropping();
    authenticationAddonTailReconstructsAtPositionZero();
    repeatedAuthenticationClosesWithoutSecondLookup();
    closeDetachesOnceAndLateSendsAreIgnored();
    return mangos::test::failures == 0 ? 0 : 1;
}
