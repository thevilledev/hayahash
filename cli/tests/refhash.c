// refhash - the oracle hayasum is tested against.
//
//   refhash [-b 64|128] [-s SEED] < input
//
// Reads all of stdin into one buffer and prints the digest hayasum
// should print, computed with the *one-shot* entry points. hayasum uses
// the streaming API over fixed-size reads, so any disagreement between
// the two is a bug in hayasum's reader (or in the streaming API).
//
// Deliberately dumb: no streaming, no chunking, no shared code with
// hayasum beyond hayahash.h itself.

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hayahash.h"

int main(int argc, char **argv)
{
	uint8_t *buf = NULL;
	size_t cap = 0, len = 0;
	uint64_t seed = 0;
	int bits = 64;
	int i;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-b") == 0 && i + 1 < argc) {
			bits = atoi(argv[++i]);
		} else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
			seed = strtoull(argv[++i], NULL, 0);
		} else {
			fprintf(stderr, "refhash: bad argument %s\n", argv[i]);
			return 2;
		}
	}
	if (bits != 64 && bits != 128) {
		fprintf(stderr, "refhash: bits must be 64 or 128\n");
		return 2;
	}

	for (;;) {
		size_t n;

		if (len == cap) {
			size_t ncap = cap != 0 ? cap * 2 : 65536;
			uint8_t *nb = (uint8_t *)realloc(buf, ncap);

			if (nb == NULL) {
				fputs("refhash: out of memory\n", stderr);
				free(buf);
				return 1;
			}
			buf = nb;
			cap = ncap;
		}
		n = fread(buf + len, 1, cap - len, stdin);
		len += n;
		if (n == 0)
			break;
	}
	if (ferror(stdin)) {
		fputs("refhash: read error\n", stderr);
		free(buf);
		return 1;
	}

	if (bits == 128) {
		hayahash128_t d = hayahash128(buf, (ptrdiff_t)len, seed);

		printf("%016" PRIx64 "%016" PRIx64 "  -\n", d.hi, d.lo);
	} else {
		printf("%016" PRIx64 "  -\n",
			hayahash64(buf, (ptrdiff_t)len, seed));
	}
	free(buf);
	return fflush(stdout) != 0 ? 1 : 0;
}
