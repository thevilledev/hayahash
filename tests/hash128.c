// Differential checks for hayahash128 and its shared streaming state.

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include "hayahash.h"

static uint64_t rng_state = UINT64_C(0x853C49E6748FEA9B);

static uint64_t rng(void)
{
	uint64_t x = (rng_state += UINT64_C(0x9E3779B97F4A7C15));
	x ^= x >> 30; x *= UINT64_C(0xBF58476D1CE4E5B9);
	x ^= x >> 27; x *= UINT64_C(0x94D049BB133111EB);
	return x ^ (x >> 31);
}

static int same128(hayahash128_t a, hayahash128_t b)
{
	return a.lo == b.lo && a.hi == b.hi;
}

static int known_answers(void)
{
	static const uint8_t boundary[33] = {
		0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
		16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
		32
	};
	static const struct {
		const void *p;
		ptrdiff_t len;
		uint64_t seed, lo, hi;
	} vectors[] = {
		{ NULL, 0, 0,
		  UINT64_C(0x68AC507CF298CA3F), UINT64_C(0xACE2141F6BA30868) },
		{ "hello world", 11, 0,
		  UINT64_C(0x4524B96611BFC05A), UINT64_C(0x41D8671459F6EEF8) },
		{ boundary, 16, UINT64_C(0x9E3779B97F4A7C15),
		  UINT64_C(0x6B249DFB8D21BBAB), UINT64_C(0x5B3D9925DDEF47CF) },
		{ boundary, 17, UINT64_C(0x9E3779B97F4A7C15),
		  UINT64_C(0xF75B8B20CC22F810), UINT64_C(0xBD21B1C2645A8D61) },
		{ boundary, 32, UINT64_C(0xDEADBEEFCAFEBABE),
		  UINT64_C(0x5B105340D96724A6), UINT64_C(0x7067F25E475D03B0) },
		{ boundary, 33, UINT64_C(0xDEADBEEFCAFEBABE),
		  UINT64_C(0x9D2FD7EA13186F8E), UINT64_C(0x6DADDAC9AA85E879) }
	};

	for (size_t i = 0; i < sizeof(vectors) / sizeof(vectors[0]); i++) {
		hayahash128_t got = hayahash128(
			vectors[i].p, vectors[i].len, vectors[i].seed);
		if (got.lo != vectors[i].lo || got.hi != vectors[i].hi) {
			fprintf(stderr, "known-answer mismatch: vector=%zu\n", i);
			return 0;
		}
	}
	return 1;
}

static hayahash128_t
streamed(const uint8_t *buf, size_t len, uint64_t seed, unsigned pattern)
{
	hayahash128_state st;
	hayahash128_init(&st, seed);
	size_t off = 0;
	while (off < len) {
		size_t n;
		switch (pattern) {
		case 0: n = 1; break;
		case 1: n = 7; break;
		case 2: n = 64; break;
		case 3: n = 1 + (size_t)(rng() % 257); break;
		default: n = len; break;
		}
		if (n > len - off)
			n = len - off;
		hayahash128_update(&st, buf + off, n);
		off += n;
	}
	return hayahash128_digest(&st);
}

static int check(const uint8_t *buf, size_t len, uint64_t seed,
		 unsigned first_pattern)
{
	hayahash128_t one = hayahash128(buf, (ptrdiff_t)len, seed);
	uint64_t h64 = hayahash64(buf, (ptrdiff_t)len, seed);
	if (one.lo != h64) {
		fprintf(stderr, "lo mismatch: len=%zu seed=%016llx\n", len,
		        (unsigned long long)seed);
		return 0;
	}
	for (unsigned pattern = first_pattern; pattern < 5; pattern++) {
		hayahash128_t got = streamed(buf, len, seed, pattern);
		if (!same128(got, one)) {
			fprintf(stderr,
			        "stream mismatch: len=%zu seed=%016llx pattern=%u\n",
			        len, (unsigned long long)seed, pattern);
			return 0;
		}
	}
	return 1;
}

int main(void)
{
	enum { MAXLEN = 20000 };
	if (!known_answers())
		return 1;
	uint8_t *buf = (uint8_t *)malloc(MAXLEN);
	if (buf == NULL)
		return 2;
	for (size_t i = 0; i < MAXLEN; i++)
		buf[i] = (uint8_t)rng();

	static const uint64_t seeds[] = {
		UINT64_C(0),
		UINT64_C(0x9E3779B97F4A7C15),
		UINT64_C(0xDEADBEEFCAFEBABE)
	};
	for (size_t len = 0; len <= 512; len++)
		for (size_t s = 0; s < sizeof(seeds) / sizeof(seeds[0]); s++)
			if (!check(buf, len, seeds[s], 0)) {
				free(buf);
				return 1;
			}

	for (unsigned trial = 0; trial < 1000; trial++) {
		size_t len = (size_t)(rng() % MAXLEN);
		// One- and seven-byte updates are covered exhaustively above,
		// including across the 320-byte bulk cutoff. Larger random
		// cases use 64-byte, random, and whole-input splits.
		if (!check(buf, len, rng(), 2)) {
			free(buf);
			return 1;
		}
	}

	// Digest is non-mutating: finalize a prefix twice, continue the
	// same state, and then finalize the complete input.
	for (unsigned trial = 0; trial < 1000; trial++) {
		size_t len = (size_t)(rng() % MAXLEN);
		size_t split = len ? (size_t)(rng() % len) : 0;
		uint64_t seed = rng();
		hayahash64_state st;
		hayahash64_init(&st, seed);
		hayahash64_update(&st, buf, split);
		hayahash128_t prefix = hayahash128_digest(&st);
		if (!same128(prefix, hayahash128_digest(&st)) ||
		    !same128(prefix, hayahash128(buf, (ptrdiff_t)split, seed))) {
			fprintf(stderr, "non-mutating digest mismatch: len=%zu\n", split);
			free(buf);
			return 1;
		}
		hayahash64_update(&st, buf + split, len - split);
		if (!same128(hayahash128_digest(&st),
		             hayahash128(buf, (ptrdiff_t)len, seed))) {
			fprintf(stderr, "continued digest mismatch: len=%zu\n", len);
			free(buf);
			return 1;
		}
	}

	free(buf);
	puts("hash128: one-shot/streaming and lo==hayahash64 checks passed");
	return 0;
}
