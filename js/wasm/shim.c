// wasm32-freestanding shim around the reference implementation.
//
// The npm package does not port the algorithm to JavaScript for its
// fast path; it compiles the untouched reference header (hayahash.h
// at the repository root) to WebAssembly, so the wasm digest is the
// C digest by construction. The JS wrapper writes input bytes into
// linear memory at __heap_base (exported by the linker along with
// the memory itself) and calls the export below.
//
// This is free and unencumbered software released into the public
// domain. For more information, please refer to <https://unlicense.org/>

#include <stdint.h>
#include <stddef.h>

#include "../../hayahash.h"

// Freestanding build, no libc. The fixed-size memcpy loads in
// hayahash.h compile to plain wasm loads at -O1 and above, but C
// still requires the symbol to exist in case the compiler emits a
// call. no_builtin stops clang from recognizing the loop and turning
// it back into a memcpy call. If unreferenced, the linker's default
// --gc-sections drops it.
__attribute__((no_builtin("memcpy")))
void *memcpy(void *dst, const void *src, size_t n)
{
	unsigned char *d = dst;
	const unsigned char *s = src;
	for (size_t i = 0; i < n; i++) {
		d[i] = s[i];
	}
	return dst;
}

// Referenced by the header's streaming buffer management, which the
// streaming exports below now reach.
__attribute__((no_builtin("memmove")))
void *memmove(void *dst, const void *src, size_t n)
{
	unsigned char *d = dst;
	const unsigned char *s = src;
	if (d < s) {
		for (size_t i = 0; i < n; i++) {
			d[i] = s[i];
		}
	} else {
		for (size_t i = n; i > 0; i--) {
			d[i - 1] = s[i - 1];
		}
	}
	return dst;
}

// len is u32 and the JS wrapper caps it at 2^31 - 1 so the cast to
// the 32-bit wasm ptrdiff_t is always in range.
__attribute__((export_name("hayahash64")))
uint64_t wasm_hayahash64(const uint8_t *p, uint32_t len, uint64_t seed)
{
	return hayahash64(p, (ptrdiff_t)len, seed);
}

// Streaming. The state lives in the caller's linear memory rather than
// in a module-static, so several hashers can be live at once and the
// JS side owns placement and lifetime. State layout is opaque to JS:
// it only ever moves the bytes around, never interprets them.
__attribute__((export_name("hayahash_state_size")))
uint32_t wasm_hayahash_state_size(void)
{
	return (uint32_t)sizeof(hayahash64_state);
}

__attribute__((export_name("hayahash_stream_init")))
void wasm_hayahash_stream_init(hayahash64_state *st, uint64_t seed)
{
	hayahash64_init(st, seed);
}

__attribute__((export_name("hayahash_stream_update")))
void wasm_hayahash_stream_update(hayahash64_state *st, const uint8_t *p,
                                 uint32_t n)
{
	hayahash64_update(st, p, (size_t)n);
}

// Both words at once: the low word is hayahash64_digest(st), so one
// crossing serves digest64() and digest128() alike.
__attribute__((export_name("hayahash_stream_digest")))
void wasm_hayahash_stream_digest(const hayahash64_state *st, uint64_t out[2])
{
	hayahash128_t digest = hayahash128_digest(st);
	out[0] = digest.lo;
	out[1] = digest.hi;
}

__attribute__((export_name("hayahash128")))
void wasm_hayahash128(const uint8_t *p, uint32_t len, uint64_t seed,
                     uint64_t out[2])
{
	hayahash128_t digest = hayahash128(p, (ptrdiff_t)len, seed);
	out[0] = digest.lo;
	out[1] = digest.hi;
}
