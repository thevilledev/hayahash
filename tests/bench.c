// Benchmark: hayahash64 / hayahash128 vs chibihash v1 / v2.
//
// Three views:
//   1. Large-input throughput (GB/s), sizes 32B..1MB.
//   2. Small-input latency: seed depends on previous hash (dependency
//      chain), which is what a hash table lookup chain feels like.
//   3. Small-input throughput: independent hashes, ILP allowed.
//
// All hashes are called through noinline wrappers with identical
// signatures so no candidate benefits from cross-call inlining.
//
// Each result is the median of calibrated, approximately equal-duration
// samples.  Candidate order is rotated (and periodically reversed) between
// samples to spread frequency scaling and thermal drift across candidates.

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
NOINLINE static uint64_t run_haya128(const void *p, ptrdiff_t l, uint64_t s)
{
	hayahash128_t h = hayahash128(p, l, s);
	return h.lo ^ h.hi;
}

typedef uint64_t (*hashfn)(const void *, ptrdiff_t, uint64_t);
static const struct { const char *name; hashfn fn; } HASHES[] = {
	{ "chibihash v1", run_v1 },
	{ "chibihash v2", run_v2 },
	{ "hayahash64", run_haya },
	{ "hayahash128", run_haya128 },
};
enum { NHASH = sizeof(HASHES) / sizeof(HASHES[0]) };

#ifndef BENCH_SAMPLES
#define BENCH_SAMPLES 9
#endif
#ifndef BENCH_SAMPLE_SEC
#define BENCH_SAMPLE_SEC 0.040
#endif
#ifndef BENCH_CALIBRATE_SEC
#define BENCH_CALIBRATE_SEC 0.005
#endif

#if BENCH_SAMPLES < 3 || (BENCH_SAMPLES % 2) == 0
#error "BENCH_SAMPLES must be an odd number of at least 3"
#endif

static double now_sec(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static volatile uint64_t g_sink;

static int cmp_double(const void *a, const void *b)
{
	const double da = *(const double *)a;
	const double db = *(const double *)b;
	return (da > db) - (da < db);
}

static double median(double samples[BENCH_SAMPLES])
{
	double sorted[BENCH_SAMPLES];
	memcpy(sorted, samples, sizeof(sorted));
	qsort(sorted, BENCH_SAMPLES, sizeof(sorted[0]), cmp_double);
	return sorted[BENCH_SAMPLES / 2];
}

// Use every candidate in every order position equally often over each group of
// NHASH samples.  Reversing alternate groups also balances predecessor effects.
static int candidate_at(size_t row, int round, int position)
{
	const int reverse = ((round / NHASH) & 1) ? -1 : 1;
	int f = (int)((row + (size_t)round) % NHASH) + reverse * position;
	f %= NHASH;
	if (f < 0) f += NHASH;
	return f;
}

static size_t scaled_iters(size_t iters, double scale)
{
	if (scale < 1.0) scale = 1.0;
	if (scale > 32.0) scale = 32.0;
	if ((double)iters > (double)SIZE_MAX / scale)
		return SIZE_MAX;
	size_t result = (size_t)((double)iters * scale + 0.5);
	return result > iters ? result : iters + (iters != SIZE_MAX);
}

// ------------------------------------------------- timed batches
static double time_large(hashfn fn, const uint8_t *buf, size_t size,
	size_t iters, uint64_t salt)
{
	uint64_t acc = salt;
	double t0 = now_sec();
	for (size_t i = 0; i < iters; i++)
		acc ^= fn(buf, (ptrdiff_t)size, salt + i);
	double dt = now_sec() - t0;
	g_sink = acc;
	return dt;
}

static double time_small_lat(hashfn fn, const uint8_t *buf, size_t len,
	size_t iters, uint64_t salt)
{
	uint64_t seed = UINT64_C(0x9E3779B97F4A7C15) ^ salt;
	double t0 = now_sec();
	for (size_t i = 0; i < iters; i++)
		seed = fn(buf, (ptrdiff_t)len, seed);
	double dt = now_sec() - t0;
	g_sink = seed;
	return dt;
}

static double time_small_tp(hashfn fn, const uint8_t *buf, size_t len,
	size_t iters, uint64_t salt)
{
	uint64_t acc = salt;
	double t0 = now_sec();
	for (size_t i = 0; i < iters; i++)
		acc ^= fn(buf, (ptrdiff_t)len, salt + i);
	double dt = now_sec() - t0;
	g_sink = acc;
	return dt;
}

typedef double (*batchfn)(hashfn, const uint8_t *, size_t, size_t, uint64_t);

static size_t calibrate(batchfn batch, hashfn fn, const uint8_t *buf,
	size_t len, uint64_t salt)
{
	size_t iters = 1;
	double dt = 0.0;

	// The short calibration interval keeps setup cheap while making timer
	// quantisation insignificant.  Each step is capped to avoid overshooting
	// badly on systems with coarse clocks or initially cold code.
	for (int attempt = 0; attempt < 12; attempt++) {
		dt = batch(fn, buf, len, iters, salt);
		if (dt >= BENCH_CALIBRATE_SEC || iters == SIZE_MAX)
			break;
		double scale = dt > 0.0 ? BENCH_CALIBRATE_SEC / dt : 32.0;
		iters = scaled_iters(iters, scale);
	}

	if (dt <= 0.0)
		return iters;
	return scaled_iters(iters, BENCH_SAMPLE_SEC / dt);
}

static void bench_row(batchfn batch, const uint8_t *buf, size_t len,
	size_t row, double result[NHASH])
{
	size_t iters[NHASH];
	double samples[NHASH][BENCH_SAMPLES];

	// Rotate calibration order too; calibration doubles as per-candidate warmup.
	for (int position = 0; position < NHASH; position++) {
		int f = candidate_at(row, 0, position);
		uint64_t salt = UINT64_C(0xD1B54A32D192ED03) ^
			((uint64_t)row << 32) ^ (uint64_t)f;
		iters[f] = calibrate(batch, HASHES[f].fn, buf, len, salt);
	}

	for (int round = 0; round < BENCH_SAMPLES; round++) {
		for (int position = 0; position < NHASH; position++) {
			int f = candidate_at(row, round, position);
			uint64_t salt = UINT64_C(0x94D049BB133111EB) ^
				((uint64_t)row << 40) ^
				((uint64_t)round << 24) ^ (uint64_t)f;
			samples[f][round] =
				batch(HASHES[f].fn, buf, len, iters[f], salt) /
				(double)iters[f];
		}
	}

	for (int f = 0; f < NHASH; f++)
		result[f] = median(samples[f]);
}

int main(void)
{
	static const size_t LARGE[] = { 32, 48, 63, 64, 96, 128, 192, 256,
		319, 320, 383, 384, 512, 1024, 4096, 16384, 65536, 262144,
		1048576 };
	static const size_t SMALL[] = { 1, 2, 3, 4, 6, 8, 10, 12, 14, 15, 16,
		17, 20, 24, 28, 31, 32, 40, 48, 56, 63, 64, 96, 128, 319, 320,
		383, 384 };

	size_t maxsize = 1048576;
	uint8_t *buf = malloc(maxsize + 64);
	if (buf == NULL) {
		fprintf(stderr, "failed to allocate benchmark buffer\n");
		return 1;
	}
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

	printf("measurement: median of %d calibrated ~%.0f ms samples; "
		"candidate order rotated\n", BENCH_SAMPLES, BENCH_SAMPLE_SEC * 1e3);

	printf("\n== large-input throughput (GB/s) ==\n");
	printf("%10s", "size");
	for (int f = 0; f < NHASH; f++) printf("  %14s", HASHES[f].name);
	printf("\n");
	for (size_t i = 0; i < sizeof(LARGE) / sizeof(LARGE[0]); i++) {
		double result[NHASH];
		bench_row(time_large, buf, LARGE[i], i, result);
		printf("%10zu", LARGE[i]);
		for (int f = 0; f < NHASH; f++)
			printf("  %14.2f",
				(double)LARGE[i] / result[f] / 1e9);
		printf("\n");
	}

	printf("\n== small-input latency (ns/hash, seed-chained) ==\n");
	printf("%10s", "len");
	for (int f = 0; f < NHASH; f++) printf("  %14s", HASHES[f].name);
	printf("\n");
	for (size_t i = 0; i < sizeof(SMALL) / sizeof(SMALL[0]); i++) {
		double result[NHASH];
		bench_row(time_small_lat, buf, SMALL[i], i, result);
		printf("%10zu", SMALL[i]);
		for (int f = 0; f < NHASH; f++)
			printf("  %14.2f", result[f] * 1e9);
		printf("\n");
	}

	printf("\n== small-input throughput (ns/hash, independent) ==\n");
	printf("%10s", "len");
	for (int f = 0; f < NHASH; f++) printf("  %14s", HASHES[f].name);
	printf("\n");
	for (size_t i = 0; i < sizeof(SMALL) / sizeof(SMALL[0]); i++) {
		double result[NHASH];
		bench_row(time_small_tp, buf, SMALL[i], i, result);
		printf("%10zu", SMALL[i]);
		for (int f = 0; f < NHASH; f++)
			printf("  %14.2f", result[f] * 1e9);
		printf("\n");
	}

	free(buf);
	return 0;
}
