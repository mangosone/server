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

#ifndef _AUTH_SARC4_H
#define _AUTH_SARC4_H

#include "Platform/Define.h"

/**
 * @brief ARCFOUR, in about thirty lines, because the alternative was a DLL.
 *
 * This used to be EVP_rc4(). OpenSSL 3 moved RC4 to the LEGACY provider, which is not
 * compiled into libcrypto -- it is a separate module discovered on disk at run time. So
 * a cipher of two hundred and fifty-six bytes of state dragged in a deployment
 * requirement (`ossl-modules/legacy.dll` beside the executable, an OPENSSL_MODULES
 * search path, an installer that ships it, CI that copies it) and a start-up check that
 * REFUSED TO RUN without it.
 *
 * It also aimed a loaded gun at the server: the legacy provider is deprecated, and the
 * release that finally drops it would have stopped this emulator booting -- for want of
 * a cipher any competent programmer can write from the specification.
 *
 * The protocol needs RC4 and will always need it, because the 2.4.3 client is never
 * going to be updated. A cipher the protocol mandates forever belongs in the tree.
 *
 * ARCFOUR is fully specified and this is the whole of it: a 256-byte permutation, a
 * key-scheduling pass, and a stream that XORs. There is no interoperability risk of the
 * kind that would justify borrowing an implementation -- CryptoStressTest exercises the
 * same vectors it always did, and it was not touched, so the equivalence is demonstrated
 * rather than asserted.
 *
 * NOT thread-safe, and it must not be: a stream cipher IS its position in the stream, so
 * two threads sharing one is a protocol error, not a race to be locked away. Each
 * direction of each session owns its own.
 *
 * ================== NO INTERNAL DISCARD. DO NOT ADD ONE. ==================
 *
 * This is plain ARCFOUR. The keystream starts at byte zero and nothing is thrown away.
 *
 * The warning matters because the obvious "improvement" is wrong here. WotLK and later
 * are documented as using RC4-drop1024, and a reader who knows that will be tempted to
 * bake the drop into Init. It does not belong to the cipher: where the protocol wants
 * it, THE CALLER does it -- AuthCrypt keys both directions and then pushes 1024 zero
 * bytes through UpdateData, visibly, in its own source. Warden uses this same class and
 * performs no drop at all, so a discard inside Init would be right for one caller and
 * wrong for the other.
 *
 * The failure it would cause is the nasty kind. World traffic would decrypt to garbage
 * and Warden would break in some different way, and both would present as a protocol
 * fault -- a client that connects and then cannot read a packet -- rather than as a
 * crypto one. Nobody would look here.
 *
 * Arc4_TheKeystreamStartsAtByteZero exists to say so out loud if it ever happens.
 * =========================================================================
 */
class ARC4
{
    public:
        /**
         * @brief A cipher expecting a key of `len` bytes, keyed later by Init().
         * @param len Key length in bytes, 1..256.
         */
        explicit ARC4(uint8 len);

        /**
         * @brief A cipher keyed immediately.
         * @param seed Key bytes.
         * @param len Key length in bytes, 1..256.
         */
        ARC4(uint8* seed, uint8 len);

        ~ARC4();

        /**
         * @brief (Re)key the cipher and rewind the keystream.
         *
         * The length is the one given to the constructor -- the signature has no room
         * for another, and every caller in the tree keys with the length it declared.
         *
         * @param seed Key bytes; at least the constructor's length must be readable.
         */
        void Init(uint8* seed);

        /**
         * @brief XOR `len` bytes of `data` with the keystream, in place.
         *
         * Encryption and decryption are the same operation, which is why one method
         * serves both and why calling it twice on the same bytes with the same key
         * returns the original.
         */
        void UpdateData(int len, uint8* data);

    private:
        uint8 m_state[256];   ///< The permutation.
        uint8 m_x;            ///< Stream position; `i` in the specification.
        uint8 m_y;            ///< Stream position; `j` in the specification.
        uint8 m_keyLength;    ///< Bytes Init() will read from its seed.
};

#endif
