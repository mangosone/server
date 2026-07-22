#ifndef MANGOS_PROTO_PACKETCODEC_H
#define MANGOS_PROTO_PACKETCODEC_H

#include "Platform/Define.h"
#include "Utilities/WorldPacket.h"

#include <cstddef>
#include <functional>
#include <utility>
#include <vector>

namespace proto
{
constexpr std::size_t CLIENT_HEADER_SIZE = 6;
constexpr std::size_t SERVER_HEADER_SIZE = 4;
constexpr uint32 MAX_CLIENT_PACKET_SIZE = 10240;

enum class DecodeStatus
{
    NeedMore,
    Ready,
    Malformed
};

class PacketCodec
{
public:
    using HeaderDecryptor = std::function<void(uint8* header, std::size_t len)>;
    using HeaderEncryptor = std::function<void(uint8* header, std::size_t len)>;

    explicit PacketCodec(HeaderDecryptor decryptor = HeaderDecryptor());

    DecodeStatus Feed(const uint8* data, std::size_t len,
        std::vector<WorldPacket>& out);

    static std::vector<uint8> Encode(const WorldPacket& packet,
        const HeaderEncryptor& encryptor = HeaderEncryptor());

    void SetHeaderDecryptor(HeaderDecryptor decryptor)
    {
        m_decryptor = std::move(decryptor);
    }

private:
    HeaderDecryptor m_decryptor;
    uint8 m_header[CLIENT_HEADER_SIZE]{};
    std::size_t m_headerFill = 0;
    bool m_haveHeader = false;
    uint16 m_opcode = 0;
    uint32 m_payloadNeeded = 0;
    std::vector<uint8> m_payload;
};
}

#endif
