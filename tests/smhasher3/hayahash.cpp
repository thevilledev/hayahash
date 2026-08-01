/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * SMHasher3 adapter for hayahash64.
 *
 * Copyright (C) 2026 Ville Vesilehto
 *
 * This file is written against SMHasher3's headers and macros (Platform.h,
 * Hashlib.h, REGISTER_HASH, PUT_U64, seed_t) and exists only to be compiled
 * into that program, whose framework is licensed under the GNU General
 * Public License, either version 3 or (at your option) any later version.
 * It is therefore licensed GPL-3.0-or-later, and is NOT covered by the
 * Unlicense that applies to the rest of this repository. See COPYING in
 * this directory for the license text.
 *
 * This says nothing about hayahash64 itself. The algorithm and its
 * reference implementation, ../../hayahash.h, are public domain under The
 * Unlicense, and merely including that header here does not change its
 * license. Only this glue file is GPL. That is also what the
 * FLAG_IMPL_LICENSE_PUBLIC_DOMAIN below records: SMHasher3's impl_flags
 * describe the license of the hash being tested, not of this adapter.
 *
 * The adapter deliberately #includes the shipped hayahash.h rather than
 * transcribing the algorithm into SMHasher3's house style. A transcription
 * would mean the suite tests a copy of the function instead of the bytes
 * users compile, and it silently rots on every digest change - which is
 * exactly what happened to the earlier hand-ported adapter, which was still
 * registering the v0.2.1 verification value long after the digest moved.
 *
 * SMHasher3 wants two readers per hash: hashfn_native, and hashfn_bswap for
 * the opposite byte order. hayahash.h picks its load path with the
 * compile-time macro HAYAHASH64_INTERNAL_ENDIAN, which it defines
 * unconditionally, so the macro cannot simply be pre-set. Instead the header
 * is included twice in two namespaces, the second time with __BYTE_ORDER__
 * forced to big-endian: on a little-endian host that selects the header's own
 * byte-swapping load path, which is precisely "read the input byte-swapped".
 * Both namespaces therefore hold the real header, compiled two ways.
 *
 * Do not "simplify" this into a single include plus a GET_U64<bswap> rewrite.
 */
#include "Platform.h"
#include "Hashlib.h"

// The header's system includes must be pulled in at global scope before the
// namespace-wrapped includes below, or their declarations land inside the
// namespaces.
#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace hayahash_native {
#include "hayahash.h"
}

namespace hayahash_bswap {
// Reset the header's include guard and every macro it defines unconditionally,
// so the second include re-runs its own configuration from scratch.
#undef HAYAHASH_H
#undef HAYAHASH64_INTERNAL_ENDIAN
#undef HAYAHASH64_INTERNAL_COMPILER_GUARD
#undef HAYAHASH64_INTERNAL_NOINLINE
#undef HAYAHASH64_INTERNAL_TIERS
#undef __BYTE_ORDER__
#define __BYTE_ORDER__ __ORDER_BIG_ENDIAN__
#include "hayahash.h"
}

//------------------------------------------------------------
// hayahash64 takes a signed length (ptrdiff_t, not size_t) and a full 64-bit
// seed, and returns the digest as a value rather than through a pointer.
template <bool bswap>
static void HayaHash64( const void * in, const size_t len, const seed_t seed, void * out ) {
    uint64_t h = bswap ?
            hayahash_bswap::hayahash64(in, (std::ptrdiff_t)len, (uint64_t)seed) :
            hayahash_native::hayahash64(in, (std::ptrdiff_t)len, (uint64_t)seed);

    PUT_U64<bswap>(h, (uint8_t *)out, 0);
}

//------------------------------------------------------------
REGISTER_FAMILY(HayaHash,
   $.src_url    = "https://github.com/thevilledev/hayahash",
   $.src_status = HashFamilyInfo::SRC_ACTIVE
 );

REGISTER_HASH(HayaHash64,
   $.desc            = "hayahash64",
   $.hash_flags      =
         FLAG_HASH_ENDIAN_INDEPENDENT,
   $.impl_flags      =
         FLAG_IMPL_CANONICAL_LE          |
         FLAG_IMPL_LICENSE_PUBLIC_DOMAIN |
         FLAG_IMPL_MULTIPLY_64_64        |
         FLAG_IMPL_ROTATE,
   $.bits            = 64,
   $.verification_LE = 0xF3C4A9B4,
   $.verification_BE = 0x01E3C68D,
   $.hashfn_native   = HayaHash64<false>,
   $.hashfn_bswap    = HayaHash64<true>
 );
