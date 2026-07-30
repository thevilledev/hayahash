// Quality smoke tests for hayahash64 (with chibihash v1/v2 as controls).
//
// Not a replacement for smhasher3, but catches gross defects fast:
//   1. Strict avalanche criterion (SAC) over input bits, per length.
//   2. SAC over seed bits, per length.
//   3. Exact 64-bit collision counting over structured key sets
//      (sequential, sparse, high-byte-only stripes, zero-extension,
//      repeated blocks, ASCII-ish keys).
//
// Usage: ./quality [haya|v1|v2]   (default: haya)

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hayahash.h"
#include "chibihash_v2.c"

uint64_t chibihash64_v1(const void *keyIn, ptrdiff_t len, uint64_t seed);

typedef uint64_t (*hashfn)(const void *, ptrdiff_t, uint64_t);

static uint64_t fn_haya(const void *p, ptrdiff_t l, uint64_t s) { return hayahash64(p, l, s); }
static uint64_t fn_v1(const void *p, ptrdiff_t l, uint64_t s) { return chibihash64_v1(p, l, s); }
static uint64_t fn_v2(const void *p, ptrdiff_t l, uint64_t s) { return chibihash64_v2(p, l, s); }

// ---------------------------------------------------------------- rng
static uint64_t rng_state = 0x853c49e6748fea9bULL;
static uint64_t rng(void)
{
	uint64_t x = (rng_state += 0x9E3779B97F4A7C15ULL);
	x ^= x >> 30; x *= 0xBF58476D1CE4E5B9ULL;
	x ^= x >> 27; x *= 0x94D049BB133111EBULL;
	x ^= x >> 31;
	return x;
}
static void rng_fill(uint8_t *buf, size_t n)
{
	size_t i = 0;
	for (; i + 8 <= n; i += 8) { uint64_t r = rng(); memcpy(buf + i, &r, 8); }
	for (; i < n; i++) buf[i] = (uint8_t)rng();
}

// ---------------------------------------------------------------- SAC
// Returns worst per-(input-bit, output-bit) bias from 0.5.
// For long inputs only a sample of bit positions is flipped.
static double sac_input(hashfn h, size_t len, int trials, int *ncells)
{
	enum { MAXPOS = 192 };
	size_t nbits = len * 8;
	size_t pos[MAXPOS];
	size_t npos = 0;

	if (nbits <= MAXPOS) {
		for (size_t i = 0; i < nbits; i++) pos[npos++] = i;
	} else {
		// head 64 bits, tail 64 bits, 64 random middle bits
		for (size_t i = 0; i < 64; i++) pos[npos++] = i;
		for (size_t i = 0; i < 64; i++) pos[npos++] = nbits - 64 + i;
		for (size_t i = 0; i < 64; i++) pos[npos++] = 64 + rng() % (nbits - 128);
	}

	static uint32_t count[MAXPOS][64];
	memset(count, 0, sizeof(count));
	uint8_t *key = malloc(len ? len : 1);

	for (int t = 0; t < trials; t++) {
		rng_fill(key, len);
		uint64_t seed = rng();
		uint64_t base = h(key, (ptrdiff_t)len, seed);
		for (size_t pi = 0; pi < npos; pi++) {
			size_t bit = pos[pi];
			key[bit >> 3] ^= (uint8_t)(1u << (bit & 7));
			uint64_t d = base ^ h(key, (ptrdiff_t)len, seed);
			key[bit >> 3] ^= (uint8_t)(1u << (bit & 7));
			for (int ob = 0; ob < 64; ob++)
				count[pi][ob] += (uint32_t)((d >> ob) & 1);
		}
	}
	free(key);

	double worst = 0.0;
	for (size_t pi = 0; pi < npos; pi++)
		for (int ob = 0; ob < 64; ob++) {
			double bias = (double)count[pi][ob] / trials - 0.5;
			if (bias < 0) bias = -bias;
			if (bias > worst) worst = bias;
		}
	*ncells = (int)(npos * 64);
	return worst;
}

static double sac_seed(hashfn h, size_t len, int trials)
{
	static uint32_t count[64][64];
	memset(count, 0, sizeof(count));
	uint8_t *key = malloc(len ? len : 1);

	for (int t = 0; t < trials; t++) {
		rng_fill(key, len);
		uint64_t seed = rng();
		uint64_t base = h(key, (ptrdiff_t)len, seed);
		for (int sb = 0; sb < 64; sb++) {
			uint64_t d = base ^ h(key, (ptrdiff_t)len, seed ^ (1ULL << sb));
			for (int ob = 0; ob < 64; ob++)
				count[sb][ob] += (uint32_t)((d >> ob) & 1);
		}
	}
	free(key);

	double worst = 0.0;
	for (int sb = 0; sb < 64; sb++)
		for (int ob = 0; ob < 64; ob++) {
			double bias = (double)count[sb][ob] / trials - 0.5;
			if (bias < 0) bias = -bias;
			if (bias > worst) worst = bias;
		}
	return worst;
}

// ---------------------------------------------------------- collisions
static int cmp_u64(const void *a, const void *b)
{
	uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
	return x < y ? -1 : x > y;
}

static uint64_t *coll_buf;
static size_t coll_n, coll_cap;

static void coll_reset(void) { coll_n = 0; }
static void coll_add(uint64_t v)
{
	if (coll_n == coll_cap) {
		coll_cap = coll_cap ? coll_cap * 2 : (1u << 20);
		coll_buf = realloc(coll_buf, coll_cap * sizeof(uint64_t));
	}
	coll_buf[coll_n++] = v;
}
static size_t coll_count(void)
{
	qsort(coll_buf, coll_n, sizeof(uint64_t), cmp_u64);
	size_t c = 0;
	for (size_t i = 1; i < coll_n; i++)
		if (coll_buf[i] == coll_buf[i - 1]) c++;
	return c;
}

static size_t test_sequential(hashfn h, size_t len, size_t n)
{
	uint8_t *key = calloc(1, len);
	coll_reset();
	for (size_t i = 0; i < n; i++) {
		memcpy(key, &i, sizeof(i) < len ? sizeof(i) : len);
		coll_add(h(key, (ptrdiff_t)len, 0));
	}
	free(key);
	return coll_count();
}

// All 1-bit and 2-bit keys of the given length.
static size_t test_sparse(hashfn h, size_t len)
{
	size_t nbits = len * 8;
	uint8_t *key = calloc(1, len);
	coll_reset();
	coll_add(h(key, (ptrdiff_t)len, 0));
	for (size_t i = 0; i < nbits; i++) {
		key[i >> 3] ^= (uint8_t)(1u << (i & 7));
		coll_add(h(key, (ptrdiff_t)len, 0));
		for (size_t j = i + 1; j < nbits; j++) {
			key[j >> 3] ^= (uint8_t)(1u << (j & 7));
			coll_add(h(key, (ptrdiff_t)len, 0));
			key[j >> 3] ^= (uint8_t)(1u << (j & 7));
		}
		key[i >> 3] ^= (uint8_t)(1u << (i & 7));
	}
	free(key);
	return coll_count();
}

// Only the top byte of each 8-byte stripe varies: stresses designs where
// high multiply bits never propagate downward in the bulk loop.
static size_t test_highbytes(hashfn h, size_t len, size_t n)
{
	uint8_t *key = calloc(1, len);
	size_t slots = len / 8;
	coll_reset();
	for (size_t i = 0; i < n; i++) {
		uint64_t v = i;
		for (size_t s = 0; s < slots && v; s++, v >>= 8)
			key[s * 8 + 7] = (uint8_t)(v & 0xff);
		coll_add(h(key, (ptrdiff_t)len, 0));
		v = i;
		for (size_t s = 0; s < slots && v; s++, v >>= 8)
			key[s * 8 + 7] = 0;
	}
	free(key);
	return coll_count();
}

// Same content (zeros), every length 0..maxlen: catches bad length mixing.
static size_t test_zero_ext(hashfn h, size_t maxlen)
{
	uint8_t *key = calloc(1, maxlen);
	coll_reset();
	for (size_t l = 0; l <= maxlen; l++)
		coll_add(h(key, (ptrdiff_t)l, 0));
	free(key);
	return coll_count();
}

// 8-byte block repeated: "abababab..." patterns across lanes.
static size_t test_repeat(hashfn h, size_t len, size_t n)
{
	uint8_t *key = malloc(len);
	coll_reset();
	for (size_t i = 0; i < n; i++) {
		uint64_t block = i * 0x9E3779B97F4A7C15ULL;
		for (size_t off = 0; off < len; off += 8)
			memcpy(key + off, &block, (len - off) < 8 ? (len - off) : 8);
		coll_add(h(key, (ptrdiff_t)len, 0));
	}
	free(key);
	return coll_count();
}

// ASCII keys "user_<num>" like real-world hash table keys.
static size_t test_text(hashfn h, size_t n)
{
	char key[32];
	coll_reset();
	for (size_t i = 0; i < n; i++) {
		int len = snprintf(key, sizeof(key), "user_%zu_x", i);
		coll_add(h(key, len, 0));
	}
	return coll_count();
}

// smhasher3 'SeedBlockOffset' shape: zero key with a sparse 4-byte block
// at a fixed offset, over sparse seeds and all lengths 9..31. Caught a
// seed-copy erasure flaw in the short path once.
static size_t test_seedblock(hashfn h)
{
	enum { OFF = 5 };
	uint8_t key[31];
	coll_reset();
	for (int sb1 = 0; sb1 < 64; sb1++)
	for (int sb2 = sb1; sb2 < 64; sb2++) {
		uint64_t seed = (1ULL << sb1) | (1ULL << sb2);
		for (int bb1 = 0; bb1 < 32; bb1++)
		for (int bb2 = bb1; bb2 < 32; bb2++) {
			uint32_t blk = (1u << bb1) | (1u << bb2);
			memset(key, 0, sizeof(key));
			memcpy(key + OFF, &blk, 4);
			for (int len = 9; len <= 31; len++)
				coll_add(h(key, len, seed));
		}
	}
	return coll_count();
}

// smhasher3 'Combination' shape: 1..23 concatenated 4-byte blocks, each
// 0x00000000 or 0x80000000. Top-of-word-only differences; caught a GF(2)
// nullspace in a staggered-load bulk absorb once.
static size_t test_combination(hashfn h)
{
	uint8_t key[92];
	coll_reset();
	for (int nblk = 1; nblk <= 23; nblk++)
		for (uint32_t pat = 0; pat < (1u << nblk); pat++) {
			memset(key, 0, sizeof(key));
			for (int i = 0; i < nblk; i++)
				if (pat & (1u << i)) key[i * 4 + 3] = 0x80;
			coll_add(h(key, nblk * 4, 0x1234));
		}
	return coll_count();
}

int main(int argc, char **argv)
{
	const char *which = argc > 1 ? argv[1] : "haya";
	hashfn h;
	if (!strcmp(which, "v1")) h = fn_v1;
	else if (!strcmp(which, "v2")) h = fn_v2;
	else h = fn_haya;

	printf("== quality: %s ==\n", which);
	int failures = 0;

	// SAC over input bits. 4000 trials -> sigma ~ 0.0079 per cell;
	// warn > 0.03 (~3.8 sigma), fail > 0.045.
	static const size_t lens[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 13, 15,
		16, 17, 20, 24, 25, 31, 32, 33, 40, 48, 63, 64, 65, 80, 96,
		127, 128, 129, 192, 255, 256, 512, 1024 };
	printf("-- SAC input bits (worst |bias|, trials=4000) --\n");
	double worst_in = 0; size_t worst_in_len = 0;
	for (size_t i = 0; i < sizeof(lens) / sizeof(lens[0]); i++) {
		int ncells;
		double w = sac_input(h, lens[i], 4000, &ncells);
		if (w > worst_in) { worst_in = w; worst_in_len = lens[i]; }
		if (w > 0.045) {
			printf("  len %4zu: %.4f  FAIL\n", lens[i], w);
			failures++;
		} else if (w > 0.03) {
			printf("  len %4zu: %.4f  (warn)\n", lens[i], w);
		}
	}
	printf("  worst overall: %.4f @ len %zu\n", worst_in, worst_in_len);

	printf("-- SAC seed bits (worst |bias|, trials=4000) --\n");
	double worst_sd = 0; size_t worst_sd_len = 0;
	static const size_t slens[] = { 0, 1, 4, 8, 12, 16, 24, 32, 48, 64, 100, 128, 256, 1024 };
	for (size_t i = 0; i < sizeof(slens) / sizeof(slens[0]); i++) {
		double w = sac_seed(h, slens[i], 4000);
		if (w > worst_sd) { worst_sd = w; worst_sd_len = slens[i]; }
		if (w > 0.045) {
			printf("  len %4zu: %.4f  FAIL\n", slens[i], w);
			failures++;
		} else if (w > 0.03) {
			printf("  len %4zu: %.4f  (warn)\n", slens[i], w);
		}
	}
	printf("  worst overall: %.4f @ len %zu\n", worst_sd, worst_sd_len);

	// Collisions: any exact 64-bit collision in these set sizes is
	// astronomically unlikely (n^2 / 2^65) for a good hash.
	printf("-- collisions --\n");
	struct { const char *name; size_t c; size_t n; } res[24];
	int nres = 0;

	static const size_t clens[] = { 4, 8, 12, 16, 24, 32, 48, 64, 128, 256 };
	for (size_t i = 0; i < sizeof(clens) / sizeof(clens[0]); i++) {
		size_t c = test_sequential(h, clens[i], 4000000);
		res[nres].name = "sequential"; res[nres].c = c; res[nres].n = clens[i]; nres++;
	}
	static const size_t plens[] = { 16, 32, 64, 128, 256 };
	for (size_t i = 0; i < sizeof(plens) / sizeof(plens[0]); i++) {
		size_t c = test_sparse(h, plens[i]);
		res[nres].name = "sparse-1/2bit"; res[nres].c = c; res[nres].n = plens[i]; nres++;
	}
	res[nres].name = "highbytes"; res[nres].n = 64;
	res[nres].c = test_highbytes(h, 64, 4000000); nres++;
	res[nres].name = "highbytes"; res[nres].n = 256;
	res[nres].c = test_highbytes(h, 256, 4000000); nres++;
	res[nres].name = "zero-extension"; res[nres].n = 100000;
	res[nres].c = test_zero_ext(h, 100000); nres++;
	res[nres].name = "repeat-block"; res[nres].n = 64;
	res[nres].c = test_repeat(h, 64, 4000000); nres++;
	res[nres].name = "repeat-block"; res[nres].n = 192;
	res[nres].c = test_repeat(h, 192, 4000000); nres++;
	res[nres].name = "text"; res[nres].n = 8000000;
	res[nres].c = test_text(h, 8000000); nres++;
	res[nres].name = "seed-block"; res[nres].n = 31;
	res[nres].c = test_seedblock(h); nres++;
	res[nres].name = "combination-hi"; res[nres].n = 92;
	res[nres].c = test_combination(h); nres++;

	for (int i = 0; i < nres; i++) {
		if (res[i].c) {
			printf("  %-16s len/n %6zu: %zu collisions  FAIL\n",
			       res[i].name, res[i].n, res[i].c);
			failures++;
		}
	}
	printf("  %d collision sets, all clean: %s\n", nres,
	       failures ? "NO" : "yes");

	printf("== %s: %s ==\n", which, failures ? "FAILED" : "PASSED");
	return failures ? 1 : 0;
}
