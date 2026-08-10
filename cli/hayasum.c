// hayasum - hash files or stdin with hayahash64 / hayahash128.
//
//   hayasum [-s SEED] [-b 64|128] [--] [FILE ...]
//   hayasum -h | -V
//
// With no FILE, or for a FILE of "-", reads stdin. One line per input:
// "<digest>  <name>". Digests are lowercase hex; 128-bit output is
// hi||lo (big-endian word order for display, matching common 128-bit
// hex dumps).
//
// Exit status: 0 success, 1 I/O error (open, read, or write), 2 usage
// error. Output is checked: a digest that fails to reach stdout is
// reported and exits 1 rather than being silently dropped.
//
// Options are only recognized before the first operand; "--" ends them
// explicitly so a file named "-b" can still be hashed. Names containing
// a backslash, newline, or carriage return are printed escaped with the
// line prefixed by a backslash (the GNU coreutils convention), so one
// input always maps to exactly one output line.
//
// The parser, the reader, and the name escaper are side-effect-free
// static functions so the targets in fuzz/ can include this file with
// HAYASUM_NO_MAIN defined and drive them directly.

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hayahash.h"

// Windows opens stdin in text mode, which mangles CRLF and stops at
// Ctrl-Z. A hash of piped bytes has to see the bytes.
#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif

// The build stamps the package version in; a bare "cc hayasum.c" still
// compiles and reports that it does not know which one it is.
#ifndef HAYASUM_VERSION
#define HAYASUM_VERSION "unknown"
#endif

// Read granularity. Overridable so a fuzz build can shrink it and reach
// the multi-iteration and full-buffer paths with small inputs.
#ifndef HAYASUM_CHUNK
#define HAYASUM_CHUNK (64 * 1024)
#endif

enum {
	HAYASUM_EXIT_OK = 0,
	HAYASUM_EXIT_IO = 1,
	HAYASUM_EXIT_USAGE = 2
};

// Longest argv fragment echoed back in a diagnostic, and the buffer the
// finished diagnostic is built in.
enum {
	HAYASUM_TOKMAX = 64,
	HAYASUM_ERRMAX = 256
};

typedef enum {
	HAYASUM_RUN,
	HAYASUM_HELP,
	HAYASUM_SHOW_VERSION,
	HAYASUM_USAGE_ERROR
} hayasum_status;

typedef struct {
	uint64_t seed;
	int bits;  // 64 or 128
	int first; // index in argv of the first operand
} hayasum_opts;

// ------------------------------------------------------------------
// Argument parsing. No I/O, no globals: diagnostics land in the
// caller's buffer, so the whole thing is a pure function of argv.
// ------------------------------------------------------------------

static int hayasum_is_digit(int c)
{
	return c >= '0' && c <= '9';
}

static int hayasum_is_hexdigit(int c)
{
	return hayasum_is_digit(c) || (c >= 'a' && c <= 'f') ||
	       (c >= 'A' && c <= 'F');
}

// Render at most HAYASUM_TOKMAX bytes of src as printable ASCII. argv
// is attacker-controlled in the way that matters here: it can carry
// control characters, and a diagnostic must not replay those into a
// terminal or split itself across lines.
static void hayasum_quote(char *dst, size_t dstsz, const char *src)
{
	size_t cap, i;

	if (dstsz == 0)
		return;
	cap = dstsz - 1;
	if (cap > (size_t)HAYASUM_TOKMAX)
		cap = (size_t)HAYASUM_TOKMAX;
	for (i = 0; i < cap && src[i] != '\0'; i++) {
		unsigned char c = (unsigned char)src[i];

		dst[i] = (c >= 0x20 && c < 0x7f) ? (char)c : '?';
	}
	dst[i] = '\0';
	if (src[i] != '\0' && i >= 3)
		memcpy(dst + i - 3, "...", 3);
}

// Strict 64-bit parse: decimal, or hex behind an explicit 0x/0X prefix.
// strtoull on its own would accept leading whitespace, a leading '+',
// and a leading '-' (silently wrapping "-1" to 2^64-1), and would read
// a leading zero as octal. A seed that quietly means something other
// than what was typed changes every digest, so screen the text first
// and pick the base explicitly.
static int hayasum_parse_u64(const char *s, uint64_t *out)
{
	const char *digits = s;
	unsigned long long v;
	char *end = NULL;
	int base = 10;

	if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
		base = 16;
		digits = s + 2;
		if (!hayasum_is_hexdigit((unsigned char)digits[0]))
			return 0;
	} else if (!hayasum_is_digit((unsigned char)digits[0])) {
		return 0;
	}

	errno = 0;
	v = strtoull(digits, &end, base);
	if (errno == ERANGE || end == digits || *end != '\0')
		return 0;
#if ULLONG_MAX > UINT64_MAX
	if (v > (unsigned long long)UINT64_MAX)
		return 0;
#endif
	*out = (uint64_t)v;
	return 1;
}

// Apply one value-taking option; opt is 's' or 'b'.
static int hayasum_set_option(int opt, const char *v, hayasum_opts *o,
	char *err, size_t errsz)
{
	char tok[HAYASUM_TOKMAX + 1];

	if (opt == 's') {
		if (hayasum_parse_u64(v, &o->seed))
			return 1;
		hayasum_quote(tok, sizeof tok, v);
		snprintf(err, errsz,
			"invalid seed '%s': expected a decimal or 0x-hex "
			"value below 2^64", tok);
		return 0;
	}
	if (strcmp(v, "64") == 0) {
		o->bits = 64;
		return 1;
	}
	if (strcmp(v, "128") == 0) {
		o->bits = 128;
		return 1;
	}
	hayasum_quote(tok, sizeof tok, v);
	snprintf(err, errsz, "invalid width '%s': expected 64 or 128", tok);
	return 0;
}

// Parse argv into *o. On HAYASUM_USAGE_ERROR, err holds a one-line
// printable-ASCII diagnostic. argv is never modified; errsz may be 0.
static hayasum_status hayasum_parse_args(int argc, char *const *argv,
	hayasum_opts *o, char *err, size_t errsz)
{
	char tok[HAYASUM_TOKMAX + 1];
	int i;

	o->seed = 0;
	o->bits = 64;
	o->first = argc > 0 ? 1 : 0;
	if (errsz > 0)
		err[0] = '\0';

	for (i = o->first; i < argc; i++) {
		const char *a = argv[i];
		const char *v;
		int c;

		// A lone "-" is stdin, not an option, and anything not
		// starting with '-' ends the option section (POSIX ordering).
		if (a == NULL || a[0] != '-' || a[1] == '\0')
			break;
		if (a[1] == '-' && a[2] == '\0') {
			i++;
			break;
		}

		if (a[1] == '-') {
			const char *name = a + 2;
			const char *eq = strchr(name, '=');
			size_t nlen = eq ? (size_t)(eq - name) : strlen(name);
			int opt;

			if (nlen == 4 && memcmp(name, "help", 4) == 0)
				opt = 'h';
			else if (nlen == 7 && memcmp(name, "version", 7) == 0)
				opt = 'V';
			else if (nlen == 4 && memcmp(name, "seed", 4) == 0)
				opt = 's';
			else if (nlen == 4 && memcmp(name, "bits", 4) == 0)
				opt = 'b';
			else
				opt = 0;

			if (opt == 0) {
				hayasum_quote(tok, sizeof tok, a);
				snprintf(err, errsz, "unknown option '%s'", tok);
				return HAYASUM_USAGE_ERROR;
			}
			if (opt == 'h' || opt == 'V') {
				if (eq != NULL) {
					snprintf(err, errsz,
						"option '--%s' takes no argument",
						opt == 'h' ? "help" : "version");
					return HAYASUM_USAGE_ERROR;
				}
				return opt == 'h' ? HAYASUM_HELP
				                  : HAYASUM_SHOW_VERSION;
			}
			if (eq != NULL) {
				v = eq + 1;
			} else if (i + 1 < argc && argv[i + 1] != NULL) {
				v = argv[++i];
			} else {
				snprintf(err, errsz,
					"option '--%s' requires an argument",
					opt == 's' ? "seed" : "bits");
				return HAYASUM_USAGE_ERROR;
			}
			if (!hayasum_set_option(opt, v, o, err, errsz))
				return HAYASUM_USAGE_ERROR;
			continue;
		}

		// Short option: -b128, -s 7, -h. Only the first letter is
		// ever acted on, so there is no cluster loop to write: -h
		// and -V end the run, and -s and -b take the rest of the
		// argument as their value.
		c = (unsigned char)a[1];
		if (c == 'h')
			return HAYASUM_HELP;
		if (c == 'V')
			return HAYASUM_SHOW_VERSION;
		if (c != 's' && c != 'b') {
			char one[2];

			one[0] = (char)c;
			one[1] = '\0';
			hayasum_quote(tok, sizeof tok, one);
			snprintf(err, errsz, "unknown option '-%s'", tok);
			return HAYASUM_USAGE_ERROR;
		}
		if (a[2] != '\0') {
			v = a + 2;
		} else if (i + 1 < argc && argv[i + 1] != NULL) {
			v = argv[++i];
		} else {
			snprintf(err, errsz,
				"option '-%c' requires an argument", c);
			return HAYASUM_USAGE_ERROR;
		}
		if (!hayasum_set_option(c, v, o, err, errsz))
			return HAYASUM_USAGE_ERROR;
	}

	o->first = i;
	return HAYASUM_RUN;
}

// ------------------------------------------------------------------
// Output. One input must yield exactly one line, so names that could
// break that are escaped and the line is flagged with a leading
// backslash, the way GNU coreutils checksum tools do it.
// ------------------------------------------------------------------

static int hayasum_label_needs_escape(const char *s)
{
	size_t i;

	for (i = 0; s[i] != '\0'; i++)
		if (s[i] == '\\' || s[i] == '\n' || s[i] == '\r')
			return 1;
	return 0;
}

static void hayasum_write_escaped(FILE *out, const char *s)
{
	size_t i;

	for (i = 0; s[i] != '\0'; i++) {
		switch (s[i]) {
		case '\\':
			fputs("\\\\", out);
			break;
		case '\n':
			fputs("\\n", out);
			break;
		case '\r':
			fputs("\\r", out);
			break;
		default:
			fputc((unsigned char)s[i], out);
			break;
		}
	}
}

static void hayasum_print_digest(FILE *out, uint64_t lo, uint64_t hi,
	int bits, const char *label)
{
	if (hayasum_label_needs_escape(label))
		fputc('\\', out);
	if (bits == 128)
		fprintf(out, "%016" PRIx64 "%016" PRIx64 "  ", hi, lo);
	else
		fprintf(out, "%016" PRIx64 "  ", lo);
	hayasum_write_escaped(out, label);
	fputc('\n', out);
}

// ------------------------------------------------------------------
// Hashing.
// ------------------------------------------------------------------

// hayasum is single-threaded; keeping the read buffer out of the stack
// frame avoids a 64 KiB frame on hosts with small default stacks.
static uint8_t hayasum_iobuf[HAYASUM_CHUNK];

// Returns 1 on success. On failure errno describes the read error.
static int hayasum_hash_stream(FILE *in, uint64_t seed, int bits,
	uint64_t *lo, uint64_t *hi)
{
	hayahash64_state st;

	hayahash64_init(&st, seed);
	for (;;) {
		size_t n = fread(hayasum_iobuf, 1, sizeof hayasum_iobuf, in);

		if (n > 0)
			hayahash64_update(&st, hayasum_iobuf, n);
		if (n == sizeof hayasum_iobuf)
			continue;
		if (ferror(in))
			return 0;
		if (feof(in))
			break;
		if (n == 0) {
			// Short of the request, no EOF, no error: nothing more
			// is coming, but the stream never said it ended. Refuse
			// to publish a digest over a possibly truncated read.
			errno = EIO;
			return 0;
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

// ------------------------------------------------------------------
// Program shell.
// ------------------------------------------------------------------

#ifndef HAYASUM_NO_MAIN

static void hayasum_usage(FILE *out)
{
	fputs(
		"Usage: hayasum [-s SEED] [-b 64|128] [--] [FILE ...]\n"
		"  -s, --seed SEED   64-bit seed, decimal or 0x-hex; default 0\n"
		"  -b, --bits BITS   digest width: 64 (default) or 128\n"
		"  -h, --help        show this help\n"
		"  -V, --version     show the version\n"
		"With no FILE, or for a FILE of \"-\", read stdin. Options are\n"
		"recognized only before the first operand; \"--\" ends them.\n"
		"Exit status: 0 ok, 1 I/O error, 2 usage error.\n",
		out);
}

static void hayasum_perror(const char *label, int err)
{
	fputs("hayasum: ", stderr);
	hayasum_write_escaped(stderr, label);
	fprintf(stderr, ": %s\n", err != 0 ? strerror(err) : "I/O error");
}

static void hayasum_set_binary(FILE *f)
{
#if defined(_WIN32)
	_setmode(_fileno(f), _O_BINARY);
#else
	(void)f;
#endif
}

static int hayasum_hash_path(const char *path, const hayasum_opts *o)
{
	FILE *in;
	uint64_t lo = 0, hi = 0;
	int ok, saved;

	if (strcmp(path, "-") == 0) {
		// A second "-" must not re-report the first one's error, and
		// on Windows stdin has to be switched out of text mode.
		clearerr(stdin);
		hayasum_set_binary(stdin);
		in = stdin;
	} else {
		errno = 0;
		in = fopen(path, "rb");
		if (in == NULL) {
			hayasum_perror(path, errno);
			return 0;
		}
	}

	errno = 0;
	ok = hayasum_hash_stream(in, o->seed, o->bits, &lo, &hi);
	saved = errno;
	if (in != stdin)
		fclose(in);
	if (!ok) {
		// Reading a directory lands here on hosts where fopen accepts
		// one; errno then says so instead of a bare "read error".
		hayasum_perror(path, saved);
		return 0;
	}

	hayasum_print_digest(stdout, lo, hi, o->bits, path);
	return 1;
}

// Flush and report any write error stdio deferred. Without this a full
// disk or a closed stdout drops digest lines and still exits 0. carried
// is the errno captured when the failure was first noticed, or 0.
static int hayasum_stdout_failed(int carried)
{
	int bad;

	errno = 0;
	bad = fflush(stdout) != 0;
	if (errno != 0)
		carried = errno;
	if (ferror(stdout))
		bad = 1;
	if (bad)
		hayasum_perror("standard output", carried);
	return bad;
}

int main(int argc, char **argv)
{
	hayasum_opts o;
	char err[HAYASUM_ERRMAX];
	int status = HAYASUM_EXIT_OK;
	int out_err = 0;
	int i;

	switch (hayasum_parse_args(argc, argv, &o, err, sizeof err)) {
	case HAYASUM_HELP:
		hayasum_usage(stdout);
		return hayasum_stdout_failed(0) ? HAYASUM_EXIT_IO
		                                : HAYASUM_EXIT_OK;
	case HAYASUM_SHOW_VERSION:
		printf("hayasum %s\n", HAYASUM_VERSION);
		return hayasum_stdout_failed(0) ? HAYASUM_EXIT_IO
		                                : HAYASUM_EXIT_OK;
	case HAYASUM_USAGE_ERROR:
		fprintf(stderr, "hayasum: %s\n", err);
		fputs("Try 'hayasum --help' for more information.\n", stderr);
		return HAYASUM_EXIT_USAGE;
	case HAYASUM_RUN:
		break;
	}

	if (o.first >= argc && !hayasum_hash_path("-", &o))
		status = HAYASUM_EXIT_IO;

	for (i = o.first; i < argc; i++) {
		if (!hayasum_hash_path(argv[i], &o))
			status = HAYASUM_EXIT_IO;
		// Once stdout is broken the remaining digests cannot be
		// delivered; stop rather than reading every remaining file.
		if (ferror(stdout)) {
			out_err = errno;
			break;
		}
	}

	if (hayasum_stdout_failed(out_err))
		status = HAYASUM_EXIT_IO;
	return status;
}

#endif // HAYASUM_NO_MAIN
