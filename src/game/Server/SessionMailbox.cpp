#include "SessionMailbox.h"

SessionMailbox::~SessionMailbox()
{
    Close();
    WorldPacket* packet = nullptr;
    while (m_packets.next(packet))
        delete packet;
}

bool SessionMailbox::Enqueue(std::unique_ptr<WorldPacket> packet)
{
    if (!packet)
        return false;

    std::lock_guard<std::mutex> guard(m_stateLock);
    if (m_closed)
        return false;

    WorldPacket* const accepted = packet.get();
    m_packets.add(accepted);
    return packet.release() == accepted;
}

bool SessionMailbox::Next(WorldPacket*& packet)
{
    return m_packets.next(packet);
}

void SessionMailbox::Close()
{
    std::lock_guard<std::mutex> guard(m_stateLock);
    m_closed = true;
}

bool SessionMailbox::IsClosed() const
{
    std::lock_guard<std::mutex> guard(m_stateLock);
    return m_closed;
}
