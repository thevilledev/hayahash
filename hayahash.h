// hayahash64 - small, fast, portable 64-bit hash function.
//
// An experiment in how fast a strictly portable 64-bit hash can be,
// built around four ideas:
//
//  1. The bulk loop should be latency-bound on nothing longer than
//     add -> mul (~4 cycles), so hayahash uses 8 independent lanes
//     over 64-byte blocks with no cross-lane ALU work on the
//     loop-carried path. For scale, ChibiHash v2's cross-lane add
//     makes its loop-carried chain ~5 cycles per 8-byte stripe over
//     4 lanes.
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
//     gives full-state seeding and makes the overlapping tail reads
//     collision-safe across lengths by construction.
//  4. Tails read overlapping words from the end of the input
//     (wyhash-style), so there is no byte-at-a-time loop for any
//     length. Inputs of at most 16 bytes take a dedicated path: two
//     independent multiplies (one per loaded word, so neither word
//     ever sits unmultiplied next to a linear seed term) merged into
//     a strong bijective finalizer.
//
// Portability rules: no SIMD, no 128-bit multiply, no
// hardware-specific instructions, no UB, endianness-independent.
//
// This is free and unencumbered software released into the public domain.
// For more information, please refer to <https://unlicense.org/>

#ifndef HAYAHASH_H
#define HAYAHASH_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

// Loads use memcpy (never UB, any alignment) + byte swap on known
// big-endian hosts. If a compiler exposes no byte-order macro, fall
// back to explicit byte assembly rather than silently assuming little
// endian. Compilers turn the normal memcpy form into one load.
#if defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) && \
    __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define HAYAHASH64_INTERNAL_ENDIAN 1
#elif defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && \
      __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define HAYAHASH64_INTERNAL_ENDIAN 0
#elif defined(_WIN32)
#define HAYAHASH64_INTERNAL_ENDIAN 0
#else
#define HAYAHASH64_INTERNAL_ENDIAN 2
#endif

static inline uint64_t hayahash64_internal_load32le(const uint8_t *p)
{
#if HAYAHASH64_INTERNAL_ENDIAN == 2
	return (uint64_t)p[0]       | ((uint64_t)p[1] <<  8) |
	       ((uint64_t)p[2] << 16) | ((uint64_t)p[3] << 24);
#else
	uint32_t v;
	memcpy(&v, p, sizeof(v));
#if HAYAHASH64_INTERNAL_ENDIAN == 1
	v = (v >> 24) | ((v >> 8) & 0xFF00u) |
	    ((v & 0xFF00u) << 8) | (v << 24);
#endif
	return v;
#endif
}
static inline uint64_t hayahash64_internal_load64le(const uint8_t *p)
{
#if HAYAHASH64_INTERNAL_ENDIAN == 2
	return (uint64_t)p[0]        | ((uint64_t)p[1] <<  8) |
	       ((uint64_t)p[2] << 16) | ((uint64_t)p[3] << 24) |
	       ((uint64_t)p[4] << 32) | ((uint64_t)p[5] << 40) |
	       ((uint64_t)p[6] << 48) | ((uint64_t)p[7] << 56);
#else
	uint64_t v;
	memcpy(&v, p, sizeof(v));
#if HAYAHASH64_INTERNAL_ENDIAN == 1
	v = ((v & UINT64_C(0x00000000FFFFFFFF)) << 32) | (v >> 32);
	v = ((v & UINT64_C(0x0000FFFF0000FFFF)) << 16) |
	    ((v >> 16) & UINT64_C(0x0000FFFF0000FFFF));
	v = ((v & UINT64_C(0x00FF00FF00FF00FF)) <<  8) |
	    ((v >>  8) & UINT64_C(0x00FF00FF00FF00FF));
#endif
	return v;
#endif
}
static inline uint64_t hayahash64_internal_rotl(uint64_t x, int n)
{
	return (x << n) | (x >> (-n & 63));
}

// Keep an already-computed product opaque. The original reason was to
// stop clang distributing a following rotate into two independent
// multiplies; neither Apple clang 21 nor stock clang 22 still applies
// that transform, but the barriers now earn their keep another way and
// removal was re-measured as a loss on both. On the M1 the guarded
// schedule is simply faster (removal costs 2..15% independent
// throughput at 17..319 bytes and 2..4% of 320..1024-byte
// throughput despite fewer instructions). On Zen 5, clang 22 without
// the guards auto-vectorizes the mid/tail paths, which lifts
// independent throughput 15..50% but collapses seed-chained latency
// 20..42%; the barrier is what keeps the latency chain scalar and
// short. GCC never needed them (third pass).
#if defined(__clang__)
#define HAYAHASH64_INTERNAL_COMPILER_GUARD(v) __asm__("" : "+r" (v))
#else
#define HAYAHASH64_INTERNAL_COMPILER_GUARD(v) ((void)0)
#endif

// The bulk (>= 320 byte) path lives in its own non-inlined function:
// its unrolled loop wants more registers than the short paths can
// afford. Inlined into hayahash64, those spills become unconditional
// prologue stores that serialize back-to-back short hashes through
// the same stack slots (measured ~2x small-key throughput loss).
// `unused` keeps -Wunused-function quiet in translation units that
// include the header without calling hayahash64 (the function is
// plain static, not static inline, so it is not exempt by default),
// and lets the wasm build drop the function silently (it dispatches
// to the inline fall-through below instead).
#if defined(__GNUC__) || defined(__clang__)
#define HAYAHASH64_INTERNAL_NOINLINE __attribute__((noinline, unused))
#elif defined(_MSC_VER)
#define HAYAHASH64_INTERNAL_NOINLINE __declspec(noinline)
#else
#define HAYAHASH64_INTERNAL_NOINLINE
#endif

// Dispatch shape for 17..319-byte keys: straight-line length tiers,
// plus 64-byte mid-loop rounds so the 256..319-byte keys left in the
// loop recover the tier dispatch compares. This is a GCC shape: GCC
// shrink-wraps the tier chain and wins with it on every measured
// workload (Zen 5: compact costs 3..13% fixed-size independent
// throughput and 2..10% on mixed-size runs). Both measured clangs
// lose with the chain live once per-hash branch targets are
// unpredictable: mixed-size workloads on Apple clang 21 (M1) run
// 3..13% faster independent and up to 9% faster chained under the
// compact dispatch (which gives back 6..11% at fixed 32..192-byte
// sizes), and stock clang 22 on Zen 5 collapses chained latency
// 9..20% when forced wide. A general-purpose default cannot assume
// single-band input sizes, so clang gets the compact dispatch.
// Jump-table dispatch over the same tier bodies (switch and computed
// goto) was measured and rejected on every compiler/arch pair: one
// indirect branch predicts worse than the short compare chain on
// mixed sizes (GCC/Zen 5 -16..-35%, Apple clang/M1 -4..-15%).
#if (defined(__aarch64__) || defined(__x86_64__)) && !defined(__clang__)
#define HAYAHASH64_INTERNAL_TIERS 1
#else
#define HAYAHASH64_INTERNAL_TIERS 0
#endif

static inline uint64_t hayahash64_internal_rotl_product(uint64_t x, int n)
{
	HAYAHASH64_INTERNAL_COMPILER_GUARD(x);
	return hayahash64_internal_rotl(x, n);
}

// Multiplier: 2^64 / golden ratio, odd.
#define HAYAHASH64_INTERNAL_K  UINT64_C(0x9E3779B97F4A7C15)
// moremur finalizer constants (Pelle Evensen); M1 doubles as the
// second multiplier of the short path.
#define HAYAHASH64_INTERNAL_M1 UINT64_C(0x3C79AC492BA7B653)
#define HAYAHASH64_INTERNAL_M2 UINT64_C(0x1C69B3F74AC4AE35)

// moremur finalizer (Pelle Evensen), also used by ChibiHash v1.
static inline uint64_t hayahash64_internal_fmix(uint64_t x)
{
	x ^= x >> 27; x *= HAYAHASH64_INTERNAL_M1;
	x ^= x >> 33; x *= HAYAHASH64_INTERNAL_M2;
	x ^= x >> 27;
	return x;
}

// The long path has already passed every input byte through a
// multiply and a non-linear lane merge. One multiply is enough for
// its final avalanche; reusing K also avoids a new 64-bit literal.
static inline uint64_t hayahash64_internal_long_fmix(uint64_t x, uint64_t K)
{
	x ^= x >> 37;
	x *= K;
	x ^= x >> 32;
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
static inline uint64_t hayahash64_internal_inj(uint64_t w)
{
	uint64_t x = w ^ hayahash64_internal_rotl(w, 21);
	HAYAHASH64_INTERNAL_COMPILER_GUARD(x);
	return x ^ hayahash64_internal_rotl(w, 41);
}
static inline uint64_t hayahash64_internal_inj2(uint64_t w)
{
	uint64_t x = w ^ hayahash64_internal_rotl(w, 11);
	HAYAHASH64_INTERNAL_COMPILER_GUARD(x);
	return x ^ hayahash64_internal_rotl(w, 50);
}
static inline uint64_t hayahash64_internal_injp(const uint8_t *p)
{
	return hayahash64_internal_inj(hayahash64_internal_load64le(p));
}

// Fixed algorithm parameter: keeping the 4-lane path below 320 bytes
// bounds it to fewer than the rotation's 64 stripes. Changing this
// boundary changes the digest and can invalidate that structural bound.
enum { hayahash64_internal_bulk_min = 320 };

// One 64-byte bulk block over the eight lanes h0..h7 plus the
// per-block raw-word checkpoint into h0. The checkpoint stops the
// known 64-stripe rotation-orbit ladder from hiding its difference
// until it returns to the same lane; AArch64 emits MADD for it.
// A macro (expanded twice below) so the bulk loop can be unrolled
// two blocks deep without spelling the stripes three times.
//
// Two spellings of the same dataflow. GCC targeting AVX-512DQ gets
// the middle four lanes as a tiny local array updated through a
// countable 4-iteration loop, re-loading the absorb and rotate-source
// words straight from the buffer (pure loads of the same bytes, so
// the digest cannot change). That is the shape clang discovers by
// itself in the plain spelling: lanes h2..h5 become one vector whose
// rotate source and absorb input are two overlapping vector loads
// (vprolq + vpaddq from memory, no shuffles), with h0/h1/h6/h7 kept
// scalar on the integer pipes because they carry the wp chain and
// the checkpoint. GCC cannot see it on its own: its SLP vectorizer
// only seeds from groups of adjacent stores, and register-resident
// lanes never store (spelled straight-line, SRA scalarizes the array
// back and the seed disappears — measured, not conjecture). The
// array + loop force real stores; GCC 16 then emits the same 4+4
// split and sustained bulk goes from 35 to 62 GB/s on Zen 5, with
// the 320..512-byte transition band up 30..46% and every sub-320
// path bit-identical in code as well as output.
//
// The win is a Zen 4/5 property, not an AVX-512 property: the loop
// vectorizes to 32-byte vpmullq, which those cores run as one uop
// but Skylake-X-class servers microcode (3 uops, ~15-cycle latency,
// landed on each lane's xor-add-mul chain). Gated on AVX-512DQ
// alone, GCC 13 and 14 on Cascade Lake both vectorize anyway and
// lose ~30% sustained bulk and ~35% chained latency from 320 bytes
// up versus the plain spelling. So the gate also requires a Zen 4/5
// target; everywhere else (generic x86-64-v4 included) this
// preprocesses to the plain spelling token for token.
#if defined(__x86_64__) && defined(__GNUC__) && !defined(__clang__) && \
    defined(__AVX512DQ__) && (defined(__znver4__) || defined(__znver5__))
#define HAYAHASH64_INTERNAL_VECGCC 1
#else
#define HAYAHASH64_INTERNAL_VECGCC 0
#endif

// clang auto-vectorizes the middle four bulk lanes to 32-byte
// vpmullq wherever AVX-512DQ is available. On Zen 4/5 vpmullq is a
// single uop and the vector loop is a large win; Skylake-X-class
// servers microcode it (3 uops, ~15-cycle latency on each lane's
// xor-add-mul chain) and the same transform measured ~30% slower
// sustained bulk and ~35% worse chained latency from 320 bytes up
// on Cascade Lake (clang 18). On non-Zen AVX-512 targets these
// barriers pin the middle lanes to scalar registers at each block
// boundary, which denies the vectorizer its seed; they cost no
// instructions and the digest is unchanged.
#if defined(__clang__) && defined(__x86_64__) && defined(__AVX512DQ__) && \
    !defined(__znver4__) && !defined(__znver5__)
#define HAYAHASH64_INTERNAL_BULK_LANE_GUARD(v) \
	HAYAHASH64_INTERNAL_COMPILER_GUARD(v)
#else
#define HAYAHASH64_INTERNAL_BULK_LANE_GUARD(v) ((void)0)
#endif

#if HAYAHASH64_INTERNAL_VECGCC
#define HAYAHASH64_INTERNAL_BULK_BLOCK(q) \
	do { \
		w = hayahash64_internal_load64le((q) +  0); \
		h0 = (h0 ^ (w + hayahash64_internal_rotl(wp, 27))) * K; \
		h1 = (h1 ^ (hayahash64_internal_load64le((q) +  8) + \
		    hayahash64_internal_rotl(w, 27))) * K; \
		for (int hh_i = 0; hh_i < 4; hh_i++) \
			hv[hh_i] = (hv[hh_i] ^ \
			    (hayahash64_internal_load64le((q) + 16 + 8 * hh_i) + \
			     hayahash64_internal_rotl( \
			        hayahash64_internal_load64le((q) + 8 + 8 * hh_i), 27))) * K; \
		h6 = (h6 ^ (hayahash64_internal_load64le((q) + 48) + \
		    hayahash64_internal_rotl(hayahash64_internal_load64le((q) + 40), 27))) * K; \
		h7 = (h7 ^ (hayahash64_internal_load64le((q) + 56) + \
		    hayahash64_internal_rotl(hayahash64_internal_load64le((q) + 48), 27))) * K; \
		wp = hayahash64_internal_load64le((q) + 56); \
		h0 += wp; \
	} while (0)
#else
#define HAYAHASH64_INTERNAL_BULK_BLOCK(q) \
	do { \
		w = hayahash64_internal_load64le((q) +  0); \
		h0 = (h0 ^ (w + hayahash64_internal_rotl(wp, 27))) * K; \
		wp = w; \
		w = hayahash64_internal_load64le((q) +  8); \
		h1 = (h1 ^ (w + hayahash64_internal_rotl(wp, 27))) * K; \
		wp = w; \
		w = hayahash64_internal_load64le((q) + 16); \
		h2 = (h2 ^ (w + hayahash64_internal_rotl(wp, 27))) * K; \
		wp = w; \
		w = hayahash64_internal_load64le((q) + 24); \
		h3 = (h3 ^ (w + hayahash64_internal_rotl(wp, 27))) * K; \
		wp = w; \
		w = hayahash64_internal_load64le((q) + 32); \
		h4 = (h4 ^ (w + hayahash64_internal_rotl(wp, 27))) * K; \
		wp = w; \
		w = hayahash64_internal_load64le((q) + 40); \
		h5 = (h5 ^ (w + hayahash64_internal_rotl(wp, 27))) * K; \
		wp = w; \
		w = hayahash64_internal_load64le((q) + 48); \
		h6 = (h6 ^ (w + hayahash64_internal_rotl(wp, 27))) * K; \
		wp = w; \
		w = hayahash64_internal_load64le((q) + 56); \
		h7 = (h7 ^ (w + hayahash64_internal_rotl(wp, 27))) * K; \
		wp = w; \
		h0 += wp; \
		HAYAHASH64_INTERNAL_BULK_LANE_GUARD(h2); \
		HAYAHASH64_INTERNAL_BULK_LANE_GUARD(h3); \
		HAYAHASH64_INTERNAL_BULK_LANE_GUARD(h4); \
		HAYAHASH64_INTERNAL_BULK_LANE_GUARD(h5); \
	} while (0)
#endif

// Long-input path, len >= hayahash64_internal_bulk_min. Bit-identical
// to running the former inline bulk loop plus the shared mid/tail
// code: the loop is merely unrolled two blocks deep (same block
// sequence), and the mid loop can run at most once here (l < 64
// after the bulk loop), so it is spelled as an if. The signature
// mirrors hayahash64 on purpose, s premix included: the call then
// compiles to a zero-move tail call, leaving the caller's register
// allocation completely unconstrained. Passing the premixed s
// instead pinned it to an argument register and cost a measured
// 2..5% on 17..160-byte keys (mangled caller address arithmetic or
// per-key entry register shuffling); recomputing it here is two
// instructions amortized over at least 320 bytes.
static HAYAHASH64_INTERNAL_NOINLINE uint64_t
hayahash64_internal_long(const void *keyIn, ptrdiff_t len, uint64_t seed)
{
	// Restate the dispatch invariant: the range information does not
	// survive the noinline boundary, and without it GCC cannot prove
	// the bulk loop runs at least four blocks. It then keeps a live
	// remaining-length computation in the loop exit (two dependent
	// ALU ops per block) and guards the post-loop remainder with a
	// cmov chain for the impossible zero-trip case, a measured
	// 1..3.5% of 320-byte-and-up throughput on Zen 5.
#if defined(__GNUC__) || defined(__clang__)
	if (len < (ptrdiff_t)hayahash64_internal_bulk_min)
		__builtin_unreachable();
#elif defined(_MSC_VER)
	__assume(len >= (ptrdiff_t)hayahash64_internal_bulk_min);
#endif
	const uint8_t *p = (const uint8_t *)keyIn;
	ptrdiff_t l = len;
	uint64_t K = HAYAHASH64_INTERNAL_K;
#if defined(__aarch64__) && (defined(__GNUC__) || defined(__clang__))
	HAYAHASH64_INTERNAL_COMPILER_GUARD(K);
#endif
	uint64_t s = seed ^ ((uint64_t)len * K);
	uint64_t h0 = s ^ K;
	uint64_t h1 = hayahash64_internal_rotl(s, 17) + (K << 21);
	uint64_t h2 = hayahash64_internal_rotl(s, 34) ^ (K >> 13);
	uint64_t h3 = hayahash64_internal_rotl(s, 51) + (K << 42);
	uint64_t h4 = s + (K >> 27);
	uint64_t h5 = hayahash64_internal_rotl(s, 13) ^ (K << 9);
	uint64_t h6 = hayahash64_internal_rotl(s, 26) + (K >> 40);
	uint64_t h7 = hayahash64_internal_rotl(s, 39) ^ (K << 30);
	uint64_t w, wp = 0;
#if defined(__x86_64__) && defined(__GNUC__) && !defined(__clang__)
	// Pin K after the IVs so their shifted-K constants still fold
	// to literals; from here on the exit path reuses one register
	// instead of rematerializing the 10-byte movabs four times.
	__asm__("" : "+r" (K));
#endif

#if defined(__aarch64__) || defined(__x86_64__)
	// Two blocks per iteration halves loop-control and pointer
	// maintenance per block; entry guarantees l >= 320 so the first
	// do-while test is safe. Measured +4..5% bulk on Apple M1 and
	// +4..10% (GCC 16) to +10..48% (clang 21) on Zen 5, where fewer
	// loop exits also let independent hashes overlap much deeper.
	// Rosetta 2 predicted an x86-64 spill penalty that native
	// silicon does not reproduce.
	//
	// The loop runs on a precomputed end pointer with the post-loop
	// remainder (l & 127) hoisted to entry, where there is ILP
	// slack. Spelled with `l -= 128` and a length test, GCC turned
	// the exit into a counted loop anyway, but then had to keep the
	// length, base pointer, and trip count live in stack slots and
	// rebuild the remainder after the loop; the pointer-compare
	// spelling removes the spills and the rebuild chain.
#if HAYAHASH64_INTERNAL_VECGCC
	// The vectorized middle lanes; folded back after the last block.
	uint64_t hv[4] = { h2, h3, h4, h5 };
#endif
	const uint8_t *pe = p + ((size_t)l & ~(size_t)127);
	l &= 127;
	do {
		HAYAHASH64_INTERNAL_BULK_BLOCK(p);
		HAYAHASH64_INTERNAL_BULK_BLOCK(p + 64);
		p += 128;
	} while (p != pe);
	if (l >= 64) {
		HAYAHASH64_INTERNAL_BULK_BLOCK(p);
		p += 64; l -= 64;
	}
#else
	// Untested targets keep one block per iteration.
	do {
		HAYAHASH64_INTERNAL_BULK_BLOCK(p);
		p += 64; l -= 64;
	} while (l >= 64);
#endif
#if HAYAHASH64_INTERNAL_VECGCC
	h2 = hv[0]; h3 = hv[1]; h4 = hv[2]; h5 = hv[3];
#endif
	// Fold the upper lanes in with xor + multiply: xor merges
	// of multiply spreads can only cancel by carry-pattern
	// luck, never exactly (unlike an additive fold).
	h0 = (h0 ^ hayahash64_internal_rotl_product(h4, 11)) * K;
	h1 = (h1 ^ hayahash64_internal_rotl_product(h5, 19)) * K;
	h2 = (h2 ^ hayahash64_internal_rotl_product(h6, 31)) * K;
	h3 = (h3 ^ hayahash64_internal_rotl_product(h7, 47)) * K;

	// 0..63 bytes left: at most one mid round; wp chains in from
	// the bulk loop. Bit-test spellings of these remainder checks
	// (l & 32, and reading the tail length as len & 31 without the
	// serial decrements) measured +0.5..1% under GCC 16 in the
	// 320..383 band but cost stock clang 22 a quarter of its bulk
	// throughput, so the plain comparisons stay.
	if (l >= 32) {
		w = hayahash64_internal_load64le(p +  0);
		h0 = (h0 ^ (w + hayahash64_internal_rotl(wp, 27))) * K;
		wp = w;
		w = hayahash64_internal_load64le(p +  8);
		h1 = (h1 ^ (w + hayahash64_internal_rotl(wp, 27))) * K;
		wp = w;
		w = hayahash64_internal_load64le(p + 16);
		h2 = (h2 ^ (w + hayahash64_internal_rotl(wp, 27))) * K;
		wp = w;
		w = hayahash64_internal_load64le(p + 24);
		h3 = (h3 ^ (w + hayahash64_internal_rotl(wp, 27))) * K;
		wp = w;
		p += 32; l -= 32;
	}

	// Absorb the final stripe's dangling rotated copy; see the wall
	// comment in hayahash64.
	h0 += hayahash64_internal_rotl(wp, 27);

	if (l > 16) {
		h0 = (h0 + hayahash64_internal_injp(p + 0)) * K;
		h1 = (h1 + hayahash64_internal_injp(p + 8)) * K;
	}
	if (l > 0) {
		h2 = (h2 + hayahash64_internal_injp(p + l - 16)) * K;
		h3 = (h3 + hayahash64_internal_injp(p + l -  8)) * K;
	}

	uint64_t t0 = (h0 ^ hayahash64_internal_rotl(h1, 13)) * K;
	uint64_t t1 = (h2 ^ hayahash64_internal_rotl(h3, 33)) * K;
	uint64_t x = s ^ t0 ^ hayahash64_internal_rotl_product(t1, 29);
	return hayahash64_internal_long_fmix(x, K);
}

// `len` must be non-negative, and `keyIn` must point to at least `len`
// readable bytes (it may be null when len is zero).
static inline uint64_t
hayahash64(const void *keyIn, ptrdiff_t len, uint64_t seed)
{
	const uint8_t *p = (const uint8_t *)keyIn;
	ptrdiff_t l = len;
	uint64_t K = HAYAHASH64_INTERNAL_K;
	// Seed & length premix; feeds every path so length extension and
	// overlapping tail reads can never collide across lengths.
	uint64_t s = seed ^ ((uint64_t)len * K);

	if (l <= 16) {
		uint64_t a, b;
		if (l >= 8) {
			a = hayahash64_internal_load64le(p);
			b = hayahash64_internal_load64le(p + l - 8);
		} else if (l >= 4) {
			a = hayahash64_internal_load32le(p);
			b = hayahash64_internal_load32le(p + l - 4);
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
		uint64_t x = (hayahash64_internal_inj(a) ^ s ^ K) * K;
		uint64_t y = (hayahash64_internal_inj2(b) ^ hayahash64_internal_rotl(s, 23) ^
		              (K >> 19)) * HAYAHASH64_INTERNAL_M1;
		return hayahash64_internal_fmix(hayahash64_internal_rotl_product(x, 27) ^ y);
	}

#if !defined(__wasm__)
	if (l >= hayahash64_internal_bulk_min)
		return hayahash64_internal_long(keyIn, len, seed);
#endif

#if defined(__aarch64__) && (defined(__GNUC__) || defined(__clang__))
	HAYAHASH64_INTERNAL_COMPILER_GUARD(K);
#endif
	uint64_t h0 = s ^ K;
	uint64_t h1 = hayahash64_internal_rotl(s, 17) + (K << 21);
	uint64_t h2 = hayahash64_internal_rotl(s, 34) ^ (K >> 13);
	uint64_t h3 = hayahash64_internal_rotl(s, 51) + (K << 42);
	uint64_t w, wp = 0;

#if defined(__wasm__)
	// Wasm has no callee-saved-register pressure to justify the
	// outlined bulk path, and a second function means a second copy
	// of the fold/tail code (+40% module size). Keep the pre-split
	// fall-through here instead: run the bulk loop inline and drop
	// into the shared mid loop and tail below. Bit-identical: same
	// block sequence as hayahash64_internal_long's generic branch,
	// with wp chaining into at most one mid round.
	if (l >= hayahash64_internal_bulk_min) {
		uint64_t h4 = s + (K >> 27);
		uint64_t h5 = hayahash64_internal_rotl(s, 13) ^ (K << 9);
		uint64_t h6 = hayahash64_internal_rotl(s, 26) + (K >> 40);
		uint64_t h7 = hayahash64_internal_rotl(s, 39) ^ (K << 30);
		do {
			HAYAHASH64_INTERNAL_BULK_BLOCK(p);
			p += 64; l -= 64;
		} while (l >= 64);
		h0 = (h0 ^ hayahash64_internal_rotl_product(h4, 11)) * K;
		h1 = (h1 ^ hayahash64_internal_rotl_product(h5, 19)) * K;
		h2 = (h2 ^ hayahash64_internal_rotl_product(h6, 31)) * K;
		h3 = (h3 ^ hayahash64_internal_rotl_product(h7, 47)) * K;
	}
#endif

#if HAYAHASH64_INTERNAL_TIERS
	// Straight-line spelling of the generic 17..31-byte tail below.
	if (l < 32) {
		h0 = (h0 + hayahash64_internal_injp(p + 0)) * K;
		h1 = (h1 + hayahash64_internal_injp(p + 8)) * K;
		h2 = (h2 + hayahash64_internal_injp(p + l - 16)) * K;
		h3 = (h3 + hayahash64_internal_injp(p + l - 8)) * K;
		uint64_t t0 =
			(h0 ^ hayahash64_internal_rotl_product(h1, 13)) * K;
		uint64_t t1 =
			(h2 ^ hayahash64_internal_rotl_product(h3, 33)) * K;
		return hayahash64_internal_long_fmix(
			s ^ t0 ^ hayahash64_internal_rotl_product(t1, 29), K);
	}
	// Straight-line spelling of one mid round plus the generic tail
	// below for 32..63-byte keys: same loads, absorbs, wall, and
	// folds, minus the loop and pointer maintenance. The first
	// absorb folds in wp = 0; the tail conditions test the original
	// length (l > 48 and l > 32 correspond to the generic path's
	// post-round l > 16 and l > 0).
	if (l < 64) {
		uint64_t w0 = hayahash64_internal_load64le(p +  0);
		uint64_t w1 = hayahash64_internal_load64le(p +  8);
		uint64_t w2 = hayahash64_internal_load64le(p + 16);
		uint64_t w3 = hayahash64_internal_load64le(p + 24);
		h0 = (h0 ^ w0) * K;
		h1 = (h1 ^ (w1 + hayahash64_internal_rotl(w0, 27))) * K;
		h2 = (h2 ^ (w2 + hayahash64_internal_rotl(w1, 27))) * K;
		h3 = (h3 ^ (w3 + hayahash64_internal_rotl(w2, 27))) * K;
		h0 += hayahash64_internal_rotl(w3, 27);
		if (l > 48) {
			h0 = (h0 + hayahash64_internal_injp(p + 32)) * K;
			h1 = (h1 + hayahash64_internal_injp(p + 40)) * K;
		}
		if (l > 32) {
			h2 = (h2 + hayahash64_internal_injp(p + l - 16)) * K;
			h3 = (h3 + hayahash64_internal_injp(p + l -  8)) * K;
		}
		uint64_t t0 =
			(h0 ^ hayahash64_internal_rotl_product(h1, 13)) * K;
		uint64_t t1 =
			(h2 ^ hayahash64_internal_rotl_product(h3, 33)) * K;
		return hayahash64_internal_long_fmix(
			s ^ t0 ^ hayahash64_internal_rotl_product(t1, 29), K);
	}
	// Straight-line 64..127-byte keys: two shared mid rounds, then
	// either the generic tail directly (64..95) or one more round
	// first (96..127). Same loads, absorbs, wall, and folds as the
	// generic path, minus loop and pointer maintenance.
	if (l < 128) {
		uint64_t w0 = hayahash64_internal_load64le(p +  0);
		uint64_t w1 = hayahash64_internal_load64le(p +  8);
		uint64_t w2 = hayahash64_internal_load64le(p + 16);
		uint64_t w3 = hayahash64_internal_load64le(p + 24);
		h0 = (h0 ^ w0) * K;
		h1 = (h1 ^ (w1 + hayahash64_internal_rotl(w0, 27))) * K;
		h2 = (h2 ^ (w2 + hayahash64_internal_rotl(w1, 27))) * K;
		h3 = (h3 ^ (w3 + hayahash64_internal_rotl(w2, 27))) * K;
		uint64_t w4 = hayahash64_internal_load64le(p + 32);
		uint64_t w5 = hayahash64_internal_load64le(p + 40);
		uint64_t w6 = hayahash64_internal_load64le(p + 48);
		uint64_t w7 = hayahash64_internal_load64le(p + 56);
		h0 = (h0 ^ (w4 + hayahash64_internal_rotl(w3, 27))) * K;
		h1 = (h1 ^ (w5 + hayahash64_internal_rotl(w4, 27))) * K;
		h2 = (h2 ^ (w6 + hayahash64_internal_rotl(w5, 27))) * K;
		h3 = (h3 ^ (w7 + hayahash64_internal_rotl(w6, 27))) * K;
		if (l >= 96) {
			uint64_t w8  = hayahash64_internal_load64le(p + 64);
			uint64_t w9  = hayahash64_internal_load64le(p + 72);
			uint64_t w10 = hayahash64_internal_load64le(p + 80);
			uint64_t w11 = hayahash64_internal_load64le(p + 88);
			h0 = (h0 ^ (w8  + hayahash64_internal_rotl(w7,  27))) * K;
			h1 = (h1 ^ (w9  + hayahash64_internal_rotl(w8,  27))) * K;
			h2 = (h2 ^ (w10 + hayahash64_internal_rotl(w9,  27))) * K;
			h3 = (h3 ^ (w11 + hayahash64_internal_rotl(w10, 27))) * K;
			h0 += hayahash64_internal_rotl(w11, 27);
			if (l > 112) {
				h0 = (h0 + hayahash64_internal_injp(p +  96)) * K;
				h1 = (h1 + hayahash64_internal_injp(p + 104)) * K;
			}
			if (l > 96) {
				h2 = (h2 + hayahash64_internal_injp(p + l - 16)) * K;
				h3 = (h3 + hayahash64_internal_injp(p + l -  8)) * K;
			}
		} else {
			h0 += hayahash64_internal_rotl(w7, 27);
			if (l > 80) {
				h0 = (h0 + hayahash64_internal_injp(p + 64)) * K;
				h1 = (h1 + hayahash64_internal_injp(p + 72)) * K;
			}
			if (l > 64) {
				h2 = (h2 + hayahash64_internal_injp(p + l - 16)) * K;
				h3 = (h3 + hayahash64_internal_injp(p + l -  8)) * K;
			}
		}
		uint64_t t0 =
			(h0 ^ hayahash64_internal_rotl_product(h1, 13)) * K;
		uint64_t t1 =
			(h2 ^ hayahash64_internal_rotl_product(h3, 33)) * K;
		return hayahash64_internal_long_fmix(
			s ^ t0 ^ hayahash64_internal_rotl_product(t1, 29), K);
	}
	// Straight-line 128..191-byte keys: four shared mid rounds, then
	// either the generic tail directly (128..159) or one more round
	// first (160..191). Same loads, absorbs, wall, and folds as the
	// generic path, minus loop and pointer maintenance.
	if (l < 192) {
		uint64_t w0 = hayahash64_internal_load64le(p +  0);
		uint64_t w1 = hayahash64_internal_load64le(p +  8);
		uint64_t w2 = hayahash64_internal_load64le(p + 16);
		uint64_t w3 = hayahash64_internal_load64le(p + 24);
		h0 = (h0 ^ w0) * K;
		h1 = (h1 ^ (w1 + hayahash64_internal_rotl(w0, 27))) * K;
		h2 = (h2 ^ (w2 + hayahash64_internal_rotl(w1, 27))) * K;
		h3 = (h3 ^ (w3 + hayahash64_internal_rotl(w2, 27))) * K;
		uint64_t w4 = hayahash64_internal_load64le(p + 32);
		uint64_t w5 = hayahash64_internal_load64le(p + 40);
		uint64_t w6 = hayahash64_internal_load64le(p + 48);
		uint64_t w7 = hayahash64_internal_load64le(p + 56);
		h0 = (h0 ^ (w4 + hayahash64_internal_rotl(w3, 27))) * K;
		h1 = (h1 ^ (w5 + hayahash64_internal_rotl(w4, 27))) * K;
		h2 = (h2 ^ (w6 + hayahash64_internal_rotl(w5, 27))) * K;
		h3 = (h3 ^ (w7 + hayahash64_internal_rotl(w6, 27))) * K;
		uint64_t w8  = hayahash64_internal_load64le(p + 64);
		uint64_t w9  = hayahash64_internal_load64le(p + 72);
		uint64_t w10 = hayahash64_internal_load64le(p + 80);
		uint64_t w11 = hayahash64_internal_load64le(p + 88);
		h0 = (h0 ^ (w8  + hayahash64_internal_rotl(w7,  27))) * K;
		h1 = (h1 ^ (w9  + hayahash64_internal_rotl(w8,  27))) * K;
		h2 = (h2 ^ (w10 + hayahash64_internal_rotl(w9,  27))) * K;
		h3 = (h3 ^ (w11 + hayahash64_internal_rotl(w10, 27))) * K;
		uint64_t w12 = hayahash64_internal_load64le(p +  96);
		uint64_t w13 = hayahash64_internal_load64le(p + 104);
		uint64_t w14 = hayahash64_internal_load64le(p + 112);
		uint64_t w15 = hayahash64_internal_load64le(p + 120);
		h0 = (h0 ^ (w12 + hayahash64_internal_rotl(w11, 27))) * K;
		h1 = (h1 ^ (w13 + hayahash64_internal_rotl(w12, 27))) * K;
		h2 = (h2 ^ (w14 + hayahash64_internal_rotl(w13, 27))) * K;
		h3 = (h3 ^ (w15 + hayahash64_internal_rotl(w14, 27))) * K;
		if (l >= 160) {
			uint64_t w16 = hayahash64_internal_load64le(p + 128);
			uint64_t w17 = hayahash64_internal_load64le(p + 136);
			uint64_t w18 = hayahash64_internal_load64le(p + 144);
			uint64_t w19 = hayahash64_internal_load64le(p + 152);
			h0 = (h0 ^ (w16 + hayahash64_internal_rotl(w15, 27))) * K;
			h1 = (h1 ^ (w17 + hayahash64_internal_rotl(w16, 27))) * K;
			h2 = (h2 ^ (w18 + hayahash64_internal_rotl(w17, 27))) * K;
			h3 = (h3 ^ (w19 + hayahash64_internal_rotl(w18, 27))) * K;
			h0 += hayahash64_internal_rotl(w19, 27);
			if (l > 176) {
				h0 = (h0 + hayahash64_internal_injp(p + 160)) * K;
				h1 = (h1 + hayahash64_internal_injp(p + 168)) * K;
			}
			if (l > 160) {
				h2 = (h2 + hayahash64_internal_injp(p + l - 16)) * K;
				h3 = (h3 + hayahash64_internal_injp(p + l -  8)) * K;
			}
		} else {
			h0 += hayahash64_internal_rotl(w15, 27);
			if (l > 144) {
				h0 = (h0 + hayahash64_internal_injp(p + 128)) * K;
				h1 = (h1 + hayahash64_internal_injp(p + 136)) * K;
			}
			if (l > 128) {
				h2 = (h2 + hayahash64_internal_injp(p + l - 16)) * K;
				h3 = (h3 + hayahash64_internal_injp(p + l -  8)) * K;
			}
		}
		uint64_t t0 =
			(h0 ^ hayahash64_internal_rotl_product(h1, 13)) * K;
		uint64_t t1 =
			(h2 ^ hayahash64_internal_rotl_product(h3, 33)) * K;
		return hayahash64_internal_long_fmix(
			s ^ t0 ^ hayahash64_internal_rotl_product(t1, 29), K);
	}
	// Straight-line 192..255-byte keys: six shared mid rounds, then
	// either the generic tail directly (192..223) or one more round
	// first (224..255). Same loads, absorbs, wall, and folds as the
	// generic path, minus loop and pointer maintenance.
	if (l < 256) {
		uint64_t w0 = hayahash64_internal_load64le(p +  0);
		uint64_t w1 = hayahash64_internal_load64le(p +  8);
		uint64_t w2 = hayahash64_internal_load64le(p + 16);
		uint64_t w3 = hayahash64_internal_load64le(p + 24);
		h0 = (h0 ^ w0) * K;
		h1 = (h1 ^ (w1 + hayahash64_internal_rotl(w0, 27))) * K;
		h2 = (h2 ^ (w2 + hayahash64_internal_rotl(w1, 27))) * K;
		h3 = (h3 ^ (w3 + hayahash64_internal_rotl(w2, 27))) * K;
		uint64_t w4 = hayahash64_internal_load64le(p + 32);
		uint64_t w5 = hayahash64_internal_load64le(p + 40);
		uint64_t w6 = hayahash64_internal_load64le(p + 48);
		uint64_t w7 = hayahash64_internal_load64le(p + 56);
		h0 = (h0 ^ (w4 + hayahash64_internal_rotl(w3, 27))) * K;
		h1 = (h1 ^ (w5 + hayahash64_internal_rotl(w4, 27))) * K;
		h2 = (h2 ^ (w6 + hayahash64_internal_rotl(w5, 27))) * K;
		h3 = (h3 ^ (w7 + hayahash64_internal_rotl(w6, 27))) * K;
		uint64_t w8  = hayahash64_internal_load64le(p + 64);
		uint64_t w9  = hayahash64_internal_load64le(p + 72);
		uint64_t w10 = hayahash64_internal_load64le(p + 80);
		uint64_t w11 = hayahash64_internal_load64le(p + 88);
		h0 = (h0 ^ (w8  + hayahash64_internal_rotl(w7,  27))) * K;
		h1 = (h1 ^ (w9  + hayahash64_internal_rotl(w8,  27))) * K;
		h2 = (h2 ^ (w10 + hayahash64_internal_rotl(w9,  27))) * K;
		h3 = (h3 ^ (w11 + hayahash64_internal_rotl(w10, 27))) * K;
		uint64_t w12 = hayahash64_internal_load64le(p +  96);
		uint64_t w13 = hayahash64_internal_load64le(p + 104);
		uint64_t w14 = hayahash64_internal_load64le(p + 112);
		uint64_t w15 = hayahash64_internal_load64le(p + 120);
		h0 = (h0 ^ (w12 + hayahash64_internal_rotl(w11, 27))) * K;
		h1 = (h1 ^ (w13 + hayahash64_internal_rotl(w12, 27))) * K;
		h2 = (h2 ^ (w14 + hayahash64_internal_rotl(w13, 27))) * K;
		h3 = (h3 ^ (w15 + hayahash64_internal_rotl(w14, 27))) * K;
		uint64_t w16 = hayahash64_internal_load64le(p + 128);
		uint64_t w17 = hayahash64_internal_load64le(p + 136);
		uint64_t w18 = hayahash64_internal_load64le(p + 144);
		uint64_t w19 = hayahash64_internal_load64le(p + 152);
		h0 = (h0 ^ (w16 + hayahash64_internal_rotl(w15, 27))) * K;
		h1 = (h1 ^ (w17 + hayahash64_internal_rotl(w16, 27))) * K;
		h2 = (h2 ^ (w18 + hayahash64_internal_rotl(w17, 27))) * K;
		h3 = (h3 ^ (w19 + hayahash64_internal_rotl(w18, 27))) * K;
		uint64_t w20 = hayahash64_internal_load64le(p + 160);
		uint64_t w21 = hayahash64_internal_load64le(p + 168);
		uint64_t w22 = hayahash64_internal_load64le(p + 176);
		uint64_t w23 = hayahash64_internal_load64le(p + 184);
		h0 = (h0 ^ (w20 + hayahash64_internal_rotl(w19, 27))) * K;
		h1 = (h1 ^ (w21 + hayahash64_internal_rotl(w20, 27))) * K;
		h2 = (h2 ^ (w22 + hayahash64_internal_rotl(w21, 27))) * K;
		h3 = (h3 ^ (w23 + hayahash64_internal_rotl(w22, 27))) * K;
		if (l >= 224) {
			uint64_t w24 = hayahash64_internal_load64le(p + 192);
			uint64_t w25 = hayahash64_internal_load64le(p + 200);
			uint64_t w26 = hayahash64_internal_load64le(p + 208);
			uint64_t w27 = hayahash64_internal_load64le(p + 216);
			h0 = (h0 ^ (w24 + hayahash64_internal_rotl(w23, 27))) * K;
			h1 = (h1 ^ (w25 + hayahash64_internal_rotl(w24, 27))) * K;
			h2 = (h2 ^ (w26 + hayahash64_internal_rotl(w25, 27))) * K;
			h3 = (h3 ^ (w27 + hayahash64_internal_rotl(w26, 27))) * K;
			h0 += hayahash64_internal_rotl(w27, 27);
			if (l > 240) {
				h0 = (h0 + hayahash64_internal_injp(p + 224)) * K;
				h1 = (h1 + hayahash64_internal_injp(p + 232)) * K;
			}
			if (l > 224) {
				h2 = (h2 + hayahash64_internal_injp(p + l - 16)) * K;
				h3 = (h3 + hayahash64_internal_injp(p + l -  8)) * K;
			}
		} else {
			h0 += hayahash64_internal_rotl(w23, 27);
			if (l > 208) {
				h0 = (h0 + hayahash64_internal_injp(p + 192)) * K;
				h1 = (h1 + hayahash64_internal_injp(p + 200)) * K;
			}
			if (l > 192) {
				h2 = (h2 + hayahash64_internal_injp(p + l - 16)) * K;
				h3 = (h3 + hayahash64_internal_injp(p + l -  8)) * K;
			}
		}
		uint64_t t0 =
			(h0 ^ hayahash64_internal_rotl_product(h1, 13)) * K;
		uint64_t t1 =
			(h2 ^ hayahash64_internal_rotl_product(h3, 33)) * K;
		return hayahash64_internal_long_fmix(
			s ^ t0 ^ hayahash64_internal_rotl_product(t1, 29), K);
	}
#endif

	// Mid loop: same absorb as the bulk loop, 4 lanes over 32-byte
	// blocks. Only 17..319-byte inputs reach it; longer ones took
	// the hayahash64_internal_long call above.
#if HAYAHASH64_INTERNAL_TIERS
	// Only 256..319-byte keys get here (the tiers above return for
	// anything shorter), so run two rounds per iteration with at
	// most one single round left over; same absorb sequence, half
	// the loop control.
	for (; l >= 64; l -= 64, p += 64) {
		w = hayahash64_internal_load64le(p +  0);
		h0 = (h0 ^ (w + hayahash64_internal_rotl(wp, 27))) * K;
		wp = w;
		w = hayahash64_internal_load64le(p +  8);
		h1 = (h1 ^ (w + hayahash64_internal_rotl(wp, 27))) * K;
		wp = w;
		w = hayahash64_internal_load64le(p + 16);
		h2 = (h2 ^ (w + hayahash64_internal_rotl(wp, 27))) * K;
		wp = w;
		w = hayahash64_internal_load64le(p + 24);
		h3 = (h3 ^ (w + hayahash64_internal_rotl(wp, 27))) * K;
		wp = w;
		w = hayahash64_internal_load64le(p + 32);
		h0 = (h0 ^ (w + hayahash64_internal_rotl(wp, 27))) * K;
		wp = w;
		w = hayahash64_internal_load64le(p + 40);
		h1 = (h1 ^ (w + hayahash64_internal_rotl(wp, 27))) * K;
		wp = w;
		w = hayahash64_internal_load64le(p + 48);
		h2 = (h2 ^ (w + hayahash64_internal_rotl(wp, 27))) * K;
		wp = w;
		w = hayahash64_internal_load64le(p + 56);
		h3 = (h3 ^ (w + hayahash64_internal_rotl(wp, 27))) * K;
		wp = w;
	}
	if (l >= 32) {
		w = hayahash64_internal_load64le(p +  0);
		h0 = (h0 ^ (w + hayahash64_internal_rotl(wp, 27))) * K;
		wp = w;
		w = hayahash64_internal_load64le(p +  8);
		h1 = (h1 ^ (w + hayahash64_internal_rotl(wp, 27))) * K;
		wp = w;
		w = hayahash64_internal_load64le(p + 16);
		h2 = (h2 ^ (w + hayahash64_internal_rotl(wp, 27))) * K;
		wp = w;
		w = hayahash64_internal_load64le(p + 24);
		h3 = (h3 ^ (w + hayahash64_internal_rotl(wp, 27))) * K;
		wp = w;
		p += 32; l -= 32;
	}
#else
	for (; l >= 32; l -= 32, p += 32) {
		w = hayahash64_internal_load64le(p +  0);
		h0 = (h0 ^ (w + hayahash64_internal_rotl(wp, 27))) * K;
		wp = w;
		w = hayahash64_internal_load64le(p +  8);
		h1 = (h1 ^ (w + hayahash64_internal_rotl(wp, 27))) * K;
		wp = w;
		w = hayahash64_internal_load64le(p + 16);
		h2 = (h2 ^ (w + hayahash64_internal_rotl(wp, 27))) * K;
		wp = w;
		w = hayahash64_internal_load64le(p + 24);
		h3 = (h3 ^ (w + hayahash64_internal_rotl(wp, 27))) * K;
		wp = w;
	}
#endif

	// Absorb the final loop stripe's dangling rotated copy. Without
	// this wall, difference "ladders" (each stripe flip cancelling
	// the previous stripe's rotated copy with carry luck) can march
	// off the end of the loops and vanish; smhasher3 found exact
	// collisions at multiple-of-32 lengths this way. h0 is the lane
	// the copy would have landed in anyway.
	h0 += hayahash64_internal_rotl(wp, 27);

	// 0..31 bytes left. When more than 16 remain, h0/h1 take the
	// next whole 16 bytes and the last-16 absorbs below cover the
	// rest. Both branches test the untouched post-loop l (l > 16
	// implies l - 16 > 0), so p and l need no updates and p + l - 16
	// stays one loop-invariant end-of-input address.
	if (l > 16) {
		h0 = (h0 + hayahash64_internal_injp(p + 0)) * K;
		h1 = (h1 + hayahash64_internal_injp(p + 8)) * K;
	}
	// Reading the last 16 input bytes (overlapping already-hashed
	// data) is always valid here since total length > 16. Length is
	// already folded into every lane via `s`.
	if (l > 0) {
		h2 = (h2 + hayahash64_internal_injp(p + l - 16)) * K;
		h3 = (h3 + hayahash64_internal_injp(p + l -  8)) * K;
	}

	// Fold rotations must not be the additive inverse (mod 64) of the
	// absorb copy rotation 27: with rotl(h3, 37) here, a lane's raw
	// stripe difference and the next lane's rotated copy re-aligned
	// exactly in the fold and could xor-cancel with carry luck.
	uint64_t t0 = (h0 ^ hayahash64_internal_rotl(h1, 13)) * K;
	uint64_t t1 = (h2 ^ hayahash64_internal_rotl(h3, 33)) * K;
	uint64_t x = s ^ t0 ^ hayahash64_internal_rotl_product(t1, 29);
	return hayahash64_internal_long_fmix(x, K);
}

#endif // HAYAHASH_H
