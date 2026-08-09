// hayasum - hash files or stdin with hayahash64 / hayahash128.
//
//   hayasum [-s seed] [-b 64|128] [FILE ...]
//   hayasum -h
//
// With no FILE arguments, reads stdin. Digests are printed as lowercase
// hex; 128-bit output is hi||lo (big-endian word order for display,
// matching common 128-bit hex dumps). Exit status is non-zero on I/O or
// usage errors.

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hayahash.h"

enum { CHUNK = 64 * 1024 };

static void usage(FILE *out)
{
	fputs(
		"Usage: hayasum [-s seed] [-b 64|128] [FILE ...]\n"
		"  -s seed   64-bit seed (decimal or 0x-hex); default 0\n"
		"  -b bits   output width: 64 (default) or 128\n"
		"  -h        show this help\n"
		"With no FILE, read stdin. Print one digest line per input.\n",
		out);
}

static int parse_u64(const char *s, uint64_t *out)
{
	char *end = NULL;
	unsigned long long v;

	errno = 0;
	v = strtoull(s, &end, 0);
	if (errno != 0 || end == s || *end != '\0')
		return 0;
	*out = (uint64_t)v;
	return 1;
}

static int hash_stream(FILE *in, uint64_t seed, int bits,
	uint64_t *lo, uint64_t *hi)
{
	hayahash64_state st;
	uint8_t buf[CHUNK];

	hayahash64_init(&st, seed);
	for (;;) {
		size_t n = fread(buf, 1, sizeof(buf), in);
		if (n > 0)
			hayahash64_update(&st, buf, n);
		if (n < sizeof(buf)) {
			if (ferror(in))
				return 0;
			break;
		}
	}
	if (bits == 128) {
		hayahash128_t dig = hayahash128_digest(&st);
		*lo = dig.lo;
		*hi = dig.hi;
	} else {
		*lo = hayahash64_digest(&st);
		*hi = 0;
	}
	return 1;
}

static void print_digest(uint64_t lo, uint64_t hi, int bits,
	const char *label)
{
	if (bits == 128)
		printf("%016" PRIx64 "%016" PRIx64 "  %s\n", hi, lo, label);
	else
		printf("%016" PRIx64 "  %s\n", lo, label);
}

static int hash_path(const char *path, uint64_t seed, int bits)
{
	FILE *in;
	const char *label;
	uint64_t lo = 0, hi = 0;
	int ok;

	if (strcmp(path, "-") == 0) {
		in = stdin;
		label = "-";
	} else {
		in = fopen(path, "rb");
		if (in == NULL) {
			fprintf(stderr, "hayasum: %s: %s\n", path,
				strerror(errno));
			return 0;
		}
		label = path;
	}
	ok = hash_stream(in, seed, bits, &lo, &hi);
	if (in != stdin)
		fclose(in);
	if (!ok) {
		fprintf(stderr, "hayasum: %s: read error\n", label);
		return 0;
	}
	print_digest(lo, hi, bits, label);
	return 1;
}

int main(int argc, char **argv)
{
	uint64_t seed = 0;
	int bits = 64;
	int i = 1;
	int status = 0;

	while (i < argc && argv[i][0] == '-' && argv[i][1] != '\0' &&
	      strcmp(argv[i], "-") != 0) {
		if (strcmp(argv[i], "-h") == 0 ||
		    strcmp(argv[i], "--help") == 0) {
			usage(stdout);
			return 0;
		}
		if (strcmp(argv[i], "-s") == 0) {
			if (i + 1 >= argc || !parse_u64(argv[i + 1], &seed)) {
				usage(stderr);
				return 2;
			}
			i += 2;
			continue;
		}
		if (strcmp(argv[i], "-b") == 0) {
			uint64_t v = 0;
			if (i + 1 >= argc || !parse_u64(argv[i + 1], &v) ||
			    (v != 64 && v != 128)) {
				usage(stderr);
				return 2;
			}
			bits = (int)v;
			i += 2;
			continue;
		}
		fprintf(stderr, "hayasum: unknown option %s\n", argv[i]);
		usage(stderr);
		return 2;
	}

	if (i >= argc)
		return hash_path("-", seed, bits) ? 0 : 1;

	for (; i < argc; i++) {
		if (!hash_path(argv[i], seed, bits))
			status = 1;
	}
	return status;
}
