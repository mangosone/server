/**
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
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
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 *
 * World of Warcraft, and all World of Warcraft or Warcraft art, images,
 * and lore are copyrighted by Blizzard Entertainment, Inc.
 */

#include "ARC4.h"

#include <cstring>

namespace
{
    /// The permutation is a byte table, so every index is taken modulo 256 -- which is
    /// what a uint8 does on its own. Saying so once beats masking at eight call sites.
    inline void Swap(uint8& a, uint8& b)
    {
        const uint8 t = a;
        a = b;
        b = t;
    }

    /// A key of zero bytes would divide by zero in the schedule below, and a key longer
    /// than the permutation cannot be distinguished from its first 256 bytes.
    inline uint8 UsableKeyLength(uint8 len)
    {
        return len ? len : 1;
    }
}

ARC4::ARC4(uint8 len)
    : m_x(0), m_y(0), m_keyLength(UsableKeyLength(len))
{
    // The identity permutation. Not keyed yet: a caller using this constructor has said
    // it will supply the key through Init, and leaving the state identity means a
    // forgotten Init produces an obviously wrong stream rather than a plausible one.
    for (int i = 0; i < 256; ++i)
    {
        m_state[i] = uint8(i);
    }
}

ARC4::ARC4(uint8* seed, uint8 len)
    : m_x(0), m_y(0), m_keyLength(UsableKeyLength(len))
{
    for (int i = 0; i < 256; ++i)
    {
        m_state[i] = uint8(i);
    }
    Init(seed);
}

ARC4::~ARC4()
{
    // The key schedule is derived from a session secret, so it does not outlive the
    // object in freed memory waiting to be read back. memset rather than std::fill
    // because the intent is erasure, and a loop over a member the compiler can see is
    // dead is a loop the compiler may delete.
    std::memset(m_state, 0, sizeof(m_state));
    m_x = 0;
    m_y = 0;
}

void ARC4::Init(uint8* seed)
{
    if (!seed)
    {
        return;
    }

    // === Key schedule (KSA) ===
    //
    // Start from the identity every time. Init is a RE-key as much as a first key --
    // the packet crypt builds a cipher and seeds it afterwards -- and keying on top of
    // a used permutation would produce a stream that depends on how much traffic had
    // gone before it.
    for (int i = 0; i < 256; ++i)
    {
        m_state[i] = uint8(i);
    }

    uint8 j = 0;
    for (int i = 0; i < 256; ++i)
    {
        j = uint8(j + m_state[i] + seed[i % m_keyLength]);
        Swap(m_state[i], m_state[j]);
    }

    // A stream cipher is its position in the stream, so a re-key rewinds it. Forgetting
    // this is the classic way to make a cipher that decrypts the first session and
    // nothing after it.
    m_x = 0;
    m_y = 0;
}

void ARC4::UpdateData(int len, uint8* data)
{
    if (len <= 0 || !data)
    {
        return;
    }

    // === Pseudo-random generation (PRGA) ===
    //
    // Kept in locals and written back once: the two indices are read and written for
    // every byte, and leaving them as members makes the compiler reload them through
    // `this` each time in case `data` aliases the object.
    uint8 x = m_x;
    uint8 y = m_y;

    for (int n = 0; n < len; ++n)
    {
        x = uint8(x + 1);
        y = uint8(y + m_state[x]);
        Swap(m_state[x], m_state[y]);
        data[n] ^= m_state[uint8(m_state[x] + m_state[y])];
    }

    m_x = x;
    m_y = y;
}
