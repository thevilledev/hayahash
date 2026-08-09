// Generates a reproducible randomized corpus from the C reference.
//
// File format (all integers little-endian):
//   8 bytes  magic "HAYAFZ02"
//   u32      case count
//   u64      corpus PRNG seed
//   repeated case count times:
//     u32    input length
//     u64    hash seed
//     u64    expected hayahash128 low word (also hayahash64)
//     u64    expected hayahash128 high word
//     u8[]   input bytes

// This is free and unencumbered software released into the public domain.

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../hayahash.h"

enum {
	EXHAUSTIVE_LAST_LENGTH = 384,
	MIN_CASES = 406,
	DEFAULT_CASES = 4096,
	MAX_CASES = 1000000,
	MAX_RANDOM_LENGTH = 131073
};

static const uint32_t large_edges[] = {
	511, 512, 513,
	1023, 1024, 1025,
	4095, 4096, 4097,
	16383, 16384, 16385,
	32767, 32768, 32769,
	65535, 65536, 65537,
	131071, 131072, 131073
};

// Branch and loop boundaries used by the reference and the ports. Random
// cases are concentrated around these in addition to the exhaustive 0..384
// sweep, which crosses every short, tail, mid-loop, tier, and bulk dispatch.
static const uint32_t dispatch_boundaries[] = {
	1, 4, 8, 16, 17, 32, 48, 64, 80, 96, 112, 128,
	144, 160, 176, 192, 224, 256, 288, 320, 384, 512,
	1024, 4096, 16384, 32768, 65536, 131072
};

struct prng {
	uint64_t state;
};

// SplitMix64 is deliberately specified here rather than using libc rand(),
// so a logged seed reproduces the exact corpus on every host.
static uint64_t prng_next(struct prng *p)
{
	uint64_t z = (p->state += UINT64_C(0x9E3779B97F4A7C15));
	z = (z ^ (z >> 30)) * UINT64_C(0xBF58476D1CE4E5B9);
	z = (z ^ (z >> 27)) * UINT64_C(0x94D049BB133111EB);
	return z ^ (z >> 31);
}

static int write_bytes(FILE *f, const void *p, size_t n)
{
	if (n == 0)
		return 1;
	return fwrite(p, 1, n, f) == n;
}

static int write_u32le(FILE *f, uint32_t x)
{
	uint8_t b[4] = {
		(uint8_t)x, (uint8_t)(x >> 8),
		(uint8_t)(x >> 16), (uint8_t)(x >> 24)
	};
	return write_bytes(f, b, sizeof(b));
}

static int write_u64le(FILE *f, uint64_t x)
{
	uint8_t b[8];
	for (unsigned int i = 0; i < 8; ++i)
		b[i] = (uint8_t)(x >> (i * 8));
	return write_bytes(f, b, sizeof(b));
}

static uint64_t parse_u64(const char *s, const char *name)
{
	char *end;
	errno = 0;
	unsigned long long value = strtoull(s, &end, 0);
	if (errno != 0 || end == s || *end != '\0') {
		fprintf(stderr, "invalid %s: %s\n", name, s);
		exit(2);
	}
	return (uint64_t)value;
}

static uint32_t parse_count(const char *s)
{
	uint64_t value = parse_u64(s, "case count");
	if (value < MIN_CASES || value > MAX_CASES) {
		fprintf(stderr, "case count must be between %d and %d\n",
		        MIN_CASES, MAX_CASES);
		exit(2);
	}
	return (uint32_t)value;
}

static uint32_t case_length(uint32_t case_index, struct prng *p)
{
	if (case_index <= EXHAUSTIVE_LAST_LENGTH)
		return case_index;

	uint32_t edge_index = case_index - (EXHAUSTIVE_LAST_LENGTH + 1);
	if (edge_index < sizeof(large_edges) / sizeof(large_edges[0]))
		return large_edges[edge_index];

	uint64_t r = prng_next(p);
	switch (r & 15) {
	case 0: case 1: case 2: case 3: case 4: case 5:
		return (uint32_t)(prng_next(p) % 385);
	case 6: case 7: case 8: case 9: case 10:
		return (uint32_t)(prng_next(p) % 4097);
	case 11: case 12: case 13:
		return (uint32_t)(prng_next(p) % 16385);
	case 14: {
		uint32_t boundary = dispatch_boundaries[
			prng_next(p) % (sizeof(dispatch_boundaries) /
			                sizeof(dispatch_boundaries[0]))];
		int32_t delta = (int32_t)(prng_next(p) % 15) - 7;
		if (delta < 0 && boundary < (uint32_t)-delta)
			return 0;
		uint32_t candidate = (uint32_t)((int64_t)boundary + delta);
		return candidate > MAX_RANDOM_LENGTH ? MAX_RANDOM_LENGTH : candidate;
	}
	default:
		return (uint32_t)(prng_next(p) % (MAX_RANDOM_LENGTH + 1));
	}
}

static void fill_random(uint8_t *buf, uint32_t len, struct prng *p)
{
	uint32_t off = 0;
	while (off < len) {
		uint64_t word = prng_next(p);
		for (unsigned int i = 0; i < 8 && off < len; ++i, ++off)
			buf[off] = (uint8_t)(word >> (i * 8));
	}
}

int main(int argc, char **argv)
{
	if (argc < 3 || argc > 4) {
		fprintf(stderr, "usage: %s OUTPUT PRNG_SEED [CASE_COUNT]\n", argv[0]);
		return 2;
	}

	const char *output_path = argv[1];
	uint64_t master_seed = parse_u64(argv[2], "PRNG seed");
	uint32_t case_count = argc == 4 ? parse_count(argv[3]) : DEFAULT_CASES;
	struct prng random = { master_seed };
	uint8_t *input = NULL;
	size_t capacity = 0;
	uint32_t largest = 0;
	FILE *output = fopen(output_path, "wb");
	if (output == NULL) {
		fprintf(stderr, "cannot open %s: %s\n", output_path, strerror(errno));
		return 1;
	}

	int ok = write_bytes(output, "HAYAFZ02", 8) &&
	         write_u32le(output, case_count) &&
	         write_u64le(output, master_seed);
	for (uint32_t i = 0; ok && i < case_count; ++i) {
		uint32_t len = case_length(i, &random);
		uint64_t hash_seed = prng_next(&random);
		if (len > capacity) {
			uint8_t *grown = realloc(input, len);
			if (grown == NULL) {
				fprintf(stderr, "cannot allocate %" PRIu32 " input bytes\n", len);
				ok = 0;
				break;
			}
			input = grown;
			capacity = len;
		}
		fill_random(input, len, &random);
		hayahash128_t expected = hayahash128(input, len, hash_seed);
		ok = write_u32le(output, len) &&
		     write_u64le(output, hash_seed) &&
		     write_u64le(output, expected.lo) &&
		     write_u64le(output, expected.hi) &&
		     write_bytes(output, input, len);
		if (len > largest)
			largest = len;
	}

	if (fclose(output) != 0)
		ok = 0;
	free(input);
	if (!ok) {
		fprintf(stderr, "failed to write corpus %s\n", output_path);
		return 1;
	}

	fprintf(stderr,
	        "generated %" PRIu32 " differential cases: "
	        "PRNG seed=0x%016" PRIx64 ", largest input=%" PRIu32 " bytes\n",
	        case_count, master_seed, largest);
	return 0;
}
