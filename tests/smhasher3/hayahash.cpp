/*
 * hayahash64 - small, fast, portable 64-bit hash function
 *
 * This is free and unencumbered software released into the public
 * domain under The Unlicense (https://unlicense.org/).
 *
 * The reference implementation is available from:
 * https://github.com/thevilledev/hayahash
 */

// HayaHash uses eight independent bulk lanes and four short-input lanes. Each
// stripe also contributes a rotated copy to the following lane, preventing
// structural cancellation of high-bit differences. Seed and length are mixed
// into every lane, and tails use overlapping reads plus bijective injections.
// The algorithm uses only portable 64-bit arithmetic; compiler-specific paths
// below change code generation, never the digest.

#include "Platform.h"
#include "Hashlib.h"

// SMHasher3 supplies alignment-safe, endian-selectable readers. The template
// parameter selects the canonical little-endian digest or its byte-swapped
// counterpart independently of the host byte order.
template <bool bswap>
static inline uint64_t hayahash64_internal_load32le( const uint8_t * p ) {
    return GET_U32<bswap>(p, 0);
}

template <bool bswap>
static inline uint64_t hayahash64_internal_load64le( const uint8_t * p ) {
    return GET_U64<bswap>(p, 0);
}

static inline uint64_t hayahash64_internal_rotl( uint64_t x, int n ) {
    return ROTL64(x, n);
}

// Keep an already-computed product opaque. Originally this stopped Clang
// from distributing a following rotate into two independent multiplies;
// current clangs no longer apply that transform, but removal re-measures
// as a loss: on the M1 the guarded schedule is simply faster, and on
// Zen 5 the barrier keeps Clang from auto-vectorizing the latency-
// sensitive mid/tail paths. The empty barrier emits no instructions.
#if defined(__clang__)
  #define HAYAHASH64_INTERNAL_COMPILER_GUARD(v) __asm__("" : "+r" (v))
#else
  #define HAYAHASH64_INTERNAL_COMPILER_GUARD(v) ((void)0)
#endif

// Keep the register-heavy bulk path out of line so its spills do not penalize
// short inputs. `unused` also lets builds without this path drop it quietly.
#if defined(__GNUC__) || defined(__clang__)
  #define HAYAHASH64_INTERNAL_NOINLINE __attribute__((noinline, unused))
#elif defined(_MSC_VER)
  #define HAYAHASH64_INTERNAL_NOINLINE __declspec(noinline)
#else
  #define HAYAHASH64_INTERNAL_NOINLINE
#endif

// GCC benefits from straight-line tiers for 17..255-byte inputs and an
// unrolled mid loop above that. Clang performs better with the compact loop.
#if (defined(__aarch64__) || defined(__x86_64__)) && !defined(__clang__)
  #define HAYAHASH64_INTERNAL_TIERS 1
#else
  #define HAYAHASH64_INTERNAL_TIERS 0
#endif

static inline uint64_t hayahash64_internal_rotl_product( uint64_t x, int n ) {
    HAYAHASH64_INTERNAL_COMPILER_GUARD(x);
    return hayahash64_internal_rotl(x, n);
}

// Multiplier: 2^64 / golden ratio, odd.
#define HAYAHASH64_INTERNAL_K  UINT64_C(0x9E3779B97F4A7C15)
// moremur finalizer constants (Pelle Evensen); M1 doubles as the
// second multiplier of the short path.
#define HAYAHASH64_INTERNAL_M1 UINT64_C(0x3C79AC492BA7B653)
#define HAYAHASH64_INTERNAL_M2 UINT64_C(0x1C69B3F74AC4AE35)
#define HAYAHASH128_INTERNAL_N1 UINT64_C(0xFF51AFD7ED558CCD)
#define HAYAHASH128_INTERNAL_N2 UINT64_C(0xC4CEB9FE1A85EC53)

// moremur finalizer (Pelle Evensen), also used by ChibiHash v1.
static inline uint64_t hayahash64_internal_fmix( uint64_t x ) {
    x ^= x >> 27; x *= HAYAHASH64_INTERNAL_M1;
    x ^= x >> 33; x *= HAYAHASH64_INTERNAL_M2;
    x ^= x >> 27;
    return x;
}

static inline uint64_t hayahash128_internal_fmix( uint64_t x ) {
    x ^= x >> 30; x *= HAYAHASH128_INTERNAL_N1;
    x ^= x >> 31; x *= HAYAHASH128_INTERNAL_N2;
    x ^= x >> 33;
    return x;
}

// The long path has already passed every input byte through a
// multiply and a non-linear lane merge. One multiply is enough for
// its final avalanche; reusing K also avoids a new 64-bit literal.
static inline uint64_t hayahash64_internal_long_fmix( uint64_t x, uint64_t K ) {
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
static inline uint64_t hayahash64_internal_inj( uint64_t w ) {
    uint64_t x = w ^ hayahash64_internal_rotl(w, 21);
    HAYAHASH64_INTERNAL_COMPILER_GUARD(x);
    return x ^ hayahash64_internal_rotl(w, 41);
}
static inline uint64_t hayahash64_internal_inj2( uint64_t w ) {
    uint64_t x = w ^ hayahash64_internal_rotl(w, 11);
    HAYAHASH64_INTERNAL_COMPILER_GUARD(x);
    return x ^ hayahash64_internal_rotl(w, 50);
}
template <bool bswap>
static inline uint64_t hayahash64_internal_injp( const uint8_t * p ) {
    return hayahash64_internal_inj(hayahash64_internal_load64le<bswap>(p));
}

// Fixed algorithm parameter: keeping the 4-lane path below 320 bytes
// bounds it to fewer than the rotation's 64 stripes. Changing this
// boundary changes the digest and can invalidate that structural bound.
enum { hayahash64_internal_bulk_min = 320 };

// One 64-byte block over lanes h0..h7. The checkpoint into h0 prevents
// rotation-orbit differences from disappearing, and the macro permits a
// two-block unroll. On Zen 4/5, the array spelling lets GCC vectorize the
// middle four lanes; other targets retain the scalar spelling because their
// packed 64-bit multiply can have much higher latency.
#if defined(__x86_64__) && defined(__GNUC__) && !defined(__clang__) && \
    defined(__AVX512DQ__) && (defined(__znver4__) || defined(__znver5__))
  #define HAYAHASH64_INTERNAL_VECGCC 1
#else
  #define HAYAHASH64_INTERNAL_VECGCC 0
#endif

// Prevent Clang from vectorizing these lanes on non-Zen AVX-512 targets,
// where packed 64-bit multiplication increases the dependency latency.
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
        w = hayahash64_internal_load64le<bswap>((q) +  0); \
        h0 = (h0 ^ (w + hayahash64_internal_rotl(wp, 27))) * K; \
        h1 = (h1 ^ (hayahash64_internal_load64le<bswap>((q) +  8) + \
            hayahash64_internal_rotl(w, 27))) * K; \
        for (int hh_i = 0; hh_i < 4; hh_i++) \
            hv[hh_i] = (hv[hh_i] ^ \
                (hayahash64_internal_load64le<bswap>((q) + 16 + 8 * hh_i) + \
                 hayahash64_internal_rotl( \
                    hayahash64_internal_load64le<bswap>((q) + 8 + 8 * hh_i), 27))) * K; \
        h6 = (h6 ^ (hayahash64_internal_load64le<bswap>((q) + 48) + \
            hayahash64_internal_rotl(hayahash64_internal_load64le<bswap>((q) + 40), 27))) * K; \
        h7 = (h7 ^ (hayahash64_internal_load64le<bswap>((q) + 56) + \
            hayahash64_internal_rotl(hayahash64_internal_load64le<bswap>((q) + 48), 27))) * K; \
        wp = hayahash64_internal_load64le<bswap>((q) + 56); \
        h0 += wp; \
    } while (0)
#else
  #define HAYAHASH64_INTERNAL_BULK_BLOCK(q) \
    do { \
        w = hayahash64_internal_load64le<bswap>((q) +  0); \
        h0 = (h0 ^ (w + hayahash64_internal_rotl(wp, 27))) * K; \
        wp = w; \
        w = hayahash64_internal_load64le<bswap>((q) +  8); \
        h1 = (h1 ^ (w + hayahash64_internal_rotl(wp, 27))) * K; \
        wp = w; \
        w = hayahash64_internal_load64le<bswap>((q) + 16); \
        h2 = (h2 ^ (w + hayahash64_internal_rotl(wp, 27))) * K; \
        wp = w; \
        w = hayahash64_internal_load64le<bswap>((q) + 24); \
        h3 = (h3 ^ (w + hayahash64_internal_rotl(wp, 27))) * K; \
        wp = w; \
        w = hayahash64_internal_load64le<bswap>((q) + 32); \
        h4 = (h4 ^ (w + hayahash64_internal_rotl(wp, 27))) * K; \
        wp = w; \
        w = hayahash64_internal_load64le<bswap>((q) + 40); \
        h5 = (h5 ^ (w + hayahash64_internal_rotl(wp, 27))) * K; \
        wp = w; \
        w = hayahash64_internal_load64le<bswap>((q) + 48); \
        h6 = (h6 ^ (w + hayahash64_internal_rotl(wp, 27))) * K; \
        wp = w; \
        w = hayahash64_internal_load64le<bswap>((q) + 56); \
        h7 = (h7 ^ (w + hayahash64_internal_rotl(wp, 27))) * K; \
        wp = w; \
        h0 += wp; \
        HAYAHASH64_INTERNAL_BULK_LANE_GUARD(h2); \
        HAYAHASH64_INTERNAL_BULK_LANE_GUARD(h3); \
        HAYAHASH64_INTERNAL_BULK_LANE_GUARD(h4); \
        HAYAHASH64_INTERNAL_BULK_LANE_GUARD(h5); \
    } while (0)
#endif

// Long-input path. Its signature mirrors hayahash64 so compilers can tail-call
// it without constraining the caller's register allocation.
template <bool bswap>
static HAYAHASH64_INTERNAL_NOINLINE uint64_t hayahash64_internal_long(
        const void * keyIn, std::ptrdiff_t len, uint64_t seed ) {
    // Restate the range invariant lost across the noinline boundary so the
    // compiler can simplify loop entry and remainder handling.
#if defined(__GNUC__) || defined(__clang__)
    if (len < (std::ptrdiff_t)hayahash64_internal_bulk_min)
        __builtin_unreachable();
#elif defined(_MSC_VER)
    __assume(len >= (std::ptrdiff_t)hayahash64_internal_bulk_min);
#endif
    const uint8_t * p = (const uint8_t *)keyIn;
    std::ptrdiff_t l = len;
    uint64_t K = HAYAHASH64_INTERNAL_K;
#if defined(__aarch64__) && (defined(__GNUC__) || defined(__clang__))
    HAYAHASH64_INTERNAL_COMPILER_GUARD(K);
#endif
    const uint64_t lenmix = (uint64_t)len * K;
    uint64_t s = seed ^ K;
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
    // Process two blocks per iteration. The precomputed end pointer hoists
    // remainder work out of the loop and avoids keeping a trip count live.
  #if HAYAHASH64_INTERNAL_VECGCC
    // The vectorized middle lanes; folded back after the last block.
    uint64_t hv[4] = { h2, h3, h4, h5 };
  #endif
    const uint8_t * pe = p + ((size_t)l & ~(size_t)127);
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

    // At most one mid round remains; wp chains in from the bulk loop.
    if (l >= 32) {
        w = hayahash64_internal_load64le<bswap>(p +  0);
        h0 = (h0 ^ (w + hayahash64_internal_rotl(wp, 27))) * K;
        wp = w;
        w = hayahash64_internal_load64le<bswap>(p +  8);
        h1 = (h1 ^ (w + hayahash64_internal_rotl(wp, 27))) * K;
        wp = w;
        w = hayahash64_internal_load64le<bswap>(p + 16);
        h2 = (h2 ^ (w + hayahash64_internal_rotl(wp, 27))) * K;
        wp = w;
        w = hayahash64_internal_load64le<bswap>(p + 24);
        h3 = (h3 ^ (w + hayahash64_internal_rotl(wp, 27))) * K;
        wp = w;
        p += 32; l -= 32;
    }

    // Absorb the final stripe's dangling rotated copy; see the wall
    // comment in hayahash64.
    h0 += hayahash64_internal_rotl(wp, 27);

    if (l > 16) {
        h0 = (h0 + hayahash64_internal_injp<bswap>(p + 0)) * K;
        h1 = (h1 + hayahash64_internal_injp<bswap>(p + 8)) * K;
    }
    if (l > 0) {
        h2 = (h2 + hayahash64_internal_injp<bswap>(p + l - 16)) * K;
        h3 = (h3 + hayahash64_internal_injp<bswap>(p + l -  8)) * K;
    }

    uint64_t t0 = (h0 ^ hayahash64_internal_rotl(h1, 13) ^ lenmix) * K;
    uint64_t t1 = (h2 ^ hayahash64_internal_rotl(h3, 33)) * K;
    uint64_t x = s ^ t0 ^ hayahash64_internal_rotl_product(t1, 29);
    return hayahash64_internal_long_fmix(x, K);
}

// `len` must be non-negative, and `keyIn` must point to at least `len`
// readable bytes (it may be null when len is zero).
template <bool bswap>
static inline uint64_t hayahash64( const void * keyIn, std::ptrdiff_t len, uint64_t seed ) {
    const uint8_t * p = (const uint8_t *)keyIn;
    std::ptrdiff_t l = len;
    uint64_t K = HAYAHASH64_INTERNAL_K;
    // The seed premixes into s; the length is absorbed in the
    // finalizer instead (through a multiply against state), so the
    // digest is a pure function of (seed, bytes-so-far) and a
    // streaming implementation can produce identical digests without
    // knowing the total length up front. len -> len*K is injective,
    // which keeps the overlapping tail reads collision-free across
    // lengths.
    const uint64_t lenmix = (uint64_t)len * K;
    uint64_t s = seed ^ K;

    if (l <= 16) {
        uint64_t a, b;
        if (l >= 8) {
            a = hayahash64_internal_load64le<bswap>(p);
            b = hayahash64_internal_load64le<bswap>(p + l - 8);
        } else if (l >= 4) {
            a = hayahash64_internal_load32le<bswap>(p);
            b = hayahash64_internal_load32le<bswap>(p + l - 4);
        } else if (l > 0) {
            // 1..3 bytes: head, middle, tail (ChibiHash v2 trick).
            a = p[0];
            b = ((uint64_t)p[l >> 1] << 8) | ((uint64_t)p[l - 1] << 16);
        } else {
            a = 0; b = 0;
        }
        // Spread both words before independent multiplies so high input bits
        // reach low positions and neither seed copy can be erased sparsely.
        uint64_t x = (hayahash64_internal_inj(a) ^ s ^ K) * K;
        uint64_t y = (hayahash64_internal_inj2(b) ^ hayahash64_internal_rotl(s, 23) ^
                      (K >> 19)) * HAYAHASH64_INTERNAL_M1;
        return hayahash64_internal_fmix(
            hayahash64_internal_rotl_product(x, 27) ^ y ^ lenmix);
    }

#if !defined(__wasm__)
    if (l >= hayahash64_internal_bulk_min)
        return hayahash64_internal_long<bswap>(keyIn, len, seed);
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
    // Wasm keeps the bulk path inline to avoid duplicating fold/tail code;
    // it executes the same block sequence as the outlined path.
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
        h0 = (h0 + hayahash64_internal_injp<bswap>(p + 0)) * K;
        h1 = (h1 + hayahash64_internal_injp<bswap>(p + 8)) * K;
        h2 = (h2 + hayahash64_internal_injp<bswap>(p + l - 16)) * K;
        h3 = (h3 + hayahash64_internal_injp<bswap>(p + l - 8)) * K;
        uint64_t t0 =
            (h0 ^ hayahash64_internal_rotl_product(h1, 13) ^ lenmix) * K;
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
        uint64_t w0 = hayahash64_internal_load64le<bswap>(p +  0);
        uint64_t w1 = hayahash64_internal_load64le<bswap>(p +  8);
        uint64_t w2 = hayahash64_internal_load64le<bswap>(p + 16);
        uint64_t w3 = hayahash64_internal_load64le<bswap>(p + 24);
        h0 = (h0 ^ w0) * K;
        h1 = (h1 ^ (w1 + hayahash64_internal_rotl(w0, 27))) * K;
        h2 = (h2 ^ (w2 + hayahash64_internal_rotl(w1, 27))) * K;
        h3 = (h3 ^ (w3 + hayahash64_internal_rotl(w2, 27))) * K;
        h0 += hayahash64_internal_rotl(w3, 27);
        if (l > 48) {
            h0 = (h0 + hayahash64_internal_injp<bswap>(p + 32)) * K;
            h1 = (h1 + hayahash64_internal_injp<bswap>(p + 40)) * K;
        }
        if (l > 32) {
            h2 = (h2 + hayahash64_internal_injp<bswap>(p + l - 16)) * K;
            h3 = (h3 + hayahash64_internal_injp<bswap>(p + l -  8)) * K;
        }
        uint64_t t0 =
            (h0 ^ hayahash64_internal_rotl_product(h1, 13) ^ lenmix) * K;
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
        uint64_t w0 = hayahash64_internal_load64le<bswap>(p +  0);
        uint64_t w1 = hayahash64_internal_load64le<bswap>(p +  8);
        uint64_t w2 = hayahash64_internal_load64le<bswap>(p + 16);
        uint64_t w3 = hayahash64_internal_load64le<bswap>(p + 24);
        h0 = (h0 ^ w0) * K;
        h1 = (h1 ^ (w1 + hayahash64_internal_rotl(w0, 27))) * K;
        h2 = (h2 ^ (w2 + hayahash64_internal_rotl(w1, 27))) * K;
        h3 = (h3 ^ (w3 + hayahash64_internal_rotl(w2, 27))) * K;
        uint64_t w4 = hayahash64_internal_load64le<bswap>(p + 32);
        uint64_t w5 = hayahash64_internal_load64le<bswap>(p + 40);
        uint64_t w6 = hayahash64_internal_load64le<bswap>(p + 48);
        uint64_t w7 = hayahash64_internal_load64le<bswap>(p + 56);
        h0 = (h0 ^ (w4 + hayahash64_internal_rotl(w3, 27))) * K;
        h1 = (h1 ^ (w5 + hayahash64_internal_rotl(w4, 27))) * K;
        h2 = (h2 ^ (w6 + hayahash64_internal_rotl(w5, 27))) * K;
        h3 = (h3 ^ (w7 + hayahash64_internal_rotl(w6, 27))) * K;
        if (l >= 96) {
            uint64_t w8  = hayahash64_internal_load64le<bswap>(p + 64);
            uint64_t w9  = hayahash64_internal_load64le<bswap>(p + 72);
            uint64_t w10 = hayahash64_internal_load64le<bswap>(p + 80);
            uint64_t w11 = hayahash64_internal_load64le<bswap>(p + 88);
            h0 = (h0 ^ (w8  + hayahash64_internal_rotl(w7,  27))) * K;
            h1 = (h1 ^ (w9  + hayahash64_internal_rotl(w8,  27))) * K;
            h2 = (h2 ^ (w10 + hayahash64_internal_rotl(w9,  27))) * K;
            h3 = (h3 ^ (w11 + hayahash64_internal_rotl(w10, 27))) * K;
            h0 += hayahash64_internal_rotl(w11, 27);
            if (l > 112) {
                h0 = (h0 + hayahash64_internal_injp<bswap>(p +  96)) * K;
                h1 = (h1 + hayahash64_internal_injp<bswap>(p + 104)) * K;
            }
            if (l > 96) {
                h2 = (h2 + hayahash64_internal_injp<bswap>(p + l - 16)) * K;
                h3 = (h3 + hayahash64_internal_injp<bswap>(p + l -  8)) * K;
            }
        } else {
            h0 += hayahash64_internal_rotl(w7, 27);
            if (l > 80) {
                h0 = (h0 + hayahash64_internal_injp<bswap>(p + 64)) * K;
                h1 = (h1 + hayahash64_internal_injp<bswap>(p + 72)) * K;
            }
            if (l > 64) {
                h2 = (h2 + hayahash64_internal_injp<bswap>(p + l - 16)) * K;
                h3 = (h3 + hayahash64_internal_injp<bswap>(p + l -  8)) * K;
            }
        }
        uint64_t t0 =
            (h0 ^ hayahash64_internal_rotl_product(h1, 13) ^ lenmix) * K;
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
        uint64_t w0 = hayahash64_internal_load64le<bswap>(p +  0);
        uint64_t w1 = hayahash64_internal_load64le<bswap>(p +  8);
        uint64_t w2 = hayahash64_internal_load64le<bswap>(p + 16);
        uint64_t w3 = hayahash64_internal_load64le<bswap>(p + 24);
        h0 = (h0 ^ w0) * K;
        h1 = (h1 ^ (w1 + hayahash64_internal_rotl(w0, 27))) * K;
        h2 = (h2 ^ (w2 + hayahash64_internal_rotl(w1, 27))) * K;
        h3 = (h3 ^ (w3 + hayahash64_internal_rotl(w2, 27))) * K;
        uint64_t w4 = hayahash64_internal_load64le<bswap>(p + 32);
        uint64_t w5 = hayahash64_internal_load64le<bswap>(p + 40);
        uint64_t w6 = hayahash64_internal_load64le<bswap>(p + 48);
        uint64_t w7 = hayahash64_internal_load64le<bswap>(p + 56);
        h0 = (h0 ^ (w4 + hayahash64_internal_rotl(w3, 27))) * K;
        h1 = (h1 ^ (w5 + hayahash64_internal_rotl(w4, 27))) * K;
        h2 = (h2 ^ (w6 + hayahash64_internal_rotl(w5, 27))) * K;
        h3 = (h3 ^ (w7 + hayahash64_internal_rotl(w6, 27))) * K;
        uint64_t w8  = hayahash64_internal_load64le<bswap>(p + 64);
        uint64_t w9  = hayahash64_internal_load64le<bswap>(p + 72);
        uint64_t w10 = hayahash64_internal_load64le<bswap>(p + 80);
        uint64_t w11 = hayahash64_internal_load64le<bswap>(p + 88);
        h0 = (h0 ^ (w8  + hayahash64_internal_rotl(w7,  27))) * K;
        h1 = (h1 ^ (w9  + hayahash64_internal_rotl(w8,  27))) * K;
        h2 = (h2 ^ (w10 + hayahash64_internal_rotl(w9,  27))) * K;
        h3 = (h3 ^ (w11 + hayahash64_internal_rotl(w10, 27))) * K;
        uint64_t w12 = hayahash64_internal_load64le<bswap>(p +  96);
        uint64_t w13 = hayahash64_internal_load64le<bswap>(p + 104);
        uint64_t w14 = hayahash64_internal_load64le<bswap>(p + 112);
        uint64_t w15 = hayahash64_internal_load64le<bswap>(p + 120);
        h0 = (h0 ^ (w12 + hayahash64_internal_rotl(w11, 27))) * K;
        h1 = (h1 ^ (w13 + hayahash64_internal_rotl(w12, 27))) * K;
        h2 = (h2 ^ (w14 + hayahash64_internal_rotl(w13, 27))) * K;
        h3 = (h3 ^ (w15 + hayahash64_internal_rotl(w14, 27))) * K;
        if (l >= 160) {
            uint64_t w16 = hayahash64_internal_load64le<bswap>(p + 128);
            uint64_t w17 = hayahash64_internal_load64le<bswap>(p + 136);
            uint64_t w18 = hayahash64_internal_load64le<bswap>(p + 144);
            uint64_t w19 = hayahash64_internal_load64le<bswap>(p + 152);
            h0 = (h0 ^ (w16 + hayahash64_internal_rotl(w15, 27))) * K;
            h1 = (h1 ^ (w17 + hayahash64_internal_rotl(w16, 27))) * K;
            h2 = (h2 ^ (w18 + hayahash64_internal_rotl(w17, 27))) * K;
            h3 = (h3 ^ (w19 + hayahash64_internal_rotl(w18, 27))) * K;
            h0 += hayahash64_internal_rotl(w19, 27);
            if (l > 176) {
                h0 = (h0 + hayahash64_internal_injp<bswap>(p + 160)) * K;
                h1 = (h1 + hayahash64_internal_injp<bswap>(p + 168)) * K;
            }
            if (l > 160) {
                h2 = (h2 + hayahash64_internal_injp<bswap>(p + l - 16)) * K;
                h3 = (h3 + hayahash64_internal_injp<bswap>(p + l -  8)) * K;
            }
        } else {
            h0 += hayahash64_internal_rotl(w15, 27);
            if (l > 144) {
                h0 = (h0 + hayahash64_internal_injp<bswap>(p + 128)) * K;
                h1 = (h1 + hayahash64_internal_injp<bswap>(p + 136)) * K;
            }
            if (l > 128) {
                h2 = (h2 + hayahash64_internal_injp<bswap>(p + l - 16)) * K;
                h3 = (h3 + hayahash64_internal_injp<bswap>(p + l -  8)) * K;
            }
        }
        uint64_t t0 =
            (h0 ^ hayahash64_internal_rotl_product(h1, 13) ^ lenmix) * K;
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
        uint64_t w0 = hayahash64_internal_load64le<bswap>(p +  0);
        uint64_t w1 = hayahash64_internal_load64le<bswap>(p +  8);
        uint64_t w2 = hayahash64_internal_load64le<bswap>(p + 16);
        uint64_t w3 = hayahash64_internal_load64le<bswap>(p + 24);
        h0 = (h0 ^ w0) * K;
        h1 = (h1 ^ (w1 + hayahash64_internal_rotl(w0, 27))) * K;
        h2 = (h2 ^ (w2 + hayahash64_internal_rotl(w1, 27))) * K;
        h3 = (h3 ^ (w3 + hayahash64_internal_rotl(w2, 27))) * K;
        uint64_t w4 = hayahash64_internal_load64le<bswap>(p + 32);
        uint64_t w5 = hayahash64_internal_load64le<bswap>(p + 40);
        uint64_t w6 = hayahash64_internal_load64le<bswap>(p + 48);
        uint64_t w7 = hayahash64_internal_load64le<bswap>(p + 56);
        h0 = (h0 ^ (w4 + hayahash64_internal_rotl(w3, 27))) * K;
        h1 = (h1 ^ (w5 + hayahash64_internal_rotl(w4, 27))) * K;
        h2 = (h2 ^ (w6 + hayahash64_internal_rotl(w5, 27))) * K;
        h3 = (h3 ^ (w7 + hayahash64_internal_rotl(w6, 27))) * K;
        uint64_t w8  = hayahash64_internal_load64le<bswap>(p + 64);
        uint64_t w9  = hayahash64_internal_load64le<bswap>(p + 72);
        uint64_t w10 = hayahash64_internal_load64le<bswap>(p + 80);
        uint64_t w11 = hayahash64_internal_load64le<bswap>(p + 88);
        h0 = (h0 ^ (w8  + hayahash64_internal_rotl(w7,  27))) * K;
        h1 = (h1 ^ (w9  + hayahash64_internal_rotl(w8,  27))) * K;
        h2 = (h2 ^ (w10 + hayahash64_internal_rotl(w9,  27))) * K;
        h3 = (h3 ^ (w11 + hayahash64_internal_rotl(w10, 27))) * K;
        uint64_t w12 = hayahash64_internal_load64le<bswap>(p +  96);
        uint64_t w13 = hayahash64_internal_load64le<bswap>(p + 104);
        uint64_t w14 = hayahash64_internal_load64le<bswap>(p + 112);
        uint64_t w15 = hayahash64_internal_load64le<bswap>(p + 120);
        h0 = (h0 ^ (w12 + hayahash64_internal_rotl(w11, 27))) * K;
        h1 = (h1 ^ (w13 + hayahash64_internal_rotl(w12, 27))) * K;
        h2 = (h2 ^ (w14 + hayahash64_internal_rotl(w13, 27))) * K;
        h3 = (h3 ^ (w15 + hayahash64_internal_rotl(w14, 27))) * K;
        uint64_t w16 = hayahash64_internal_load64le<bswap>(p + 128);
        uint64_t w17 = hayahash64_internal_load64le<bswap>(p + 136);
        uint64_t w18 = hayahash64_internal_load64le<bswap>(p + 144);
        uint64_t w19 = hayahash64_internal_load64le<bswap>(p + 152);
        h0 = (h0 ^ (w16 + hayahash64_internal_rotl(w15, 27))) * K;
        h1 = (h1 ^ (w17 + hayahash64_internal_rotl(w16, 27))) * K;
        h2 = (h2 ^ (w18 + hayahash64_internal_rotl(w17, 27))) * K;
        h3 = (h3 ^ (w19 + hayahash64_internal_rotl(w18, 27))) * K;
        uint64_t w20 = hayahash64_internal_load64le<bswap>(p + 160);
        uint64_t w21 = hayahash64_internal_load64le<bswap>(p + 168);
        uint64_t w22 = hayahash64_internal_load64le<bswap>(p + 176);
        uint64_t w23 = hayahash64_internal_load64le<bswap>(p + 184);
        h0 = (h0 ^ (w20 + hayahash64_internal_rotl(w19, 27))) * K;
        h1 = (h1 ^ (w21 + hayahash64_internal_rotl(w20, 27))) * K;
        h2 = (h2 ^ (w22 + hayahash64_internal_rotl(w21, 27))) * K;
        h3 = (h3 ^ (w23 + hayahash64_internal_rotl(w22, 27))) * K;
        if (l >= 224) {
            uint64_t w24 = hayahash64_internal_load64le<bswap>(p + 192);
            uint64_t w25 = hayahash64_internal_load64le<bswap>(p + 200);
            uint64_t w26 = hayahash64_internal_load64le<bswap>(p + 208);
            uint64_t w27 = hayahash64_internal_load64le<bswap>(p + 216);
            h0 = (h0 ^ (w24 + hayahash64_internal_rotl(w23, 27))) * K;
            h1 = (h1 ^ (w25 + hayahash64_internal_rotl(w24, 27))) * K;
            h2 = (h2 ^ (w26 + hayahash64_internal_rotl(w25, 27))) * K;
            h3 = (h3 ^ (w27 + hayahash64_internal_rotl(w26, 27))) * K;
            h0 += hayahash64_internal_rotl(w27, 27);
            if (l > 240) {
                h0 = (h0 + hayahash64_internal_injp<bswap>(p + 224)) * K;
                h1 = (h1 + hayahash64_internal_injp<bswap>(p + 232)) * K;
            }
            if (l > 224) {
                h2 = (h2 + hayahash64_internal_injp<bswap>(p + l - 16)) * K;
                h3 = (h3 + hayahash64_internal_injp<bswap>(p + l -  8)) * K;
            }
        } else {
            h0 += hayahash64_internal_rotl(w23, 27);
            if (l > 208) {
                h0 = (h0 + hayahash64_internal_injp<bswap>(p + 192)) * K;
                h1 = (h1 + hayahash64_internal_injp<bswap>(p + 200)) * K;
            }
            if (l > 192) {
                h2 = (h2 + hayahash64_internal_injp<bswap>(p + l - 16)) * K;
                h3 = (h3 + hayahash64_internal_injp<bswap>(p + l -  8)) * K;
            }
        }
        uint64_t t0 =
            (h0 ^ hayahash64_internal_rotl_product(h1, 13) ^ lenmix) * K;
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
        w = hayahash64_internal_load64le<bswap>(p +  0);
        h0 = (h0 ^ (w + hayahash64_internal_rotl(wp, 27))) * K;
        wp = w;
        w = hayahash64_internal_load64le<bswap>(p +  8);
        h1 = (h1 ^ (w + hayahash64_internal_rotl(wp, 27))) * K;
        wp = w;
        w = hayahash64_internal_load64le<bswap>(p + 16);
        h2 = (h2 ^ (w + hayahash64_internal_rotl(wp, 27))) * K;
        wp = w;
        w = hayahash64_internal_load64le<bswap>(p + 24);
        h3 = (h3 ^ (w + hayahash64_internal_rotl(wp, 27))) * K;
        wp = w;
        w = hayahash64_internal_load64le<bswap>(p + 32);
        h0 = (h0 ^ (w + hayahash64_internal_rotl(wp, 27))) * K;
        wp = w;
        w = hayahash64_internal_load64le<bswap>(p + 40);
        h1 = (h1 ^ (w + hayahash64_internal_rotl(wp, 27))) * K;
        wp = w;
        w = hayahash64_internal_load64le<bswap>(p + 48);
        h2 = (h2 ^ (w + hayahash64_internal_rotl(wp, 27))) * K;
        wp = w;
        w = hayahash64_internal_load64le<bswap>(p + 56);
        h3 = (h3 ^ (w + hayahash64_internal_rotl(wp, 27))) * K;
        wp = w;
    }
    if (l >= 32) {
        w = hayahash64_internal_load64le<bswap>(p +  0);
        h0 = (h0 ^ (w + hayahash64_internal_rotl(wp, 27))) * K;
        wp = w;
        w = hayahash64_internal_load64le<bswap>(p +  8);
        h1 = (h1 ^ (w + hayahash64_internal_rotl(wp, 27))) * K;
        wp = w;
        w = hayahash64_internal_load64le<bswap>(p + 16);
        h2 = (h2 ^ (w + hayahash64_internal_rotl(wp, 27))) * K;
        wp = w;
        w = hayahash64_internal_load64le<bswap>(p + 24);
        h3 = (h3 ^ (w + hayahash64_internal_rotl(wp, 27))) * K;
        wp = w;
        p += 32; l -= 32;
    }
#else
    for (; l >= 32; l -= 32, p += 32) {
        w = hayahash64_internal_load64le<bswap>(p +  0);
        h0 = (h0 ^ (w + hayahash64_internal_rotl(wp, 27))) * K;
        wp = w;
        w = hayahash64_internal_load64le<bswap>(p +  8);
        h1 = (h1 ^ (w + hayahash64_internal_rotl(wp, 27))) * K;
        wp = w;
        w = hayahash64_internal_load64le<bswap>(p + 16);
        h2 = (h2 ^ (w + hayahash64_internal_rotl(wp, 27))) * K;
        wp = w;
        w = hayahash64_internal_load64le<bswap>(p + 24);
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
        h0 = (h0 + hayahash64_internal_injp<bswap>(p + 0)) * K;
        h1 = (h1 + hayahash64_internal_injp<bswap>(p + 8)) * K;
    }
    // Reading the last 16 input bytes (overlapping already-hashed
    // data) is always valid here since total length > 16. Length is
    // already folded into every lane via `s`.
    if (l > 0) {
        h2 = (h2 + hayahash64_internal_injp<bswap>(p + l - 16)) * K;
        h3 = (h3 + hayahash64_internal_injp<bswap>(p + l -  8)) * K;
    }

    // Fold rotations must not be the additive inverse (mod 64) of the
    // absorb copy rotation 27: with rotl(h3, 37) here, a lane's raw
    // stripe difference and the next lane's rotated copy re-aligned
    // exactly in the fold and could xor-cancel with carry luck.
    uint64_t t0 = (h0 ^ hayahash64_internal_rotl(h1, 13) ^ lenmix) * K;
    uint64_t t1 = (h2 ^ hayahash64_internal_rotl(h3, 33)) * K;
    uint64_t x = s ^ t0 ^ hayahash64_internal_rotl_product(t1, 29);
    return hayahash64_internal_long_fmix(x, K);
}

//------------------------------------------------------------
struct hayahash128_t { uint64_t lo, hi; };

static inline hayahash128_t hayahash128_internal_short(
        uint64_t x, uint64_t y, uint64_t lenmix ) {
    uint64_t u = hayahash64_internal_rotl_product(x, 27) ^ y ^ lenmix;
    return {
        hayahash64_internal_fmix(u),
        hayahash128_internal_fmix(x + hayahash64_internal_rotl(u, 32))
    };
}

static inline hayahash128_t hayahash128_internal_long(
        uint64_t s, uint64_t t0, uint64_t t1, uint64_t K ) {
    return {
        hayahash64_internal_long_fmix(
            s ^ t0 ^ hayahash64_internal_rotl_product(t1, 29), K),
        hayahash128_internal_fmix(hayahash64_internal_rotl(s, 32) ^
            (t1 + hayahash64_internal_rotl(t0, 47)))
    };
}

// Compact 128-bit spelling of the same state walk. The low output is
// bit-identical to hayahash64; the second output is extracted from the
// already-computed short pre-image or long t0/t1 folds.
template <bool bswap>
static inline hayahash128_t hayahash128(
        const void * keyIn, std::ptrdiff_t len, uint64_t seed ) {
    const uint8_t * p = (const uint8_t *)keyIn;
    std::ptrdiff_t l = len;
    uint64_t K = HAYAHASH64_INTERNAL_K;
    const uint64_t lenmix = (uint64_t)len * K;
    uint64_t s = seed ^ K;

    if (l <= 16) {
        uint64_t a, b;
        if (l >= 8) {
            a = hayahash64_internal_load64le<bswap>(p);
            b = hayahash64_internal_load64le<bswap>(p + l - 8);
        } else if (l >= 4) {
            a = hayahash64_internal_load32le<bswap>(p);
            b = hayahash64_internal_load32le<bswap>(p + l - 4);
        } else if (l > 0) {
            a = p[0];
            b = ((uint64_t)p[l >> 1] << 8) | ((uint64_t)p[l - 1] << 16);
        } else {
            a = 0; b = 0;
        }
        uint64_t x = (hayahash64_internal_inj(a) ^ s ^ K) * K;
        uint64_t y = (hayahash64_internal_inj2(b) ^
                      hayahash64_internal_rotl(s, 23) ^ (K >> 19)) *
                     HAYAHASH64_INTERNAL_M1;
        return hayahash128_internal_short(x, y, lenmix);
    }

#if defined(__aarch64__) && (defined(__GNUC__) || defined(__clang__))
    HAYAHASH64_INTERNAL_COMPILER_GUARD(K);
#endif
    uint64_t h0 = s ^ K;
    uint64_t h1 = hayahash64_internal_rotl(s, 17) + (K << 21);
    uint64_t h2 = hayahash64_internal_rotl(s, 34) ^ (K >> 13);
    uint64_t h3 = hayahash64_internal_rotl(s, 51) + (K << 42);
    uint64_t w, wp = 0;

    if (l >= hayahash64_internal_bulk_min) {
        uint64_t h4 = s + (K >> 27);
        uint64_t h5 = hayahash64_internal_rotl(s, 13) ^ (K << 9);
        uint64_t h6 = hayahash64_internal_rotl(s, 26) + (K >> 40);
        uint64_t h7 = hayahash64_internal_rotl(s, 39) ^ (K << 30);
#if HAYAHASH64_INTERNAL_VECGCC
        uint64_t hv[4] = { h2, h3, h4, h5 };
#endif
        do {
            HAYAHASH64_INTERNAL_BULK_BLOCK(p);
            p += 64; l -= 64;
        } while (l >= 64);
#if HAYAHASH64_INTERNAL_VECGCC
        h2 = hv[0]; h3 = hv[1]; h4 = hv[2]; h5 = hv[3];
#endif
        h0 = (h0 ^ hayahash64_internal_rotl_product(h4, 11)) * K;
        h1 = (h1 ^ hayahash64_internal_rotl_product(h5, 19)) * K;
        h2 = (h2 ^ hayahash64_internal_rotl_product(h6, 31)) * K;
        h3 = (h3 ^ hayahash64_internal_rotl_product(h7, 47)) * K;
        if (l >= 32) {
            w = hayahash64_internal_load64le<bswap>(p + 0);
            h0 = (h0 ^ (w + hayahash64_internal_rotl(wp, 27))) * K; wp = w;
            w = hayahash64_internal_load64le<bswap>(p + 8);
            h1 = (h1 ^ (w + hayahash64_internal_rotl(wp, 27))) * K; wp = w;
            w = hayahash64_internal_load64le<bswap>(p + 16);
            h2 = (h2 ^ (w + hayahash64_internal_rotl(wp, 27))) * K; wp = w;
            w = hayahash64_internal_load64le<bswap>(p + 24);
            h3 = (h3 ^ (w + hayahash64_internal_rotl(wp, 27))) * K; wp = w;
            p += 32; l -= 32;
        }
    } else {
        while (l >= 32) {
            w = hayahash64_internal_load64le<bswap>(p + 0);
            h0 = (h0 ^ (w + hayahash64_internal_rotl(wp, 27))) * K; wp = w;
            w = hayahash64_internal_load64le<bswap>(p + 8);
            h1 = (h1 ^ (w + hayahash64_internal_rotl(wp, 27))) * K; wp = w;
            w = hayahash64_internal_load64le<bswap>(p + 16);
            h2 = (h2 ^ (w + hayahash64_internal_rotl(wp, 27))) * K; wp = w;
            w = hayahash64_internal_load64le<bswap>(p + 24);
            h3 = (h3 ^ (w + hayahash64_internal_rotl(wp, 27))) * K; wp = w;
            p += 32; l -= 32;
        }
    }

    h0 += hayahash64_internal_rotl(wp, 27);
    if (l > 16) {
        h0 = (h0 + hayahash64_internal_injp<bswap>(p + 0)) * K;
        h1 = (h1 + hayahash64_internal_injp<bswap>(p + 8)) * K;
    }
    if (l > 0) {
        h2 = (h2 + hayahash64_internal_injp<bswap>(p + l - 16)) * K;
        h3 = (h3 + hayahash64_internal_injp<bswap>(p + l - 8)) * K;
    }
    uint64_t t0 =
        (h0 ^ hayahash64_internal_rotl_product(h1, 13) ^ lenmix) * K;
    uint64_t t1 =
        (h2 ^ hayahash64_internal_rotl_product(h3, 33)) * K;
    return hayahash128_internal_long(s, t0, t1, K);
}

template <bool bswap>
static void HayaHash64( const void * in, const size_t len, const seed_t seed, void * out ) {
    const uint64_t h = hayahash64<bswap>(in, (std::ptrdiff_t)len, (uint64_t)seed);

    PUT_U64<bswap>(h, (uint8_t *)out, 0);
}

template <bool bswap>
static void HayaHash128( const void * in, const size_t len, const seed_t seed, void * out ) {
    const hayahash128_t h = hayahash128<bswap>(
        in, (std::ptrdiff_t)len, (uint64_t)seed);

    PUT_U64<bswap>(h.lo, (uint8_t *)out, 0);
    PUT_U64<bswap>(h.hi, (uint8_t *)out, 8);
}

//------------------------------------------------------------
// The dispatch shape this translation unit compiled to; every shape
// produces identical digests (see the notes at the top of this file).
#if defined(__wasm__)
  #define HAYAHASH64_IMPL_STR "wasm"
#elif HAYAHASH64_INTERNAL_VECGCC
  #define HAYAHASH64_IMPL_STR "tiers+vecgcc"
#elif HAYAHASH64_INTERNAL_TIERS
  #define HAYAHASH64_IMPL_STR "tiers"
#else
  #define HAYAHASH64_IMPL_STR "compact"
#endif

REGISTER_FAMILY(hayahash,
   $.src_url    = "https://github.com/thevilledev/hayahash",
   $.src_status = HashFamilyInfo::SRC_ACTIVE
 );

REGISTER_HASH(hayahash,
   $.desc            = "hayahash64 v0.5",
   $.impl            = HAYAHASH64_IMPL_STR,
   $.hash_flags      =
         FLAG_HASH_ENDIAN_INDEPENDENT,
   $.impl_flags      =
         FLAG_IMPL_CANONICAL_LE          |
         FLAG_IMPL_LICENSE_PUBLIC_DOMAIN |
         FLAG_IMPL_MULTIPLY_64_64        |
         FLAG_IMPL_ROTATE,
   $.bits            = 64,
   $.verification_LE = 0x65F2AC15,
   $.verification_BE = 0x805DE5C0,
   $.hashfn_native   = HayaHash64<false>,
   $.hashfn_bswap    = HayaHash64<true>
 );

REGISTER_HASH(hayahash128,
   $.desc            = "hayahash128 v0.5 (low word == hayahash64)",
   $.impl            = HAYAHASH64_IMPL_STR,
   $.hash_flags      =
         FLAG_HASH_ENDIAN_INDEPENDENT,
   $.impl_flags      =
         FLAG_IMPL_CANONICAL_LE          |
         FLAG_IMPL_LICENSE_PUBLIC_DOMAIN |
         FLAG_IMPL_MULTIPLY_64_64        |
         FLAG_IMPL_ROTATE,
   $.bits            = 128,
   $.verification_LE = 0x3F0411F4,
   $.verification_BE = 0x46140A64,
   $.hashfn_native   = HayaHash128<false>,
   $.hashfn_bswap    = HayaHash128<true>
 );
