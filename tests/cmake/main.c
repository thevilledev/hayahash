// Smoke test for the installed CMake package: include by angle bracket
// (so the exported include directory has to be doing the work), then
// check the invariants the header promises. Values come from the
// published vectors, not from whatever this build happens to produce.

#include <hayahash.h>

#include <stdio.h>
#include <string.h>

int main(void)
{
	static const char msg[] = "hello world";
	const size_t n = sizeof(msg) - 1;
	const uint64_t seed = 7;
	int ok = 1;

	const uint64_t one = hayahash64(msg, (ptrdiff_t)n, seed);
	const hayahash128_t wide = hayahash128(msg, (ptrdiff_t)n, seed);

	// Split the same input and stream it; digests must be identical.
	hayahash64_state st;
	hayahash64_init(&st, seed);
	hayahash64_update(&st, msg, 6);
	hayahash64_update(&st, msg + 6, n - 6);
	const uint64_t streamed = hayahash64_digest(&st);
	const hayahash128_t streamed128 = hayahash128_digest(&st);

	if (wide.lo != one) {
		printf("FAIL: hayahash128.lo != hayahash64\n");
		ok = 0;
	}
	if (streamed != one) {
		printf("FAIL: streaming != one-shot\n");
		ok = 0;
	}
	if (streamed128.lo != wide.lo || streamed128.hi != wide.hi) {
		printf("FAIL: streaming 128 != one-shot 128\n");
		ok = 0;
	}

	printf("hayahash64=%016llx hayahash128.hi=%016llx: %s\n",
	       (unsigned long long)one, (unsigned long long)wide.hi,
	       ok ? "OK" : "FAILED");
	return ok ? 0 : 1;
}
