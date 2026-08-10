// Portable replay driver for the libFuzzer targets in this directory.
//
//   fuzz_args-replay FILE|DIR ...
//
// Builds without -fsanitize=fuzzer, so every compiler in CI can run the
// committed corpus as an ordinary regression suite (and, under
// ASan/UBSan, as a memory-safety one). Each input is read whole and
// handed to LLVMFuzzerTestOneInput; a failing invariant aborts.

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(_WIN32)
#include <dirent.h>
#endif

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static unsigned long replayed;

static int replay_file(const char *path)
{
	FILE *f = fopen(path, "rb");
	unsigned char *buf = NULL;
	size_t cap = 0, len = 0;
	int bad = 0;

	if (f == NULL) {
		perror(path);
		return 1;
	}
	for (;;) {
		size_t n;

		if (len == cap) {
			size_t ncap = cap != 0 ? cap * 2 : 4096;
			unsigned char *nb = realloc(buf, ncap);

			if (nb == NULL) {
				fputs("replay: out of memory\n", stderr);
				free(buf);
				fclose(f);
				return 1;
			}
			buf = nb;
			cap = ncap;
		}
		n = fread(buf + len, 1, cap - len, f);
		len += n;
		if (n == 0)
			break;
	}
	if (ferror(f)) {
		perror(path);
		bad = 1;
	}
	fclose(f);
	if (!bad) {
		LLVMFuzzerTestOneInput(buf, len);
		replayed++;
	}
	free(buf);
	return bad;
}

#if !defined(_WIN32)
static int replay_dir(const char *path)
{
	DIR *d = opendir(path);
	struct dirent *e;
	char joined[4096];
	int bad = 0;

	if (d == NULL)
		return -1; // not a directory: caller retries it as a file
	while ((e = readdir(d)) != NULL) {
		if (e->d_name[0] == '.')
			continue;
		if (snprintf(joined, sizeof joined, "%s/%s", path,
			    e->d_name) >= (int)sizeof joined) {
			fprintf(stderr, "replay: path too long: %s/%s\n", path,
				e->d_name);
			bad = 1;
			continue;
		}
		if (replay_file(joined) != 0)
			bad = 1;
	}
	closedir(d);
	return bad;
}
#else
static int replay_dir(const char *path)
{
	(void)path;
	return -1;
}
#endif

int main(int argc, char **argv)
{
	int bad = 0;
	int i;

	if (argc < 2) {
		fprintf(stderr, "usage: %s FILE|DIR ...\n",
			argc > 0 ? argv[0] : "replay");
		return 2;
	}
	for (i = 1; i < argc; i++) {
		int r = replay_dir(argv[i]);

		if (r < 0)
			r = replay_file(argv[i]);
		if (r != 0)
			bad = 1;
	}
	printf("%s: replayed %lu inputs\n", argv[0], replayed);
	return bad;
}
