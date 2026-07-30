// hayahash64 - small, fast, portable 64-bit hash function.
//
// Successor experiment to ChibiHash v1/v2, built around four ideas:
//
//  1. The bulk loop should be latency-bound on nothing longer than
//     add -> mul (~4 cycles). ChibiHash v2's cross-lane add makes its
//     loop-carried chain ~5 cycles per 8-byte stripe over 4 lanes.
//     hayahash uses 8 independent lanes over 64-byte blocks instead,
//     with no cross-lane ALU work on the loop-carried path.
//  2. Each lane absorbs t = w + rotl(w_prev, 27), where w_prev is the
//     previous stripe (chained across lanes, blocks, and the bulk/mid
//     loop boundary). This gives two guarantees:
//       - The absorb sequence is injective: at the first stripe where
//         two inputs differ, w_prev is still equal, so t differs.
//         No structural collision pattern can zero out every absorb
//         difference (an earlier xor-of-staggered-loads variant failed
//         exactly this way: xor-combining shifted copies of stripes is
//         GF(2)-linear and has a nullspace once absorbs chain).
//       - Multiplication mod 2^64 only spreads bits upward, and bit 63
//         is a fixed point of every odd multiplication, so top-of-word
//         differences cancel too easily on their own. The rotated copy
//         plants every stripe bit at a second, low-reaching position
//         in the *next* lane, where + and rotl commute with neither
//         GF(2) nor mod-2^64 algebra, so the copy can only be
//         cancelled by carry luck, never structurally.
//     The rotation amount matters: difference "ladders" (each stripe
//     flip cancelling the previous stripe's rotated copy with carry
//     luck) walk the bit position by +r per stripe, and if the orbit
//     revisits the top bits at a stripe distance that is a multiple
//     of the lane count, mod-2^64 anchors like 2^62*(K-1) = 0 can
//     terminate a ladder into an exact collision. r = 21 failed
//     (3*21 = 63: back to the top in 3 hops); r = 27 keeps every
//     same-lane revisit far from the top for all practical lengths.
//     After the loops, the final stripe's dangling copy is absorbed
//     into h0 so ladders cannot march off the end of the input.
//     The rotation is applied to the already-loaded previous stripe,
//     off the loop-carried path, and costs one instruction per stripe
//     (cheaper than a second load on wide cores: three loads per
//     16 bytes was the actual throughput limiter of the staggered
//     variant on Apple M1).
//     Tail paths (at most two rounds per hash) use the bijective
//     injection inj(w) = w ^ rotl(w,21) ^ rotl(w,41) instead; each
//     tail absorb feeds its own lane, so per-absorb bijectivity is
//     enough there.
//  3. Seed AND length are premixed into `s`, and all lane states are
//     derived from `s` and shifted copies of the multiplier K, so no
//     big per-lane constants are materialized (a hidden cost of wide
//     states: 4 instructions per 64-bit literal on AArch64). This
//     keeps v2's full-state seeding property and makes the overlapping
//     tail reads collision-safe across lengths by construction.
//  4. Tails read overlapping words from the end of the input
//     (wyhash-style, refined from v2), so there is no byte-at-a-time
//     loop for any length. Inputs of at most 16 bytes take a
//     dedicated path: two independent multiplies (one per loaded
//     word, so neither word ever sits unmultiplied next to a linear
//     seed term) merged into a strong bijective finalizer.
//
// Portability rules kept from ChibiHash: no SIMD, no 128-bit multiply,
// no hardware-specific instructions, no UB, endianness-independent.
//
// This is free and unencumbered software released into the public domain.
// For more information, please refer to <https://unlicense.org/>

#ifndef HAYAHASH_H
#define HAYAHASH_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

// Loads use memcpy (never UB, any alignment) + byte swap on big-endian
// hosts, so results are endianness-independent. Compilers reliably
// turn the memcpy form into a single load instruction.
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define HAYAHASH64__BSWAP 1
#else
#define HAYAHASH64__BSWAP 0
#endif

static inline uint64_t hayahash64__load32le(const uint8_t *p)
{
	uint32_t v;
	memcpy(&v, p, sizeof(v));
#if HAYAHASH64__BSWAP
	v = (v >> 24) | ((v >> 8) & 0xFF00u) |
	    ((v & 0xFF00u) << 8) | (v << 24);
#endif
	return v;
}
static inline uint64_t hayahash64__load64le(const uint8_t *p)
{
	uint64_t v;
	memcpy(&v, p, sizeof(v));
#if HAYAHASH64__BSWAP
	v = ((v & UINT64_C(0x00000000FFFFFFFF)) << 32) | (v >> 32);
	v = ((v & UINT64_C(0x0000FFFF0000FFFF)) << 16) |
	    ((v >> 16) & UINT64_C(0x0000FFFF0000FFFF));
	v = ((v & UINT64_C(0x00FF00FF00FF00FF)) <<  8) |
	    ((v >>  8) & UINT64_C(0x00FF00FF00FF00FF));
#endif
	return v;
}
static inline uint64_t hayahash64__rotl(uint64_t x, int n)
{
	return (x << n) | (x >> (-n & 63));
}

// Multiplier: 2^64 / golden ratio, odd.
#define HAYAHASH64__K  UINT64_C(0x9E3779B97F4A7C15)
// moremur finalizer constants (Pelle Evensen); M1 doubles as the
// second multiplier of the short path.
#define HAYAHASH64__M1 UINT64_C(0x3C79AC492BA7B653)
#define HAYAHASH64__M2 UINT64_C(0x1C69B3F74AC4AE35)

// moremur finalizer (Pelle Evensen), also used by ChibiHash v1.
static inline uint64_t hayahash64__fmix(uint64_t x)
{
	x ^= x >> 27; x *= HAYAHASH64__M1;
	x ^= x >> 33; x *= HAYAHASH64__M2;
	x ^= x >> 27;
	return x;
}

// Bijective stripe injections (any odd number of rotation terms is
// invertible over GF(2)). The short path uses a second injection with
// different rotation amounts for its b word: if both words used the
// same injection, the key maps of the two multiply terms would commute
// with the rotation relating the two seed copies, and a sparse seed
// difference equal to inj(da) together with db = rotl(da, 23) would
// erase the seed from both sides at once (smhasher3 found exactly
// that). With distinct amounts, simultaneous erasure requires dense,
// unreachable key patterns.
static inline uint64_t hayahash64__inj(uint64_t w)
{
	return w ^ hayahash64__rotl(w, 21) ^ hayahash64__rotl(w, 41);
}
static inline uint64_t hayahash64__inj2(uint64_t w)
{
	return w ^ hayahash64__rotl(w, 11) ^ hayahash64__rotl(w, 50);
}
static inline uint64_t hayahash64__injp(const uint8_t *p)
{
	return hayahash64__inj(hayahash64__load64le(p));
}

// Input length (in bytes) at or above which the 8-lane bulk loop kicks in.
#ifndef HAYAHASH64__BULK_MIN
#define HAYAHASH64__BULK_MIN 320
#endif

static inline uint64_t
hayahash64(const void *keyIn, ptrdiff_t len, uint64_t seed)
{
	const uint8_t *p = (const uint8_t *)keyIn;
	ptrdiff_t l = len;
	const uint64_t K = HAYAHASH64__K;
	// Seed & length premix; feeds every path so length extension and
	// overlapping tail reads can never collide across lengths.
	uint64_t s = seed ^ ((uint64_t)len * K);

	if (l <= 16) {
		uint64_t a, b;
		if (l >= 8) {
			a = hayahash64__load64le(p);
			b = hayahash64__load64le(p + l - 8);
		} else if (l >= 4) {
			a = hayahash64__load32le(p);
			b = hayahash64__load32le(p + l - 4);
		} else if (l > 0) {
			// 1..3 bytes: head, middle, tail (ChibiHash v2 trick).
			a = p[0];
			b = ((uint64_t)p[l >> 1] << 8) | ((uint64_t)p[l - 1] << 16);
		} else {
			a = 0; b = 0;
		}
		// Two independent multiplies, then a strong finalizer.
		// Each word is inj()-spread before its multiply so top-bit
		// differences get low copies and the multiply diff window
		// is always wide; without this, differences confined to
		// the top ~24 bits face only 24 bits of carry luck. The
		// seed is multiplied on both sides (good seed BIC), and
		// erasing a seed copy with key bits requires the dense
		// pattern inj^-1(seed diff) on both sides at once: sparse
		// key differences can never reach either copy.
		uint64_t x = (hayahash64__inj(a) ^ s ^ K) * K;
		uint64_t y = (hayahash64__inj2(b) ^ hayahash64__rotl(s, 23) ^
		              (K >> 19)) * HAYAHASH64__M1;
		return hayahash64__fmix(hayahash64__rotl(x, 27) ^ y);
	}

	uint64_t h0 = s ^ K;
	uint64_t h1 = hayahash64__rotl(s, 17) + (K << 21);
	uint64_t h2 = hayahash64__rotl(s, 34) ^ (K >> 13);
	uint64_t h3 = hayahash64__rotl(s, 51) + (K << 42);
	uint64_t w, wp = 0;

	if (l >= HAYAHASH64__BULK_MIN) {
		uint64_t h4 = s + (K >> 27);
		uint64_t h5 = hayahash64__rotl(s, 13) ^ (K << 9);
		uint64_t h6 = hayahash64__rotl(s, 26) + (K >> 40);
		uint64_t h7 = hayahash64__rotl(s, 39) ^ (K << 30);
		do {
			w = hayahash64__load64le(p +  0);
			h0 = (h0 + (w + hayahash64__rotl(wp, 27))) * K;
			wp = w;
			w = hayahash64__load64le(p +  8);
			h1 = (h1 + (w + hayahash64__rotl(wp, 27))) * K;
			wp = w;
			w = hayahash64__load64le(p + 16);
			h2 = (h2 + (w + hayahash64__rotl(wp, 27))) * K;
			wp = w;
			w = hayahash64__load64le(p + 24);
			h3 = (h3 + (w + hayahash64__rotl(wp, 27))) * K;
			wp = w;
			w = hayahash64__load64le(p + 32);
			h4 = (h4 + (w + hayahash64__rotl(wp, 27))) * K;
			wp = w;
			w = hayahash64__load64le(p + 40);
			h5 = (h5 + (w + hayahash64__rotl(wp, 27))) * K;
			wp = w;
			w = hayahash64__load64le(p + 48);
			h6 = (h6 + (w + hayahash64__rotl(wp, 27))) * K;
			wp = w;
			w = hayahash64__load64le(p + 56);
			h7 = (h7 + (w + hayahash64__rotl(wp, 27))) * K;
			wp = w;
			p += 64; l -= 64;
		} while (l >= 64);
		// Fold the upper lanes in with xor + multiply: xor merges
		// of multiply spreads can only cancel by carry-pattern
		// luck, never exactly (unlike an additive fold).
		h0 = (h0 ^ hayahash64__rotl(h4, 11)) * K;
		h1 = (h1 ^ hayahash64__rotl(h5, 19)) * K;
		h2 = (h2 ^ hayahash64__rotl(h6, 31)) * K;
		h3 = (h3 ^ hayahash64__rotl(h7, 47)) * K;
	}

	// Mid loop: same absorb as the bulk loop, 4 lanes over 32-byte
	// blocks; wp chains in from the bulk loop.
	for (; l >= 32; l -= 32, p += 32) {
		w = hayahash64__load64le(p +  0);
		h0 = (h0 + (w + hayahash64__rotl(wp, 27))) * K;
		wp = w;
		w = hayahash64__load64le(p +  8);
		h1 = (h1 + (w + hayahash64__rotl(wp, 27))) * K;
		wp = w;
		w = hayahash64__load64le(p + 16);
		h2 = (h2 + (w + hayahash64__rotl(wp, 27))) * K;
		wp = w;
		w = hayahash64__load64le(p + 24);
		h3 = (h3 + (w + hayahash64__rotl(wp, 27))) * K;
		wp = w;
	}

	// Absorb the final loop stripe's dangling rotated copy. Without
	// this wall, difference "ladders" (each stripe flip cancelling
	// the previous stripe's rotated copy with carry luck) can march
	// off the end of the loops and vanish; smhasher3 found exact
	// collisions at multiple-of-32 lengths this way. h0 is the lane
	// the copy would have landed in anyway.
	h0 += hayahash64__rotl(wp, 27);

	// 0..31 bytes left.
	if (l > 16) {
		h0 = (h0 + hayahash64__injp(p + 0)) * K;
		h1 = (h1 + hayahash64__injp(p + 8)) * K;
		p += 16; l -= 16;
	}
	// 0..16 bytes left; total length > 16, so reading the last 16
	// input bytes (overlapping already-hashed data) is always valid.
	// Length is already folded into every lane via `s`.
	if (l > 0) {
		h2 = (h2 + hayahash64__injp(p + l - 16)) * K;
		h3 = (h3 + hayahash64__injp(p + l -  8)) * K;
	}

	// Fold rotations must not be the additive inverse (mod 64) of the
	// absorb copy rotation 27: with rotl(h3, 37) here, a lane's raw
	// stripe difference and the next lane's rotated copy re-aligned
	// exactly in the fold and could xor-cancel with carry luck.
	uint64_t t0 = (h0 ^ hayahash64__rotl(h1, 13)) * K;
	uint64_t t1 = (h2 ^ hayahash64__rotl(h3, 33)) * K;
	return hayahash64__fmix(s ^ t0 ^ hayahash64__rotl(t1, 29));
}

#endif // HAYAHASH_H
