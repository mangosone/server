#include "TestSupport.hpp"

#include "SessionMailbox.h"

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

namespace
{
std::unique_ptr<WorldPacket> Packet(uint16 opcode, uint8 value)
{
    auto packet = std::make_unique<WorldPacket>(opcode, 1);
    *packet << value;
    return packet;
}

void mailboxTransfersPacketsInFifoOrder()
{
    SessionMailbox mailbox;
    CHECK(mailbox.Enqueue(Packet(1, 0x11)));
    CHECK(mailbox.Enqueue(Packet(2, 0x22)));

    WorldPacket* raw = nullptr;
    CHECK(mailbox.Next(raw));
    std::unique_ptr<WorldPacket> first(raw);
    CHECK(first->GetOpcode() == 1);
    CHECK((*first)[0] == 0x11);

    CHECK(mailbox.Next(raw));
    std::unique_ptr<WorldPacket> second(raw);
    CHECK(second->GetOpcode() == 2);
    CHECK((*second)[0] == 0x22);
    CHECK(!mailbox.Next(raw));
}

void closedMailboxRejectsNewOwnership()
{
    SessionMailbox mailbox;
    mailbox.Close();

    CHECK(mailbox.IsClosed());
    CHECK(!mailbox.Enqueue(Packet(3, 0x33)));
    WorldPacket* raw = nullptr;
    CHECK(!mailbox.Next(raw));
}

void closeRacingProducersLeavesNoPostClosePackets()
{
    SessionMailbox mailbox;
    std::atomic<bool> start{false};
    std::atomic<unsigned> accepted{0};
    std::vector<std::thread> producers;
    for (unsigned producer = 0; producer < 4; ++producer)
    {
        producers.emplace_back([&mailbox, &start, &accepted, producer]()
        {
            while (!start.load())
                std::this_thread::yield();
            for (unsigned packet = 0; packet < 200; ++packet)
            {
                if (mailbox.Enqueue(Packet(uint16(producer + 1), uint8(packet))))
                    accepted.fetch_add(1);
            }
        });
    }

    start.store(true);
    mailbox.Close();
    for (std::thread& producer : producers)
        producer.join();

    unsigned drained = 0;
    WorldPacket* raw = nullptr;
    while (mailbox.Next(raw))
    {
        std::unique_ptr<WorldPacket> packet(raw);
        ++drained;
    }
    CHECK(drained == accepted.load());
    CHECK(!mailbox.Enqueue(Packet(9, 0x99)));
}

void detachedRegistryRouteCannotReachItsReplacement()
{
    auto oldMailbox = std::make_shared<SessionMailbox>();
    std::shared_ptr<SessionMailbox> retainedDelivery = oldMailbox;
    oldMailbox->Close();

    auto replacement = std::make_shared<SessionMailbox>();
    CHECK(!retainedDelivery->Enqueue(Packet(4, 0x44)));
    CHECK(replacement->Enqueue(Packet(5, 0x55)));

    WorldPacket* raw = nullptr;
    CHECK(replacement->Next(raw));
    std::unique_ptr<WorldPacket> packet(raw);
    CHECK(packet->GetOpcode() == 5);
    CHECK(!replacement->Next(raw));
}
}

int main()
{
    mailboxTransfersPacketsInFifoOrder();
    closedMailboxRejectsNewOwnership();
    closeRacingProducersLeavesNoPostClosePackets();
    detachedRegistryRouteCannotReachItsReplacement();
    return mangos::test::failures == 0 ? 0 : 1;
}
