# hayahash64

An experimental successor to [ChibiHash](https://github.com/N-R-K/ChibiHash)
v1/v2: a small 64-bit hash function that passes the full
[SMHasher3](https://gitlab.com/fwojcik/smhasher3) suite. Its claim is
**fastest in its portability class**, not fastest overall: no SIMD, no
64x64-to-128-bit multiply, no per-architecture code, no UB, and
endianness-independent output.

That makes hayahash a candidate for wasm, JVM, and portable C targets
where only ordinary 64-bit multiplication is available. On native
x86-64 or ARM64, where a wide multiply is available, `rapidhash v3` is
decisively faster and is the better choice unless this portability is
required.

*Haya* (速) is Japanese for "fast" - a nod to ChibiHash's naming.

The reference implementation is the single C header
[`hayahash.h`](hayahash.h) at the repository root.

## Repository layout

- `hayahash.h` - reference implementation (C99, single header, public
  domain)
- `rust/` - Rust port (`hayahash` crate, `no_std` compatible)
- `go/` - Go port (`github.com/thevilledev/hayahash/go` module)
- `zig/` - Zig port (`hayahash` module, Zig 0.16)
- `java/` - Java port (Maven module `io.github.thevilledev:hayahash`,
  Java 17+)
- `js/` - JavaScript/TypeScript port for npm (`hayahash` package): the
  reference header compiled to WebAssembly, plus a pure-JS fallback
- `tests/` - C quality and benchmark harnesses; ChibiHash v1/v2
  reference sources are vendored there so the comparisons are
  self-contained
- `tests/wasm/` - baseline-wasm32 shootout and wasm-vs-native
  bit-exactness check (zig cc + Node); run on demand in CI via the
  "Wasm bench" workflow, or locally with `make -C tests/wasm run-kat
  run-bench`. Competitor headers (rapidhash, xxHash) are fetched
  pinned to exact upstream commits at build time

Each port lives in its own top-level directory and is verified against
the reference implementation via the SMHasher3 verification value and
the shared known-answer vectors (see `rust/tests/kat.rs`,
`go/kat_test.go`, `zig/tests/kat.zig`, the Java `KatTest` under
`java/src/test`, and `js/test/hayahash.test.mjs`).

## Usage

C - copy `hayahash.h` into your project:

```c
#include "hayahash.h"

uint64_t h = hayahash64(buf, len, seed);
```

Rust - the `hayahash` crate lives in [`rust/`](rust/):

```rust
let h = hayahash::hayahash64(buf, seed);
```

Go - the module lives in [`go/`](go/):

```go
import hayahash "github.com/thevilledev/hayahash/go"

h := hayahash.Hash64(buf, seed)
```

Zig - the package lives in [`zig/`](zig/):

```zig
const hayahash = @import("hayahash");

const h = hayahash.hayahash64(buf, seed);
```

Java - the Maven module lives in [`java/`](java/):

```java
import io.github.thevilledev.hayahash.Hayahash;

long h = Hayahash.hash64(buf, seed);
```

JavaScript/TypeScript - the npm package lives in [`js/`](js/); the
fast path is `hayahash.h` itself, compiled to a ~1.5 KB WebAssembly
module, with a pure-JS fallback:

```js
import { hayahash64 } from "hayahash";

const h = hayahash64(buf, seed); // unsigned 64-bit bigint
```

## Design

Four ideas, explained in detail at the top of the header:

1. **A short bulk dependency chain.** ChibiHash v2's bulk loop carries a
   ~5-cycle `add -> mul -> xor` chain per 8-byte stripe across 4 lanes.
   hayahash uses 8 independent lanes over 64-byte blocks with nothing
   longer than `add -> mul` (~4 cycles) on the loop-carried path, and no
   cross-lane ALU work on that path.
2. **A chained, injective absorb.** Each lane absorbs
   `t = w + rotl(w_prev, 27)`, where `w_prev` is the previous stripe.
   At the first stripe where two inputs differ, `w_prev` is still
   equal, so `t` differs: the absorb sequence is injective by
   induction. The rotated copy also plants every stripe bit at a low
   position in the next lane, where `+` and `rotl` commute with
   neither GF(2) nor mod-2^64 algebra. The rotation amount, the fold
   rotations, and a final "wall" absorb of the last stripe's dangling
   copy are all chosen against difference-ladder attacks; see the
   header notes. The rotation applies to an already-loaded register,
   off the loop-carried path, and is cheaper than a second load on
   wide cores.
3. **Derived lane constants.** Seed and length are premixed into one
   value `s`, and all lane IVs are derived from `s` plus shifted copies
   of the single multiplier constant. No big per-lane literals are
   materialized (on AArch64 a 64-bit literal costs 4 instructions), and
   full-state seeding comes for free.
4. **Overlapping tail reads, two-multiply short path.** Tails read
   whole (overlapping) words from the end of the input, wyhash-style,
   so no byte-at-a-time loop exists for any length. Inputs of at most
   16 bytes take a dedicated path: both loaded words are spread with
   bijective 3-rotation injections (a different one per word, so the
   two multiply terms cannot be erased simultaneously) and passed
   through independent multiplies into a strong finalizer.

Getting the details right was the hard part: SMHasher3 found five
distinct structural collision classes in earlier iterations of this
design:

- a GF(2) nullspace in a staggered-load absorb
- seed-copy erasure by aligned key bits - twice
- top-window carry-luck ladders, and
- a fold rotation resonating with the absorb rotation

Each fix is documented in the header where it lives.

## Quality

- SMHasher3: **188/188 tests passed**, verification value `0x6B558D9D`.
  (ChibiHash v2 also passes 188/188; v1 fails.)
- Local harness (`make -C tests run-quality`): strict avalanche
  criterion over input and seed bits, plus exact-collision tests over
  23 structured key sets, including reproductions of the SMHasher3
  keysets that broke earlier iterations. All clean.
- The Rust, Go, Zig, Java, and JavaScript ports are bit-exact against
  the C reference:
  each port's test suite checks the SMHasher3 verification value and a
  shared table of known-answer vectors generated from `hayahash.h`.
  (The JavaScript package checks both of its engines: the wasm build
  of the reference header and the pure-JS fallback.)
- The absorb sequence is injective by construction (first-difference
  induction), and all tail injections are bijective.

## Performance

### ChibiHash comparison

Measured on an Apple M1 (P-core, ~3.2 GHz), Apple clang -O3
`-mcpu=native`, against the C reference implementations of ChibiHash
v1/v2 vendored in `tests/` (`make -C tests run-bench`):

Large-input throughput (GB/s, higher is better):

| size    | chibihash v1 | chibihash v2 | hayahash  |
|--------:|-------------:|-------------:|----------:|
| 64      | 7.5          | 9.8          | **10.1**  |
| 256     | 15.5         | 17.6         | **17.7**  |
| 1024    | 15.3         | 19.2         | **23.8**  |
| 16384   | 14.4         | 18.5         | **26.5**  |
| 1048576 | 14.4         | 18.5         | **26.8**  |

Small-input latency (ns/hash, seed-chained, lower is better):

| len | chibihash v1 | chibihash v2 | hayahash |
|----:|-------------:|-------------:|---------:|
| 4   | 10.0         | 10.4         | **7.8**  |
| 8   | 6.6          | 10.0         | 7.9      |
| 16  | 6.9          | 10.2         | 7.8      |
| 32  | 12.3         | 11.7         | **10.3** |
| 64  | 14.5         | 13.3         | **12.1** |
| 128 | 18.8         | 17.1         | **15.7** |

Small-input throughput (ns/hash, independent hashes, lower is better):

| len | chibihash v1 | chibihash v2 | hayahash |
|----:|-------------:|-------------:|---------:|
| 4   | 7.5          | 4.4          | **3.1**  |
| 8   | 4.6          | 4.6          | **3.1**  |
| 16  | 5.2          | 5.1          | **3.1**  |
| 32  | 6.9          | 5.4          | **5.3**  |
| 64  | 8.6          | 6.6          | **6.4**  |
| 128 | 11.9         | 9.2          | **8.8**  |

v1's 8/16-byte latency wins come from special-cased paths that are also
part of why it fails smhasher3; among the two functions that pass,
hayahash is fastest at every size, and the bulk rate is ~1.4x
ChibiHash v2.

### SMHasher3 shootout

The following averages come from back-to-back SMHasher3 speed tests on
the same Apple M1. Small-key results average the 1-31-byte tests (lower
is better); bulk results are bytes per cycle (higher is better).
Full-suite results combine our runs with SMHasher3's published results.

| hash | small keys (cy/hash) | bulk (B/cy) | full suite | peak-performance requirement |
|---|---:|---:|---|---|
| rapidhash v3 | **20.7** | **15.4** | pass | 64x64-to-128-bit multiply |
| wyhash v4.2 | 21.0 | 9.2 | fail (15 tests) | 64x64-to-128-bit multiply |
| a5hash | 21.9 | 3.2 | pass | 64x64-to-128-bit multiply |
| komihash | 24.2 | 7.6 | pass | 64x64-to-128-bit multiply |
| XXH3-64 | 24.7 | 7.3 | fail (27 tests) | SIMD for peak bulk speed |
| **hayahash64** | 32.6 | 9.3 | pass | ordinary 64x64-to-64-bit multiply |
| ChibiHash v2 | 35.1 | 6.6 | pass | ordinary 64x64-to-64-bit multiply |
| mx3.v3 | 43.9 | 4.2 | fail (36 tests) | ordinary 64x64-to-64-bit multiply |
| SpookyHash2-64 | 50.9 | 4.2 | fail (6 tests) | ordinary 64-bit operations |

The results fall into distinct instruction classes:

- **Wide multiply:** rapidhash v3 passes the full suite and is about
  1.6x faster than hayahash on small keys and 1.65x faster in bulk.
  This is the honest recommendation when a native 128-bit multiply
  result is available. That instruction is exactly what hayahash's
  portability rules exclude.
- **SIMD or hardware acceleration:** faster x86 results exist; for
  example, upstream reports about 19.6 B/cy for gxhash, although it
  fails 24 tests. On this M1, hayahash nevertheless beats NEON XXH3-64
  in bulk, 9.3 versus 7.3 B/cy.
- **Portable 64-bit scalar:** among hashes for which we found evidence
  that meet all of hayahash's constraints and pass the complete suite,
  hayahash is fastest on both axes. It improves on the previous class
  leader, ChibiHash v2, by about 7% on small keys and 1.4x in bulk.
  Other ordinary-multiply candidates either fail the suite or are
  substantially slower.

In short: hayahash is the fastest fully portable 64-bit hash we found
that passes the complete SMHasher3 suite. This is a deliberately scoped
claim, not a universal record. The measurements above are from one
Apple M1; hayahash does not yet have x86 measurements, although
SMHasher3's upstream x86 tables suggest the same class ordering. The
search is also bounded by the roughly 250 hashes tracked by SMHasher3,
not every hash implementation in existence.

## Status

Experimental prototype. The algorithm, constants, and digest values may
still change; do not use hayahash yet anywhere hashes are persisted.
An SMHasher3 plugin (`hashes/hayahash.cpp`) is being worked on.

## License

Public domain, under the [Unlicense](LICENSE).
