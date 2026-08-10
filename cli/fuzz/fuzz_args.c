// Fuzz hayasum's argument parser.
//
// The input is split on NUL into argv tokens behind a synthetic
// argv[0]. Every token and the diagnostic buffer get their own
// exact-size heap allocation, so ASan traps any read past a token's
// terminator or any write past the end of the caller's buffer.
//
// Invariants checked on every input:
//   - the parser terminates and returns one of its four statuses
//   - argv and the strings it points at are never modified
//   - RUN implies bits in {64,128} and an operand index in [0, argc]
//   - USAGE_ERROR implies a non-empty, NUL-terminated, single-line,
//     printable-ASCII diagnostic that fits the caller's buffer
//   - an errsz of 0 makes the parser write nothing at all
//   - parsing is deterministic

#define HAYASUM_NO_MAIN 1
#include "../hayasum.c"

#define CHECK(cond)                                                        \
	do {                                                               \
		if (!(cond)) {                                             \
			fprintf(stderr, "%s:%d: check failed: %s\n",       \
				__FILE__, __LINE__, #cond);                \
			abort();                                           \
		}                                                          \
	} while (0)

enum { MAX_ARGS = 48 };

static char *dup_token(const void *p, size_t n)
{
	char *s = (char *)malloc(n + 1);

	CHECK(s != NULL);
	if (n != 0)
		memcpy(s, p, n);
	s[n] = '\0';
	return s;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	char *argv[MAX_ARGS + 1];
	char *shadow[MAX_ARGS + 1];
	hayasum_opts o1, o2, o3;
	hayasum_status s1, s2, s3;
	char *err1, *err2, *guard;
	size_t start = 0, i;
	int argc = 0;
	int k;

	argv[argc] = dup_token("hayasum", 7);
	argc++;
	for (i = 0; i <= size && argc < MAX_ARGS; i++) {
		if (i == size || data[i] == '\0') {
			argv[argc] = dup_token(data + start, i - start);
			argc++;
			start = i + 1;
		}
	}
	argv[argc] = NULL;
	for (k = 0; k < argc; k++)
		shadow[k] = dup_token(argv[k], strlen(argv[k]));

	err1 = (char *)malloc(HAYASUM_ERRMAX);
	err2 = (char *)malloc(HAYASUM_ERRMAX);
	guard = (char *)malloc(1);
	CHECK(err1 != NULL && err2 != NULL && guard != NULL);
	memset(err1, 0x7f, HAYASUM_ERRMAX);
	memset(err2, 0x7f, HAYASUM_ERRMAX);
	guard[0] = (char)0x7f;

	s1 = hayasum_parse_args(argc, argv, &o1, err1, HAYASUM_ERRMAX);
	s2 = hayasum_parse_args(argc, argv, &o2, err2, HAYASUM_ERRMAX);
	s3 = hayasum_parse_args(argc, argv, &o3, guard, 0);

	// A zero-size buffer must stay untouched.
	CHECK(guard[0] == (char)0x7f);
	CHECK(s3 == s1);

	CHECK(s1 == HAYASUM_RUN || s1 == HAYASUM_HELP ||
	      s1 == HAYASUM_SHOW_VERSION || s1 == HAYASUM_USAGE_ERROR);
	CHECK(s2 == s1);

	for (k = 0; k < argc; k++)
		CHECK(strcmp(argv[k], shadow[k]) == 0);

	if (s1 == HAYASUM_RUN) {
		CHECK(o1.bits == 64 || o1.bits == 128);
		CHECK(o1.first >= 0 && o1.first <= argc);
		CHECK(o2.bits == o1.bits && o2.seed == o1.seed &&
		      o2.first == o1.first);
	}

	if (s1 == HAYASUM_USAGE_ERROR) {
		size_t n;

		for (n = 0; n < HAYASUM_ERRMAX && err1[n] != '\0'; n++) {
			unsigned char c = (unsigned char)err1[n];

			CHECK(c >= 0x20 && c < 0x7f);
		}
		CHECK(n < HAYASUM_ERRMAX); // terminated inside the buffer
		CHECK(n > 0);              // and says something
		CHECK(memcmp(err1, err2, n + 1) == 0);
	} else {
		// Non-error results leave the buffer as an empty string.
		CHECK(err1[0] == '\0');
	}

	// The quoting helper also has to hold for buffers smaller than a
	// token, which no caller passes today. Drive it directly, into an
	// exact-size allocation so ASan judges the bound rather than a
	// generously sized stack array hiding an off-by-one.
	{
		size_t qsz = (size_t)(size != 0 ? data[0] : 0) % 80u;
		size_t alloc = qsz != 0 ? qsz : 1;
		char *q = (char *)malloc(alloc);

		CHECK(q != NULL);
		memset(q, 0x7f, alloc);
		hayasum_quote(q, qsz, argv[argc - 1]);
		if (qsz == 0) {
			CHECK(q[0] == (char)0x7f); // nothing written at all
		} else {
			size_t n;

			for (n = 0; n < qsz && q[n] != '\0'; n++) {
				unsigned char c = (unsigned char)q[n];

				CHECK(c >= 0x20 && c < 0x7f);
			}
			CHECK(n < qsz); // terminated inside the buffer
		}
		free(q);
	}

	free(guard);
	free(err2);
	free(err1);
	for (k = 0; k < argc; k++) {
		free(shadow[k]);
		free(argv[k]);
	}
	return 0;
}
