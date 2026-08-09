// Generate the committed known-answer file for the current digest.
//
// Usage:
//   make -C test_vectors regenerate   # rewrite v0.5.0.txt
//   make -C test_vectors check        # require exact match

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include "hayahash.h"

static uint8_t byte_at(size_t i) {
	return (uint8_t)(((i * UINT64_C(0x9E3779B97F4A7C15) + UINT64_C(0x2545F4914F6CDD1D)) >> 56) & 0xFF);
}

static void fill_pattern_a(uint8_t *buf, size_t n) {
	for (size_t i = 0; i < n; i++) buf[i] = byte_at(i);
}

static void fill_pattern_b(uint8_t *buf, size_t n) {
	uint32_t x = 0x9E3779B9u;
	for (size_t i = 0; i < n; i++) {
		x ^= x << 13;
		x ^= x >> 17;
		x ^= x << 5;
		buf[i] = (uint8_t)x;
	}
}

static void fill_boundary(uint8_t *buf, size_t n) {
	for (size_t i = 0; i < n; i++) buf[i] = (uint8_t)i;
}

int main(void) {
	static const size_t lengths[] = {
		0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,20,24,31,32,33,
		47,48,63,64,65,96,127,128,191,192,255,319,320,321,383,512,1023,1024
	};
	static const uint64_t seeds[] = {
		0, UINT64_C(0x9E3779B97F4A7C15), UINT64_C(0xDEADBEEFCAFEBABE)
	};
	uint8_t buf[1024];
	fill_pattern_a(buf, sizeof buf);

	puts("# hayahash known-answer vectors (v0.5.0 digest)");
	puts("# format: len seed_hex h64_hex h128_hi_hex");
	puts("# input: byte(i) = (i*0x9E3779B97F4A7C15 + 0x2545F4914F6CDD1D) >> 56");
	puts("# invariant: h128.lo == h64");
	for (size_t li = 0; li < sizeof lengths/sizeof lengths[0]; li++) {
		size_t len = lengths[li];
		for (size_t si = 0; si < sizeof seeds/sizeof seeds[0]; si++) {
			uint64_t seed = seeds[si];
			uint64_t h64 = hayahash64(buf, (ptrdiff_t)len, seed);
			hayahash128_t h128 = hayahash128(buf, (ptrdiff_t)len, seed);
			if (h128.lo != h64) return 2;
			printf("%zu %016llX %016llX %016llX\n",
				len,
				(unsigned long long)seed,
				(unsigned long long)h64,
				(unsigned long long)h128.hi);
		}
	}

	puts("");
	puts("# named vectors (literal / boundary inputs)");
	puts("# format: name len seed_hex h64_hex h128_hi_hex input_hex");
	{
		uint64_t h64 = hayahash64(NULL, 0, 0);
		hayahash128_t h128 = hayahash128(NULL, 0, 0);
		printf("empty 0 %016llX %016llX %016llX -\n",
			0ULL, (unsigned long long)h64, (unsigned long long)h128.hi);
	}
	{
		const char *s = "hello world";
		uint64_t h64 = hayahash64(s, 11, 0);
		hayahash128_t h128 = hayahash128(s, 11, 0);
		printf("hello_world 11 %016llX %016llX %016llX ",
			0ULL, (unsigned long long)h64, (unsigned long long)h128.hi);
		for (int i = 0; i < 11; i++) printf("%02x", (unsigned char)s[i]);
		puts("");
	}
	uint8_t boundary[33];
	fill_boundary(boundary, 33);
	static const struct { size_t len; uint64_t seed; } named[] = {
		{16, UINT64_C(0x9E3779B97F4A7C15)},
		{17, UINT64_C(0x9E3779B97F4A7C15)},
		{32, UINT64_C(0xDEADBEEFCAFEBABE)},
		{33, UINT64_C(0xDEADBEEFCAFEBABE)},
	};
	for (size_t i = 0; i < sizeof named/sizeof named[0]; i++) {
		uint64_t h64 = hayahash64(boundary, (ptrdiff_t)named[i].len, named[i].seed);
		hayahash128_t h128 = hayahash128(boundary, (ptrdiff_t)named[i].len, named[i].seed);
		printf("boundary_%zu %zu %016llX %016llX %016llX ",
			named[i].len, named[i].len,
			(unsigned long long)named[i].seed,
			(unsigned long long)h64,
			(unsigned long long)h128.hi);
		for (size_t j = 0; j < named[i].len; j++) printf("%02x", boundary[j]);
		puts("");
	}

	puts("");
	puts("# pattern_b vectors (seed 0x1234), same fill as port hash128_boundary tests");
	puts("# format: len seed_hex h64_hex h128_hi_hex");
	uint8_t bufb[1000];
	fill_pattern_b(bufb, sizeof bufb);
	static const size_t blens[] = {0,1,3,7,8,16,17,31,32,63,64,319,320,1000};
	for (size_t i = 0; i < sizeof blens/sizeof blens[0]; i++) {
		size_t len = blens[i];
		uint64_t seed = 0x1234;
		uint64_t h64 = hayahash64(bufb, (ptrdiff_t)len, seed);
		hayahash128_t h128 = hayahash128(bufb, (ptrdiff_t)len, seed);
		printf("%zu %016llX %016llX %016llX\n",
			len,
			(unsigned long long)seed,
			(unsigned long long)h64,
			(unsigned long long)h128.hi);
	}

	/* streaming split checks: one-shot vs 1-byte updates for a few lengths */
	puts("");
	puts("# streaming equality samples (pattern_a, seed 0)");
	puts("# format: len split_pattern h64_hex  (split_pattern=bytewise)");
	for (size_t len = 0; len <= 40; len += 5) {
		hayahash64_state st;
		hayahash64_init(&st, 0);
		for (size_t i = 0; i < len; i++)
			hayahash64_update(&st, buf + i, 1);
		uint64_t streamed = hayahash64_digest(&st);
		uint64_t one = hayahash64(buf, (ptrdiff_t)len, 0);
		if (streamed != one) return 3;
		printf("%zu bytewise %016llX\n", len, (unsigned long long)streamed);
	}
	return 0;
}
