// Native twin of driver.mjs --kat: same buffer fill, same lengths,
// same seed; stdout must match the wasm run byte for byte. Together
// with run-kat's diff this pins every hash to identical output on a
// 64-bit native target and a 32-bit-pointer wasm target.
//
// Built with -DKAT_HAYA_ONLY, the program needs no fetched competitor
// headers and prints only the hayahash lines; CI compiles it that way
// under gcc, clang, MSVC x64, and zig cc -target s390x-linux-musl
// (run via qemu-user), and diffs the output against the committed
// kat_haya.txt for digest conformance. The s390x job is the
// endianness proof: same digests on a real big-endian host.
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

#include "hayahash.h"

#ifndef KAT_HAYA_ONLY
#include "chibihash_v2.c"
#include "rapidhash.h"

#define XXH_INLINE_ALL
#define XXH_NO_STREAM
#define XXH_NO_STDLIB
#include "xxhash.h"
#endif

static uint8_t buf[1u << 20];

typedef uint64_t (*hashfn)(const uint8_t *, uint32_t, uint64_t);
static uint64_t run_haya(const uint8_t *p, uint32_t l, uint64_t s)
{ return hayahash64(p, (ptrdiff_t)l, s); }
static uint64_t run_haya128_lo(const uint8_t *p, uint32_t l, uint64_t s)
{ return hayahash128(p, (ptrdiff_t)l, s).lo; }
static uint64_t run_haya128_hi(const uint8_t *p, uint32_t l, uint64_t s)
{ return hayahash128(p, (ptrdiff_t)l, s).hi; }
#ifndef KAT_HAYA_ONLY
static uint64_t run_chibi2(const uint8_t *p, uint32_t l, uint64_t s)
{ return chibihash64_v2(p, (ptrdiff_t)l, s); }
static uint64_t run_rapid(const uint8_t *p, uint32_t l, uint64_t s)
{ return rapidhash_withSeed(p, l, s); }
static uint64_t run_xxh3(const uint8_t *p, uint32_t l, uint64_t s)
{ return XXH3_64bits_withSeed(p, l, s); }
static uint64_t run_xxh64(const uint8_t *p, uint32_t l, uint64_t s)
{ return XXH64(p, l, s); }
#endif

int main(void)
{
	// xorshift32; keep in sync with the fill loop in driver.mjs.
	uint32_t x = 0x9e3779b9u;
	for (size_t i = 0; i < sizeof(buf); i++) {
		x ^= x << 13; x ^= x >> 17; x ^= x << 5;
		buf[i] = (uint8_t)(x & 0xff);
	}
	static const struct { const char *name; hashfn fn; } H[] = {
		{ "haya", run_haya },
		{ "haya128_lo", run_haya128_lo },
		{ "haya128_hi", run_haya128_hi },
#ifndef KAT_HAYA_ONLY
		{ "chibi2", run_chibi2 },
		{ "rapid", run_rapid }, { "xxh3", run_xxh3 },
		{ "xxh64", run_xxh64 },
#endif
	};
	static const uint32_t LENS[] =
		{ 0, 1, 3, 7, 8, 16, 17, 31, 32, 63, 64, 319, 320, 1000 };
	for (size_t h = 0; h < sizeof(H) / sizeof(H[0]); h++)
		for (size_t i = 0; i < sizeof(LENS) / sizeof(LENS[0]); i++)
			printf("%s %u %016llx\n", H[h].name, LENS[i],
			       (unsigned long long)H[h].fn(buf, LENS[i], 0x1234));
	return 0;
}
