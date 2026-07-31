# hayahash64

[![CI](https://img.shields.io/github/actions/workflow/status/thevilledev/hayahash/ci.yml?branch=main&logo=githubactions&logoColor=white&label=CI)](https://github.com/thevilledev/hayahash/actions/workflows/ci.yml)
[![license](https://img.shields.io/github/license/thevilledev/hayahash?logo=unlicense&logoColor=white&label=license)](LICENSE)

[![C header](https://img.shields.io/github/v/release/thevilledev/hayahash?logo=c&logoColor=white&label=C%20header)](hayahash.h)
[![crates.io](https://img.shields.io/crates/v/hayahash?logo=rust&logoColor=white&label=crates.io)](https://crates.io/crates/hayahash)
[![pkg.go.dev](https://img.shields.io/github/v/release/thevilledev/hayahash?logo=go&logoColor=white&label=pkg.go.dev)](https://pkg.go.dev/github.com/thevilledev/hayahash/go)
[![zig](https://img.shields.io/github/v/release/thevilledev/hayahash?logo=zig&logoColor=white&label=zig)](https://github.com/thevilledev/hayahash/releases/latest)
[![Maven Central](https://img.shields.io/maven-central/v/io.github.thevilledev/hayahash?logo=openjdk&logoColor=white&label=maven%20central)](https://central.sonatype.com/artifact/io.github.thevilledev/hayahash)
[![npm](https://img.shields.io/npm/v/hayahash?logo=npm&logoColor=white&label=npm)](https://www.npmjs.com/package/hayahash)

A small 64-bit hash function that passes the full
[SMHasher3](https://gitlab.com/fwojcik/smhasher3) suite while staying
strictly portable: no SIMD, no 64x64-to-128-bit multiply, no
per-architecture code, no UB, and endianness-independent output.

That suits wasm, JVM, and portable C targets where only ordinary
64-bit multiplication is available. On native x86-64 or ARM64, where a
wide multiply is available, `rapidhash v3` is decisively faster and is
the better choice unless that portability matters to you.

[ChibiHash](https://github.com/N-R-K/ChibiHash) sets out to do a
similar thing, which makes it the most useful baseline to measure
against, and the comparisons below use it that way. hayahash is its
own design rather than a fork of it; where it does borrow a specific
trick, the header says so.

*Haya* (速) is Japanese for "fast".

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

1. **A short bulk dependency chain.** The bulk loop runs 8 independent
   lanes over 64-byte blocks with nothing longer than `add -> mul`
   (~4 cycles) on the loop-carried path, and no cross-lane ALU work
   there. For scale, ChibiHash v2 carries a ~5-cycle
   `add -> mul -> xor` chain per 8-byte stripe across 4 lanes.
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
| 64      | 7.5          | 9.8          | **13.4**  |
| 256     | 15.5         | 17.5         | **20.8**  |
| 1024    | 15.2         | 19.2         | **25.7**  |
| 16384   | 14.4         | 18.5         | **28.2**  |
| 1048576 | 14.3         | 18.5         | **28.5**  |

Small-input latency (ns/hash, seed-chained, lower is better):

| len | chibihash v1 | chibihash v2 | hayahash |
|----:|-------------:|-------------:|---------:|
| 4   | 9.9          | 10.3         | **7.4**  |
| 8   | 6.5          | 9.8          | 7.4      |
| 16  | 6.9          | 10.1         | 7.4      |
| 32  | 12.2         | 11.6         | **8.4**  |
| 64  | 14.3         | 13.2         | **9.5**  |
| 128 | 18.9         | 16.9         | **12.2** |

Small-input throughput (ns/hash, independent hashes, lower is better):

| len | chibihash v1 | chibihash v2 | hayahash |
|----:|-------------:|-------------:|---------:|
| 4   | 7.3          | 4.3          | **2.8**  |
| 8   | 4.5          | 4.5          | **2.8**  |
| 16  | 5.2          | 5.0          | **2.8**  |
| 32  | 7.5          | 5.3          | **3.5**  |
| 64  | 8.6          | 6.6          | **4.8**  |
| 128 | 11.9         | 9.1          | **6.9**  |

v1's 8/16-byte latency wins come from special-cased paths that are also
part of why it fails SMHasher3; among the two functions that pass,
hayahash is fastest at every size, and the bulk rate is ~1.5x
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
  hayahash is fastest on both axes. The next fastest of them,
  ChibiHash v2, is about 7% behind on small keys and 1.4x behind in
  bulk. Other ordinary-multiply candidates either fail the suite or
  are substantially slower.

Taken together: within that portable scalar class, hayahash is the
fastest hash we found that passes the complete SMHasher3 suite. This is
a deliberately scoped claim, not a universal record. The measurements
above are from one Apple M1; hayahash does not yet have x86
measurements, although SMHasher3's upstream x86 tables suggest the same
class ordering. The search is also bounded by the roughly 250 hashes
tracked by SMHasher3, not every hash implementation in existence.

## Status

Experimental prototype. The algorithm, constants, and digest values may
still change; do not use hayahash yet anywhere hashes are persisted.
An SMHasher3 plugin (`hashes/hayahash.cpp`) is being worked on.

## License

Public domain, under the [Unlicense](LICENSE).
