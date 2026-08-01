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
- `tests/smhasher3/` - SMHasher3 adapter and pinned build harness; the
  suite is cloned at test time, never vendored. See
  [`docs/smhasher3.md`](docs/smhasher3.md)
- `docs/` - how to run and reproduce the external test suites
- `tests/differential/` - reproducible randomized corpus generator for
  nightly cross-port differential conformance against the C reference
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
- Nightly differential conformance fuzzing generates one C-reference
  corpus with random input bytes, random 64-bit hash seeds, exhaustive
  lengths 0..384, and boundary-biased random lengths through the
  128 KiB edge.
  Every port consumes the identical corpus (including both JavaScript
  engines). The logged PRNG seed or the failure artifact reproduces a
  run exactly; the workflow can also be dispatched manually with a
  chosen seed.
- Endianness is tested in CI: the shared KAT is also produced on
  s390x (big-endian, via `zig cc` + qemu-user) and must match the
  little-endian reference. wasm32 covers the ILP32 case; MSVC x64
  covers the Windows ABI.
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
| 64      | 7.7          | 9.9          | **13.0**  |
| 256     | 15.7         | 17.8         | **20.3**  |
| 1024    | 15.5         | 19.5         | **27.2**  |
| 16384   | 14.6         | 18.8         | **30.1**  |
| 1048576 | 14.6         | 18.8         | **30.2**  |

Small-input latency (ns/hash, seed-chained, lower is better):

| len | chibihash v1 | chibihash v2 | hayahash |
|----:|-------------:|-------------:|---------:|
| 4   | 9.7          | 10.1         | **7.3**  |
| 8   | 6.4          | 9.6          | 7.3      |
| 16  | 6.8          | 9.9          | 7.3      |
| 32  | 12.0         | 11.4         | **8.0**  |
| 64  | 14.2         | 13.1         | **9.4**  |
| 128 | 19.7         | 16.9         | **13.0** |

Small-input throughput (ns/hash, independent hashes, lower is better):

| len | chibihash v1 | chibihash v2 | hayahash |
|----:|-------------:|-------------:|---------:|
| 4   | 7.3          | 4.3          | **2.8**  |
| 8   | 4.5          | 4.5          | **2.8**  |
| 16  | 5.2          | 5.0          | **2.8**  |
| 32  | 7.4          | 5.3          | **3.8**  |
| 64  | 8.5          | 6.6          | **5.0**  |
| 128 | 11.8         | 9.0          | **7.5**  |

v1's 8/16-byte latency wins come from special-cased paths that are also
part of why it fails SMHasher3; among the two functions that pass,
hayahash is fastest at every size, and the bulk rate is ~1.6x
ChibiHash v2. The 32..128-byte rows reflect the fifth pass's
dispatch choice: clang targets take the compact dispatch, giving up
5..9% at these fixed sizes to run 2..10% faster on mixed-size
workloads, which single-size tables cannot show (see
`OPTIMIZATION_NOTES.md`).

The same comparison on native x86-64 (AMD Zen 5, GCC 16,
`-march=native`) now shows hayahash ahead of both ChibiHash versions
at every measured size: 128-byte keys 5.2 vs 6.6 ns independent and
24.7 vs 19.5 GB/s streamed, the 320..512-byte band that v2 led after
the fourth pass flipped to a 27..47% hayahash lead (512 bytes: 42.2
vs 28.6 GB/s), and sustained bulk essentially doubled to 61.3 vs
31.2 GB/s at 1 MiB after the fifth pass taught GCC to auto-vectorize
the bulk loop for AVX-512 (builds without AVX-512DQ keep the previous
35 GB/s scalar rate). Dispatch shapes are tuned per architecture and
compiler; `OPTIMIZATION_NOTES.md` documents the measurements.

### SMHasher3 shootout

Run with `make -C tests/smhasher3 run`, which pins SMHasher3 to an exact
upstream commit; [`docs/smhasher3.md`](docs/smhasher3.md) covers how to
reproduce all of this. Small-key numbers are cycles per hash over 1-31-byte
keys (lower is better); bulk is bytes per cycle on 256 KiB keys (higher
is better). Full-suite results are our own runs at that pinned commit,
not upstream's published tables. hayahash's Zen 5 bulk cell was
re-measured after the fifth pass's GCC vectorization change; every
other cell is from the fourth-pass session, whose code paths the fifth
pass left bit-identical.

| hash | M1 small | M1 bulk | Zen 5 small | Zen 5 bulk | full suite | peak-performance requirement |
|---|---:|---:|---:|---:|---|---|
| rapidhash v3 | **21.2** | 14.9 | 9.8 | 28.7 | pass | 64x64-to-128-bit multiply |
| wyhash v4.2 | 22.2 | 8.6 | 8.8 | 19.8 | fail (11) | 64x64-to-128-bit multiply |
| a5hash v5.21 | 23.0 | 3.0 | **6.1** | 6.9 | pass | 64x64-to-128-bit multiply |
| komihash v5.27 | 25.0 | 7.5 | 10.7 | 19.9 | pass | 64x64-to-128-bit multiply |
| XXH3-64 | 25.6 | **12.6** | 10.1 | 49.1 | fail (22) | SIMD for peak bulk speed |
| gxhash-64 | - | - | 16.4 | **64.8** | fail (23) | AES instructions |
| **hayahash64** | 33.3 | 9.7 | 12.0 | 31.1 | pass | ordinary 64x64-to-64-bit multiply |
| ChibiHash v2 | 37.4 | 6.1 | 9.2 | 15.8 | pass | ordinary 64x64-to-64-bit multiply |
| mx3.v3 | 45.1 | 4.1 | 16.9 | 10.1 | fail (26) | ordinary 64x64-to-64-bit multiply |
| SpookyHash2-64 | 52.2 | 4.1 | 18.1 | 15.4 | fail (5) | ordinary 64-bit operations |

Two things about the small-key column before reading anything into it.
It is *dependent latency*, not throughput: SMHasher3 feeds each hash's
output back into the next key, so hashes cannot overlap. It is therefore
not comparable to the independent-hash figures in the ChibiHash tables
above. It also needed correcting: SMHasher3 measures call overhead once
per process and subtracts it from all 31 lengths, and that calibration
varied by ~2 cycles between runs, which is 10-40% on Zen 5. The archived
records explain the correction; after it, replicates agree to within
0.25 cycles.

gxhash is measured on Zen 5 only. Its SMHasher3 port takes the hardware
path solely under x86 AES, so an ARM number would describe a software-AES
fallback rather than gxhash.

The results fall into distinct instruction classes:

- **Wide multiply:** rapidhash v3 passes and stays ahead of hayahash on
  small keys everywhere and on both axes on the M1; it remains the
  honest recommendation when a native 128-bit multiply result is
  available - exactly the instruction hayahash's portability rules
  exclude. Its Zen 5 bulk lead did not survive the fifth pass, though:
  28.7 bytes/cycle against hayahash's 31.1.
- **SIMD or hardware acceleration:** these win bulk decisively where the
  hardware provides them. XXH3-64 reaches 49.1 B/cy on Zen 5 with AVX-512
  and 12.6 B/cy on the M1 with NEON, and gxhash reaches 64.8 B/cy on Zen 5
  with AES-NI, against hayahash's 31.1 and 9.7. Both fail the suite, but
  they are not beaten on speed.
- **Portable 64-bit scalar:** the only other hash here that meets
  hayahash's constraints *and* passes is ChibiHash v2. hayahash leads it
  in bulk on both hosts, by 1.59x on the M1 and 1.97x on Zen 5. On small
  keys the two split: hayahash takes 11% less time on the M1, but 30%
  more on Zen 5, where ChibiHash v2's simpler short path wins the
  8-15-byte band by about 5.3 cycles. mx3.v3 is the remaining
  ordinary-multiply candidate and fails 26 tests.

Taken together: within the portable scalar class, hayahash is the fastest
hash we found in sustained bulk that passes the complete SMHasher3 suite,
on both hosts - and on Zen 5 it is now the fastest passing hash in bulk
of any instruction class measured here, wide multiplies included; only
XXH3-64 and gxhash stream faster, and both fail the suite. It does not
hold that lead on small-key latency
everywhere - ChibiHash v2 is ahead on Zen 5 - so the small-key advantage
is architecture-dependent, not general. This is a deliberately scoped
claim, not a universal record: two hosts, one compiler each, bounded by
the roughly 250 hashes SMHasher3 tracks.

## Status

Experimental prototype. The algorithm, constants, and digest values may
still change; do not use hayahash yet anywhere hashes are persisted.
The SMHasher3 adapter lives in [`tests/smhasher3/`](tests/smhasher3/); it
includes `hayahash.h` directly, so the suite tests the shipped header
rather than a transcription. It is not in a form upstream would accept,
which needs a self-contained port to SMHasher3's own primitives.

## License

Public domain, under the [Unlicense](LICENSE). That covers `hayahash.h`
and every language port.

One exception: the source files in [`tests/smhasher3/`](tests/smhasher3/)
are GPL-3.0-or-later, with the license text in
[`tests/smhasher3/COPYING`](tests/smhasher3/COPYING). The two patches
modify SMHasher3's own sources, and the adapter is written against its
headers and macros and only ever compiled into it. This does not affect
hayahash64: the adapter includes `hayahash.h`, which does not change that
header's public-domain status. SMHasher3 is fetched at test time and is
not part of any release artifact, so building it locally does not affect
this software's license; redistributing the resulting combined SMHasher3
executable would need to comply with the GPL.
