// Baseline-wasm shootout: hayahash64 vs chibihash v2 (same portability
// class), rapidhash v3 (wide-multiply tier), XXH3-64 and XXH64 (npm
// incumbents). Compiled to wasm32 and driven from Node/V8 by
// driver.mjs; all timing loops run inside wasm so the JS boundary is
// not part of the measurement.
//
// The same file is compiled natively by kat_native.c's build to check
// that every hash is bit-exact across the two targets (see run-kat in
// the Makefile).

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "hayahash.h"
#include "chibihash_v2.c"
#include "rapidhash.h"

// xxhash's wasm-SIMD path needs emscripten's SIMDe-based arm_neon.h,
// which zig cc does not ship; drop xxhash from -msimd128 builds. (The
// KAT diff only works against the default, non-SIMD build anyway.)
#ifndef __wasm_simd128__
#define XXH_INLINE_ALL
#define XXH_NO_STREAM
#define XXH_NO_STDLIB
#include "xxhash.h"
#define HAVE_XXH 1
#endif

#define EXPORT(name) __attribute__((export_name(name), visibility("default")))
#define NOINLINE __attribute__((noinline))

static uint8_t buf[1u << 20];

EXPORT("buf_ptr") uint8_t *buf_ptr(void) { return buf; }
EXPORT("buf_cap") uint32_t buf_cap(void) { return (uint32_t)sizeof(buf); }

// noinline wrappers with one signature so no candidate wins by
// cross-call inlining (same trick as ../bench.c).
NOINLINE static uint64_t run_haya(const uint8_t *p, uint32_t l, uint64_t s)
{ return hayahash64(p, (ptrdiff_t)l, s); }
NOINLINE static uint64_t run_chibi2(const uint8_t *p, uint32_t l, uint64_t s)
{ return chibihash64_v2(p, (ptrdiff_t)l, s); }
NOINLINE static uint64_t run_rapid(const uint8_t *p, uint32_t l, uint64_t s)
{ return rapidhash_withSeed(p, l, s); }
#ifdef HAVE_XXH
NOINLINE static uint64_t run_xxh3(const uint8_t *p, uint32_t l, uint64_t s)
{ return XXH3_64bits_withSeed(p, l, s); }
NOINLINE static uint64_t run_xxh64(const uint8_t *p, uint32_t l, uint64_t s)
{ return XXH64(p, l, s); }
#endif

// Chained: next seed = previous hash (latency, what a lookup chain
// feels like). Independent: accumulate xors, ILP allowed (throughput).
#define DEFINE_BENCH(tag, fn)                                          \
EXPORT("lat_" tag)                                                     \
uint64_t lat_##fn(uint32_t size, uint32_t iters, uint64_t seed)        \
{                                                                      \
	uint64_t h = seed;                                             \
	for (uint32_t i = 0; i < iters; i++)                           \
		h = fn(buf, size, h);                                  \
	return h;                                                      \
}                                                                      \
EXPORT("tp_" tag)                                                      \
uint64_t tp_##fn(uint32_t size, uint32_t iters, uint64_t seed)         \
{                                                                      \
	uint64_t acc = 0;                                              \
	for (uint32_t i = 0; i < iters; i++)                           \
		acc ^= fn(buf, size, seed + i);                        \
	return acc;                                                    \
}                                                                      \
EXPORT("one_" tag)                                                     \
uint64_t one_##fn(uint32_t size, uint64_t seed)                        \
{ return fn(buf, size, seed); }

DEFINE_BENCH("haya", run_haya)
DEFINE_BENCH("chibi2", run_chibi2)
DEFINE_BENCH("rapid", run_rapid)
#ifdef HAVE_XXH
DEFINE_BENCH("xxh3", run_xxh3)
DEFINE_BENCH("xxh64", run_xxh64)
#endif
