// Benchmark: hayahash64 vs chibihash v1 / v2 (C implementations).
//
// Three views:
//   1. Large-input throughput (GB/s), sizes 32B..1MB.
//   2. Small-input latency: seed depends on previous hash (dependency
//      chain), which is what a hash table lookup chain feels like.
//   3. Small-input throughput: independent hashes, ILP allowed.
//
// All hashes are called through noinline wrappers with identical
// signatures so no candidate benefits from cross-call inlining.

// glibc hides clock_gettime()/CLOCK_MONOTONIC_RAW under strict -std=c11
// unless a feature-test macro asks for them.
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef CLOCK_MONOTONIC_RAW
#define CLOCK_MONOTONIC_RAW CLOCK_MONOTONIC
#endif

#include "hayahash.h"
#include "chibihash_v2.c"

uint64_t chibihash64_v1(const void *keyIn, ptrdiff_t len, uint64_t seed);

#define NOINLINE __attribute__((noinline))

NOINLINE static uint64_t run_v1(const void *p, ptrdiff_t l, uint64_t s)
{ return chibihash64_v1(p, l, s); }
NOINLINE static uint64_t run_v2(const void *p, ptrdiff_t l, uint64_t s)
{ return chibihash64_v2(p, l, s); }
NOINLINE static uint64_t run_haya(const void *p, ptrdiff_t l, uint64_t s)
{ return hayahash64(p, l, s); }

typedef uint64_t (*hashfn)(const void *, ptrdiff_t, uint64_t);
static const struct { const char *name; hashfn fn; } HASHES[] = {
	{ "chibihash v1", run_v1 },
	{ "chibihash v2", run_v2 },
	{ "hayahash", run_haya },
};
enum { NHASH = sizeof(HASHES) / sizeof(HASHES[0]) };

static double now_sec(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static volatile uint64_t g_sink;

// ------------------------------------------------- large throughput
static double bench_large(hashfn fn, const uint8_t *buf, size_t size)
{
	// aim for ~24MB per round, at least 8 iterations
	size_t iters = (24u << 20) / size;
	if (iters < 8) iters = 8;
	double best = 1e300;
	for (int round = 0; round < 6; round++) {
		uint64_t acc = 0;
		double t0 = now_sec();
		for (size_t i = 0; i < iters; i++)
			acc ^= fn(buf, (ptrdiff_t)size, i);
		double dt = now_sec() - t0;
		g_sink = acc;
		double gbps = (double)size * (double)iters / dt / 1e9;
		if (dt > 0 && 1.0 / gbps < best) best = 1.0 / gbps;
	}
	return 1.0 / best;
}

// ------------------------------------------------- small latency
static double bench_small_lat(hashfn fn, const uint8_t *buf, size_t len, size_t iters)
{
	uint64_t seed = 0x1234;
	// warmup
	for (size_t i = 0; i < iters / 8; i++)
		seed = fn(buf, (ptrdiff_t)len, seed);
	double best = 1e300;
	for (int round = 0; round < 4; round++) {
		double t0 = now_sec();
		for (size_t i = 0; i < iters; i++)
			seed = fn(buf, (ptrdiff_t)len, seed);
		double dt = now_sec() - t0;
		if (dt < best) best = dt;
	}
	g_sink = seed;
	return best / (double)iters * 1e9; // ns/hash
}

// ------------------------------------------------- small throughput
static double bench_small_tp(hashfn fn, const uint8_t *buf, size_t len, size_t iters)
{
	uint64_t acc = 0;
	for (size_t i = 0; i < iters / 8; i++)
		acc ^= fn(buf, (ptrdiff_t)len, i);
	double best = 1e300;
	for (int round = 0; round < 4; round++) {
		double t0 = now_sec();
		for (size_t i = 0; i < iters; i++)
			acc ^= fn(buf, (ptrdiff_t)len, i);
		double dt = now_sec() - t0;
		if (dt < best) best = dt;
	}
	g_sink = acc;
	return best / (double)iters * 1e9; // ns/hash
}

int main(void)
{
	static const size_t LARGE[] = { 32, 48, 64, 96, 128, 192, 256, 512,
		1024, 4096, 16384, 65536, 262144, 1048576 };
	static const size_t SMALL[] = { 1, 2, 3, 4, 6, 8, 10, 12, 14, 16,
		20, 24, 28, 32, 40, 48, 56, 64, 96, 128 };

	size_t maxsize = 1048576;
	uint8_t *buf = malloc(maxsize + 64);
	uint64_t x = 0x2545F4914F6CDD1DULL;
	for (size_t i = 0; i < maxsize + 64; i++) {
		x ^= x << 13; x ^= x >> 7; x ^= x << 17;
		buf[i] = (uint8_t)x;
	}

	printf("cpu warmup...\n");
	{
		double t0 = now_sec();
		uint64_t acc = 0;
		while (now_sec() - t0 < 1.0)
			acc ^= run_haya(buf, 4096, acc);
		g_sink = acc;
	}

	printf("\n== large-input throughput (GB/s, best of 6) ==\n");
	printf("%10s", "size");
	for (int f = 0; f < NHASH; f++) printf("  %14s", HASHES[f].name);
	printf("\n");
	for (size_t i = 0; i < sizeof(LARGE) / sizeof(LARGE[0]); i++) {
		printf("%10zu", LARGE[i]);
		for (int f = 0; f < NHASH; f++)
			printf("  %14.2f", bench_large(HASHES[f].fn, buf, LARGE[i]));
		printf("\n");
	}

	printf("\n== small-input latency (ns/hash, seed-chained, best of 4) ==\n");
	printf("%10s", "len");
	for (int f = 0; f < NHASH; f++) printf("  %14s", HASHES[f].name);
	printf("\n");
	for (size_t i = 0; i < sizeof(SMALL) / sizeof(SMALL[0]); i++) {
		printf("%10zu", SMALL[i]);
		for (int f = 0; f < NHASH; f++)
			printf("  %14.2f", bench_small_lat(HASHES[f].fn, buf, SMALL[i], 4000000));
		printf("\n");
	}

	printf("\n== small-input throughput (ns/hash, independent, best of 4) ==\n");
	printf("%10s", "len");
	for (int f = 0; f < NHASH; f++) printf("  %14s", HASHES[f].name);
	printf("\n");
	for (size_t i = 0; i < sizeof(SMALL) / sizeof(SMALL[0]); i++) {
		printf("%10zu", SMALL[i]);
		for (int f = 0; f < NHASH; f++)
			printf("  %14.2f", bench_small_tp(HASHES[f].fn, buf, SMALL[i], 4000000));
		printf("\n");
	}

	free(buf);
	return 0;
}
