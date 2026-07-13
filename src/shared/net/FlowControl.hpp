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

#pragma once

// Reusable backpressure gate owned by each connection's net::SendQueue, so the
// three transports (IOCP / reactor / io_uring) share one correct implementation of
// the FlowControl contract. The SendQueue calls onQueued()/onSent() as bytes enter
// and leave the outbound buffer, and onClosed() once at teardown; a bulk producer
// thread parks in awaitWritable().
//
// The ceiling is measured in BYTES. The SendQueue coalesces queued packets into one
// contiguous stream, so "number of outstanding buffers" is not a meaningful quantity
// — and a memory ceiling is what a producer actually wants to bound anyway.
//
// The transport hot path is lock-free unless a producer is actually parked: the
// common case (world clients, request/response auth) never waits, so onSent() is
// an atomic subtract plus one flag check. Only a bulk producer that is currently
// blocked makes onSent() take the lock to signal it.

#include "net/ISession.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>

namespace net {

class FlowGate : public FlowControl {
public:
    // SendQueue: `n` bytes were appended to / drained from the outbound buffer.
    void onQueued(uint64_t n) { m_outstanding.fetch_add(n, std::memory_order_seq_cst); }

    void onSent(uint64_t n) {
        m_outstanding.fetch_sub(n, std::memory_order_seq_cst);
        // seq_cst pairs the store(m_waiting) in awaitWritable() with this load so a
        // producer that has just parked is never missed (the classic StoreLoad).
        if (m_waiting.load(std::memory_order_seq_cst)) {
            std::lock_guard<std::mutex> lk(m_mu);
            m_cv.notify_all();
        }
    }

    // Transport: the connection is being torn down. Wakes any parked producer,
    // which then observes closed() and stops.
    void onClosed() {
        m_closed.store(true, std::memory_order_seq_cst);
        std::lock_guard<std::mutex> lk(m_mu);   // serialise with a parking producer
        m_cv.notify_all();
    }

    // Producer thread: block until the backlog is <= maxOutstandingBytes or the
    // connection is gone. Returns false once it is gone (stop producing).
    bool awaitWritable(uint64_t maxOutstandingBytes) override {
        if (m_outstanding.load(std::memory_order_seq_cst) <= maxOutstandingBytes) {
            return !m_closed.load(std::memory_order_seq_cst);
        }
        std::unique_lock<std::mutex> lk(m_mu);
        m_waiting.store(true, std::memory_order_seq_cst);
        m_cv.wait(lk, [&]
        {
            return m_closed.load(std::memory_order_seq_cst) ||
                   m_outstanding.load(std::memory_order_seq_cst) <= maxOutstandingBytes;
        });
        m_waiting.store(false, std::memory_order_seq_cst);
        return !m_closed.load(std::memory_order_seq_cst);
    }

private:
    std::atomic<uint64_t>   m_outstanding{0};  ///< bytes queued but not yet written
    std::atomic<bool>       m_waiting{false};  ///< a producer is parked in wait()
    std::atomic<bool>       m_closed{false};   ///< connection torn down
    std::mutex              m_mu;
    std::condition_variable m_cv;
};

} // namespace net
