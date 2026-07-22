#include "TestSupport.hpp"

#include "IWorldGateway.h"
#include "Opcodes.h"
#include "PacketCodec.h"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
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
}

int main()
{
    fragmentedFrameDecodesOnce();
    combinedFramesPreserveOrder();
    splitHeadersDecryptExactlyOnce();
    malformedFramesAreRejected();
    serverFramesUseTheFixed243Header();
    return mangos::test::failures == 0 ? 0 : 1;
}
