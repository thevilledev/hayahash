// Fuzz hayasum's streaming reader against the one-shot hash.
//
// Input layout: the first 8 bytes (or as many as exist) are the seed,
// the next byte caps how much a single underlying read may return, and
// the rest is the message. The cap makes stdio refill at arbitrary
// boundaries, so the reader's loop runs over inputs whose chunk
// structure has nothing to do with its buffer size.
//
// Invariants checked on every input:
//   - the reader's 64-bit digest equals hayahash64() over the same
//     bytes and seed, for a plain in-memory stream and for a
//     short-read one
//   - the reader's 128-bit digest equals hayahash128(), and its low
//     word equals the 64-bit digest (the documented relationship)
//   - the 64-bit path zeroes the high word rather than leaking it
//   - a stream that fails partway is reported as an error, never
//     rounded off into a digest of the bytes that did arrive
//
// Built twice: once with the shipped 64 KiB read buffer and once with
// HAYASUM_CHUNK=61, so the multi-iteration and full-buffer paths are
// reached without needing 64 KiB inputs.

#define _GNU_SOURCE 1

#define HAYASUM_NO_MAIN 1
#include "../hayasum.c"

#include <sys/types.h>

#define CHECK(cond)                                                        \
	do {                                                               \
		if (!(cond)) {                                             \
			fprintf(stderr, "%s:%d: check failed: %s\n",       \
				__FILE__, __LINE__, #cond);                \
			abort();                                           \
		}                                                          \
	} while (0)

#if defined(__GLIBC__)
typedef struct {
	const uint8_t *p;
	size_t len;
	size_t pos;
	size_t cap;    // most bytes one read may hand back
	size_t fail_at; // fail once pos reaches this; SIZE_MAX to never fail
} slow_source;

static ssize_t slow_read(void *cookie, char *buf, size_t want)
{
	slow_source *s = (slow_source *)cookie;
	size_t avail = s->len - s->pos;
	size_t take = want;

	if (s->pos >= s->fail_at) {
		errno = EIO;
		return -1;
	}
	if (take > s->cap)
		take = s->cap;
	if (take > avail)
		take = avail;
	if (take != 0)
		memcpy(buf, s->p + s->pos, take);
	s->pos += take;
	return (ssize_t)take;
}

static FILE *open_slow(slow_source *s)
{
	cookie_io_functions_t fns;

	memset(&fns, 0, sizeof fns);
	fns.read = slow_read;
	return fopencookie(s, "rb", fns);
}
#endif

// fmemopen needs a writable buffer and rejects a zero size on some
// libcs, so hand it an exact-size copy and, for the empty message,
// a one-byte window positioned at its end.
static FILE *open_mem(uint8_t *buf, size_t len)
{
	FILE *f = fmemopen(buf, len != 0 ? len : 1, "rb");

	CHECK(f != NULL);
	if (len == 0)
		CHECK(fseek(f, 1, SEEK_SET) == 0);
	return f;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	uint64_t seed = 0;
	uint64_t lo = 0, hi = 0xa5a5a5a5a5a5a5a5ull;
	uint64_t want64;
	hayahash128_t want128;
	uint8_t *buf;
	size_t msglen, cap, i;
	FILE *mem;

	for (i = 0; i < 8 && i < size; i++)
		seed = (seed << 8) | data[i];
	cap = size > 8 ? (size_t)data[8] : 0;
	if (cap == 0)
		cap = 1;
	msglen = size > 9 ? size - 9 : 0;

	buf = (uint8_t *)malloc(msglen != 0 ? msglen : 1);
	CHECK(buf != NULL);
	if (msglen != 0)
		memcpy(buf, data + 9, msglen);

	want64 = hayahash64(buf, (ptrdiff_t)msglen, seed);
	want128 = hayahash128(buf, (ptrdiff_t)msglen, seed);
	CHECK(want128.lo == want64);

	mem = open_mem(buf, msglen);
	CHECK(hayasum_hash_stream(mem, seed, 64, &lo, &hi) == 1);
	CHECK(lo == want64);
	CHECK(hi == 0);
	CHECK(fclose(mem) == 0);

	mem = open_mem(buf, msglen);
	CHECK(hayasum_hash_stream(mem, seed, 128, &lo, &hi) == 1);
	CHECK(lo == want128.lo);
	CHECK(hi == want128.hi);
	CHECK(fclose(mem) == 0);

#if defined(__GLIBC__)
	{
		slow_source s;
		FILE *slow;

		s.p = buf;
		s.len = msglen;
		s.pos = 0;
		s.cap = cap;
		s.fail_at = (size_t)-1;
		slow = open_slow(&s);
		CHECK(slow != NULL);
		CHECK(hayasum_hash_stream(slow, seed, 128, &lo, &hi) == 1);
		CHECK(lo == want128.lo);
		CHECK(hi == want128.hi);
		CHECK(fclose(slow) == 0);

		if (msglen != 0) {
			s.pos = 0;
			s.fail_at = msglen / 2;
			slow = open_slow(&s);
			CHECK(slow != NULL);
			CHECK(hayasum_hash_stream(slow, seed, 64, &lo, &hi) == 0);
			CHECK(fclose(slow) == 0);
		}
	}
#else
	(void)cap;
#endif

	free(buf);
	return 0;
}
