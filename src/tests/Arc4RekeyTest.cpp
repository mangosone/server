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
 */

/**
 * @file Arc4RekeyTest.cpp
 * @brief The two ways an RC4 implementation goes wrong without failing a vector.
 *
 * A separate file on purpose. CryptoStressTest holds the published vector and was
 * deliberately NOT touched when RC4 moved into the tree, so that it demonstrates the
 * new implementation against the same bytes it always checked. Adding cases to it
 * afterwards would blur that; this is the regression coverage, kept apart.
 *
 * The vector proves nine bytes from a fresh cipher. Everything that can go wrong AFTER
 * those nine bytes is invisible to it, and both classic failures live there:
 *
 *   * Init keys on top of a permutation the previous session left behind, so the stream
 *     depends on how much traffic preceded it;
 *   * Init rebuilds the permutation but forgets to rewind the stream position, so the
 *     cipher decrypts one session and nothing after it.
 *
 * Both produce a server that works perfectly for the first player to log in.
 *
 * Every expectation here is derived inside the test -- from the published vector or by
 * comparing two ciphers -- so nothing depends on a constant recalled from elsewhere.
 */

#include "TestHarness.h"

#include "Auth/ARC4.h"

#include <cstring>
#include <vector>

namespace
{
    /// The vector CryptoStressTest checks, reused as an anchor: any test below that
    /// reproduces it has also re-proved the cipher itself.
    uint8 const KEY[] = { 'K', 'e', 'y' };
    char const* const VECTOR = "bbf316e8d940af0ad3";

    std::vector<uint8> Plaintext()
    {
        char const* p = "Plaintext";
        return std::vector<uint8>(p, p + 9);
    }

    /// `len` bytes of keystream, read out by encrypting zeroes.
    std::vector<uint8> Keystream(ARC4& cipher, int len)
    {
        std::vector<uint8> out(std::size_t(len), 0);
        cipher.UpdateData(len, out.data());
        return out;
    }

    bool Same(std::vector<uint8> const& a, std::vector<uint8> const& b)
    {
        return a.size() == b.size() &&
               (a.empty() || std::memcmp(a.data(), b.data(), a.size()) == 0);
    }
}

/**
 * THE KEYSTREAM STARTS AT BYTE ZERO. Nothing is discarded inside the cipher.
 *
 * WotLK and later are documented as using RC4-drop1024, so a reader who knows that will
 * eventually be tempted to bake the discard into Init. It does not belong there. Where
 * the protocol wants a drop, THE CALLER does it -- AuthCrypt keys both directions and
 * then pushes 1024 zero bytes through UpdateData, in its own source, where it can be
 * seen. The cipher must remain caller-neutral: a discard inside Init would change every
 * caller and double-apply any discard already owned by the protocol layer.
 *
 * The published vector would fail too, but it would fail as a hex mismatch, and whoever
 * added the drop would spend an afternoon doubting their key schedule. This one fails
 * with a name that says what happened.
 *
 * The stakes: world traffic decrypting to garbage and presenting as a protocol fault --
 * a client that connects and then cannot read a single packet -- rather than as a crypto
 * one. Nobody would look at the cipher.
 */
TEST(Arc4_TheKeystreamStartsAtByteZero)
{
    // The keystream the published vector implies: ciphertext XOR "Plaintext".
    const uint8 expected[] = { 0xbb ^ 'P', 0xf3 ^ 'l', 0x16 ^ 'a', 0xe8 ^ 'i',
                               0xd9 ^ 'n', 0x40 ^ 't', 0xaf ^ 'e', 0x0a ^ 'x',
                               0xd3 ^ 't' };

    ARC4 fresh(const_cast<uint8*>(KEY), static_cast<uint8>(sizeof(KEY)));
    const std::vector<uint8> head = Keystream(fresh, 9);
    CHECK(Same(head, std::vector<uint8>(expected, expected + sizeof(expected))));

    // The same, through the other constructor and Init -- a discard added to Init only
    // would slip past a test that used the keying constructor alone.
    ARC4 keyedLater(static_cast<uint8>(sizeof(KEY)));
    keyedLater.Init(const_cast<uint8*>(KEY));
    CHECK(Same(Keystream(keyedLater, 9), head));

    // And it is emphatically not the stream at offset 1024, which is what a baked-in
    // drop-1024 would hand back instead.
    ARC4 dropped(const_cast<uint8*>(KEY), static_cast<uint8>(sizeof(KEY)));
    Keystream(dropped, 1024);
    CHECK(!Same(Keystream(dropped, 9), head));
}

/**
 * THE ONE THAT MATTERS.
 *
 * A cipher that has already encrypted a megabyte, then re-keyed, must be
 * indistinguishable from one that has just been constructed with that key. This is what
 * catches an Init that keys on top of the permutation it inherited: with a clean state
 * the two agree, and the bug only appears once the state is dirty -- which in a server
 * means once a session has carried some traffic.
 */
TEST(Arc4_RekeyingADirtyCipherMatchesAFreshOne)
{
    ARC4 used(static_cast<uint8>(sizeof(KEY)));
    used.Init(const_cast<uint8*>(KEY));

    // Dirty it thoroughly: well past a single pass over the 256-byte permutation.
    std::vector<uint8> traffic(4096, 0xA5);
    used.UpdateData(int(traffic.size()), traffic.data());

    // Now re-key with the very key the published vector uses...
    used.Init(const_cast<uint8*>(KEY));
    std::vector<uint8> after = Plaintext();
    used.UpdateData(int(after.size()), after.data());

    // ...and it must produce the published bytes, exactly as a new cipher would.
    CHECK_HEX(after.data(), after.size(), VECTOR);

    ARC4 fresh(const_cast<uint8*>(KEY), static_cast<uint8>(sizeof(KEY)));
    std::vector<uint8> reference = Plaintext();
    fresh.UpdateData(int(reference.size()), reference.data());
    CHECK(Same(after, reference));
}

/**
 * Re-keying rewinds the stream.
 *
 * Two runs of the same key must give the same keystream from byte zero. An Init that
 * rebuilds the permutation but leaves the position where it was passes the published
 * vector on a fresh cipher and fails here.
 */
TEST(Arc4_RekeyingRewindsTheStream)
{
    ARC4 cipher(static_cast<uint8>(sizeof(KEY)));

    cipher.Init(const_cast<uint8*>(KEY));
    const std::vector<uint8> first = Keystream(cipher, 512);

    cipher.Init(const_cast<uint8*>(KEY));
    const std::vector<uint8> second = Keystream(cipher, 512);

    CHECK(Same(first, second));

    // And the rewind is to the beginning, not merely to somewhere repeatable: the head
    // of that keystream is what turns "Plaintext" into the published bytes.
    std::vector<uint8> text = Plaintext();
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        text[i] ^= first[i];
    }
    CHECK_HEX(text.data(), text.size(), VECTOR);
}

/**
 * A new key means a new stream, and the same stream a fresh cipher would give.
 *
 * Catches an Init that rebuilds the state but ignores its argument -- which would keep
 * decrypting with the previous session's key.
 */
TEST(Arc4_RekeyingWithADifferentKeyTakesTheNewKey)
{
    // Same LENGTH as KEY, deliberately -- see the contract test below.
    uint8 other[] = { 'C', 'o', 'd' };

    ARC4 reused(static_cast<uint8>(sizeof(KEY)));
    reused.Init(const_cast<uint8*>(KEY));
    Keystream(reused, 300);              // use it, then re-key with something else
    reused.Init(other);
    const std::vector<uint8> afterRekey = Keystream(reused, 256);

    ARC4 fresh(other, static_cast<uint8>(sizeof(other)));
    const std::vector<uint8> reference = Keystream(fresh, 256);
    CHECK(Same(afterRekey, reference));

    // Different key, different stream -- otherwise the comparison above would pass for
    // a cipher that ignores keys altogether.
    ARC4 original(const_cast<uint8*>(KEY), static_cast<uint8>(sizeof(KEY)));
    const std::vector<uint8> withOldKey = Keystream(original, 256);
    CHECK(!Same(afterRekey, withOldKey));
}

/**
 * THE KEY LENGTH IS FIXED AT CONSTRUCTION, and Init cannot change it.
 *
 * `Init(uint8*)` takes no length, so it reads exactly as many bytes as the constructor
 * declared. Re-keying with a longer key silently uses only its prefix; with a shorter
 * one it reads past the end of the caller's buffer. This is not new -- the OpenSSL
 * implementation set the key length in the constructor too -- but nothing said so
 * anywhere, and writing this file is how it was found: the first draft re-keyed a
 * three-byte cipher with seven bytes and could not understand why the streams differed.
 *
 * Pinned here so the next person is told rather than left to discover it. If Init ever
 * grows a length, this is the test that should change, and it should change loudly.
 */
TEST(Arc4_InitUsesTheLengthTheConstructorWasGiven)
{
    uint8 sevenBytes[] = { 'S', 'e', 'c', 'r', 'e', 't', '!' };

    // Declared three, re-keyed with seven: only the first three are used.
    ARC4 declaredThree(static_cast<uint8>(3));
    declaredThree.Init(sevenBytes);
    const std::vector<uint8> asThree = Keystream(declaredThree, 128);

    ARC4 prefixOnly(sevenBytes, static_cast<uint8>(3));
    CHECK(Same(asThree, Keystream(prefixOnly, 128)));

    // And it is genuinely not the whole key.
    ARC4 allSeven(sevenBytes, static_cast<uint8>(sizeof(sevenBytes)));
    CHECK(!Same(asThree, Keystream(allSeven, 128)));
}

/**
 * The stream position survives between calls.
 *
 * A packet crypt calls UpdateData once per header, so the keystream is consumed in many
 * small bites. Encrypting in pieces must equal encrypting in one go -- and the pieces
 * here run to 4 KB, sixteen times round the permutation, which is the continuity the
 * nine-byte vector cannot speak to.
 */
TEST(Arc4_TheStreamContinuesAcrossCalls)
{
    const int TOTAL = 4096;

    ARC4 whole(const_cast<uint8*>(KEY), static_cast<uint8>(sizeof(KEY)));
    const std::vector<uint8> once = Keystream(whole, TOTAL);

    ARC4 piecemeal(const_cast<uint8*>(KEY), static_cast<uint8>(sizeof(KEY)));
    std::vector<uint8> pieces;
    pieces.reserve(TOTAL);

    // Uneven bites, including single bytes and a run that straddles the 256-byte
    // boundary, because a position kept per call rather than per cipher fails exactly
    // where the chunk edges fall.
    const int sizes[] = { 1, 5, 2, 250, 3, 1, 511, 17, 1, 64, 1 };
    int taken = 0;
    std::size_t k = 0;
    while (taken < TOTAL)
    {
        int want = sizes[k % (sizeof(sizes) / sizeof(sizes[0]))];
        ++k;
        if (taken + want > TOTAL)
        {
            want = TOTAL - taken;
        }
        const std::vector<uint8> bite = Keystream(piecemeal, want);
        pieces.insert(pieces.end(), bite.begin(), bite.end());
        taken += want;
    }

    CHECK_EQ(pieces.size(), once.size());
    CHECK(Same(pieces, once));
}

/**
 * Encryption is its own inverse, at length.
 *
 * Two ciphers keyed alike, one encrypting and one decrypting, must return the original
 * bytes over a buffer long enough to wrap the permutation many times. This is the
 * property the wire actually relies on, and it fails loudly for any drift between the
 * two directions of a session.
 */
TEST(Arc4_EncryptThenDecryptReturnsTheOriginal)
{
    std::vector<uint8> message(9000);
    for (std::size_t i = 0; i < message.size(); ++i)
    {
        message[i] = uint8((i * 37) ^ (i >> 5));
    }
    const std::vector<uint8> original = message;

    ARC4 out(const_cast<uint8*>(KEY), static_cast<uint8>(sizeof(KEY)));
    out.UpdateData(int(message.size()), message.data());
    CHECK(!Same(message, original));      // it really did encrypt something

    ARC4 in(const_cast<uint8*>(KEY), static_cast<uint8>(sizeof(KEY)));
    in.UpdateData(int(message.size()), message.data());
    CHECK(Same(message, original));
}

/**
 * A key shorter or longer than the state is still a key.
 *
 * The schedule reads the key cyclically, so a one-byte key repeats 256 times and a
 * 64-byte key is consumed once with room to spare. Both are legal; what must not happen
 * is a divide by zero or a read past the end. The session key this protocol uses is
 * forty bytes, so the long case is not hypothetical.
 */
TEST(Arc4_KeyLengthsAtTheEdgesBehave)
{
    uint8 one[] = { 0x2A };
    ARC4 tiny(one, 1);
    const std::vector<uint8> tinyStream = Keystream(tiny, 64);

    ARC4 tinyAgain(static_cast<uint8>(1));
    tinyAgain.Init(one);
    CHECK(Same(tinyStream, Keystream(tinyAgain, 64)));

    uint8 long40[40];
    for (int i = 0; i < 40; ++i)
    {
        long40[i] = uint8(i * 7 + 1);
    }
    ARC4 big(long40, 40);
    const std::vector<uint8> bigStream = Keystream(big, 64);

    ARC4 bigAgain(static_cast<uint8>(40));
    bigAgain.Init(long40);
    CHECK(Same(bigStream, Keystream(bigAgain, 64)));

    // Two different key lengths must not collide into the same stream.
    CHECK(!Same(tinyStream, bigStream));
}
