# hayahash64

[![CI](https://img.shields.io/github/actions/workflow/status/thevilledev/hayahash/ci.yml?branch=main&logo=githubactions&logoColor=white&label=CI)](https://github.com/thevilledev/hayahash/actions/workflows/ci.yml)
[![license](https://img.shields.io/github/license/thevilledev/hayahash?logo=unlicense&logoColor=white&label=license)](LICENSE)

[![C header](https://img.shields.io/github/v/release/thevilledev/hayahash?logo=c&logoColor=white&label=C%20header)](hayahash.h)
[![crates.io](https://img.shields.io/crates/v/hayahash?logo=rust&logoColor=white&label=crates.io)](https://crates.io/crates/hayahash)
[![pkg.go.dev](https://img.shields.io/github/v/release/thevilledev/hayahash?logo=go&logoColor=white&label=pkg.go.dev)](https://pkg.go.dev/github.com/thevilledev/hayahash/go)
[![zig](https://img.shields.io/github/v/release/thevilledev/hayahash?logo=zig&logoColor=white&label=zig)](https://github.com/thevilledev/hayahash/releases/latest)
[![Maven Central](https://img.shields.io/maven-central/v/io.github.thevilledev/hayahash?logo=openjdk&logoColor=white&label=maven%20central)](https://central.sonatype.com/artifact/io.github.thevilledev/hayahash)
[![NuGet](https://img.shields.io/nuget/v/Hayahash?logo=nuget&logoColor=white&label=nuget)](https://www.nuget.org/packages/Hayahash)
[![PyPI](https://img.shields.io/pypi/v/hayahash?logo=pypi&logoColor=white&label=pypi)](https://pypi.org/project/hayahash/)
[![npm](https://img.shields.io/npm/v/hayahash?logo=npm&logoColor=white&label=npm)](https://www.npmjs.com/package/hayahash)

A small 64-bit hash function that passes the full
[SMHasher3](https://gitlab.com/fwojcik/smhasher3) suite while staying
strictly portable: no SIMD, no 64x64-to-128-bit multiply, no
per-architecture code, no UB, and endianness-independent output.

That suits wasm, JVM, .NET, and portable C targets where only ordinary
64-bit multiplication is available. On native x86-64 or ARM64, where a
wide multiply is available, `rapidhash v3` is still the better default
unless that portability matters to you: it leads on small keys
everywhere and on both axes on the M1, although its sustained-bulk
lead on Zen 5 did not survive hayahash's auto-vectorized bulk loop
(see the [shootout](#smhasher3-shootout) below).

[ChibiHash](https://github.com/N-R-K/ChibiHash) sets out to do a
similar thing, which makes it the most useful baseline to measure
against, and the comparisons below use it that way. hayahash is its
own design rather than a fork of it; where it does borrow a specific
trick, the header says so.

*Haya* (速) is Japanese for "fast".

The reference implementation is the single C header
[`hayahash.h`](hayahash.h) at the repository root. Bit-exact ports to
Rust, Go, Zig, Java, C#, Python, Swift, JavaScript/TypeScript, and
MIPS64 assembly live in this repository; see [Usage](#usage).

Documentation: [design](docs/design.md) ·
[quality](docs/quality.md) · [ports & layout](docs/ports.md) ·
[running SMHasher3](docs/smhasher3.md) ·
[optimization log](docs/optimization/)

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
ChibiHash v2. The 32..128-byte rows reflect the fifth optimization
pass's dispatch choice: clang targets take the compact dispatch, giving
up 5..9% at these fixed sizes to run 2..10% faster on mixed-size
workloads, which single-size tables cannot show (see the
[optimization log](docs/optimization/)).

The same comparison on native x86-64 (AMD Zen 5, GCC 16,
`-march=native`) now shows hayahash ahead of both ChibiHash versions
at every measured size: 128-byte keys 5.2 vs 6.6 ns independent and
24.7 vs 19.5 GB/s streamed, the 320..512-byte band that v2 led after
the fourth pass flipped to a 27..47% hayahash lead (512 bytes: 42.2
vs 28.6 GB/s), and sustained bulk essentially doubled to 61.3 vs
31.2 GB/s at 1 MiB after the fifth pass taught GCC to auto-vectorize
the bulk loop for AVX-512 (builds without AVX-512DQ keep the previous
35 GB/s scalar rate). Dispatch shapes are tuned per architecture and
compiler; the [optimization log](docs/optimization/) documents the
measurements.

### SMHasher3 shootout

Run with `make -C tests/smhasher3 run`, which pins SMHasher3 to an exact
upstream commit; [`docs/smhasher3.md`](docs/smhasher3.md) covers how to
reproduce all of this. Small-key numbers are cycles per hash over 1-31-byte
keys (lower is better); bulk is bytes per cycle on 256 KiB keys (higher
is better). Full-suite results are our own runs at that pinned commit,
not upstream's published tables. Every cell was re-measured at `v0.4.0`,
five replicates per hash.

| hash | M1 small | M1 bulk | Zen 5 small | Zen 5 bulk | full suite | peak-performance requirement |
|---|---:|---:|---:|---:|---|---|
| rapidhash v3 | **21.5** | 15.2 | 9.8 | 28.6 | pass | 64x64-to-128-bit multiply |
| wyhash v4.2 | 22.5 | 8.8 | 8.8 | 19.8 | fail (11) | 64x64-to-128-bit multiply |
| a5hash v5.21 | 23.3 | 3.0 | **6.1** | 6.9 | pass | 64x64-to-128-bit multiply |
| komihash v5.27 | 25.3 | 7.5 | 10.7 | 19.9 | pass | 64x64-to-128-bit multiply |
| XXH3-64 | 25.9 | **12.6** | 10.1 | 48.9 | fail (22) | SIMD for peak bulk speed |
| gxhash-64 | - | - | 16.4 | **64.7** | fail (23) | AES instructions |
| **hayahash64** | 33.5 | 9.8 | 12.0 | 31.2 [^avx] | pass | ordinary 64x64-to-64-bit multiply |
| ChibiHash v2 | 37.7 | 6.1 | 9.2 | 15.7 | pass | ordinary 64x64-to-64-bit multiply |
| mx3.v3 | 44.7 | 4.2 | 16.9 | 10.1 | fail (26) | ordinary 64x64-to-64-bit multiply |
| SpookyHash2-64 | 52.6 | 4.1 | 18.1 | 15.4 | fail (5) | ordinary 64-bit operations |

[^avx]: That figure needs the compiler to auto-vectorize the bulk loop,
    which requires AVX-512DQ for its packed 64-bit multiply. A separate
    five-replicate sweep across the three shapes on the same host gives
    31.1 B/cy under GCC with `-march=native`, 31.1 under Clang, and
    **17.6 under GCC without AVX-512DQ** (the 31.2 above is the same
    quantity from the main sweep; the 0.1 is run-to-run). An EPYC 9655
    reproduces the same 1.8x gap under GCC 13 and Clang 18, and an EPYC
    7B13 with no AVX-512 at all cannot reach the vectorized shape however
    it is tuned. The source stays portable
    scalar C either way - nothing here is hand-vectorized - but a machine
    without AVX-512 gets the lower rate. Small-key figures are unaffected.

Two things about the small-key column before reading anything into it.
It is *dependent latency*, not throughput: SMHasher3 feeds each hash's
output back into the next key, so hashes cannot overlap. It is therefore
not comparable to the independent-hash figures in the ChibiHash tables
above. It also needed correcting: SMHasher3 measures call overhead once
per process and subtracts it from all 31 lengths, and that calibration
varied by ~2 cycles between runs, which is 10-40% on Zen 5. The archived
records explain the correction; after it the five Zen 5 replicates agree
to within 0.35 cycles. The fanless M1 is noisier, so its figures are
medians and the archived record reports both raw and outlier-trimmed
dispersion.

gxhash is measured on Zen 5 only. Its SMHasher3 port takes the hardware
path solely under x86 AES, so an ARM number would describe a software-AES
fallback rather than gxhash.

The table above is the two bare-metal hosts. Two further machines, an EPYC
7B13 (Zen 3) and an EPYC 9655 (Zen 5 Turin), were measured as well but are
shared KVM guests with no frequency control, so their absolute rates are not
comparable and are archived as indicative only. Their *within-host* ratios
are worth reporting, because they reproduce the conclusions below on
different silicon and two compiler generations back: on the EPYC 9655,
hayahash leads ChibiHash v2 in bulk by 1.99x, exactly the bare-metal Zen 5
ratio, and leads rapidhash v3 21.3 to 19.8. On the Zen 3 host, which has no
AVX-512 and so cannot vectorize, that lead over ChibiHash v2 collapses to
1.05x. All three x86 hosts agree that ChibiHash v2 is ahead on small keys.

The results fall into distinct instruction classes:

- **Wide multiply:** rapidhash v3 passes and stays ahead of hayahash on
  small keys everywhere and on both axes on the M1; it remains the
  honest recommendation when a native 128-bit multiply result is
  available - exactly the instruction hayahash's portability rules
  exclude. Its Zen 5 bulk lead did not survive the fifth pass, though:
  28.6 bytes/cycle against hayahash's 31.2, and only where hayahash's bulk
  loop vectorizes.
- **SIMD or hardware acceleration:** these win bulk decisively where the
  hardware provides them. XXH3-64 reaches 48.9 B/cy on Zen 5 with AVX-512
  and 12.6 B/cy on the M1 with NEON, and gxhash reaches 64.7 B/cy on Zen 5
  with AES-NI, against hayahash's 31.2 and 9.8. Both fail the suite, but
  they are not beaten on speed.
- **Portable 64-bit scalar:** the only other hash here that meets
  hayahash's constraints *and* passes is ChibiHash v2. hayahash leads it
  in bulk on both hosts, by 1.61x on the M1 and 1.99x on Zen 5. On small
  keys the two split: hayahash takes 11% less time on the M1, but 30%
  more on Zen 5, where ChibiHash v2's simpler short path wins the
  8-15-byte band by about 5.3 cycles. mx3.v3 is the remaining
  ordinary-multiply candidate and fails 26 tests.

Taken together: within the portable scalar class, hayahash is the fastest
hash we found in sustained bulk that passes the complete SMHasher3 suite,
on both hosts. On Zen 5 it also beats every wide-multiply hash measured
here in bulk, rapidhash v3 included; only XXH3-64 and gxhash stream
faster, and both fail the suite. That last part is conditional, though:
it holds where the bulk loop vectorizes, and a build without AVX-512DQ
falls back to 17.6 B/cy, behind rapidhash's 28.6.

Two further limits. hayahash does not hold a small-key lead everywhere -
ChibiHash v2 is ahead on Zen 5 - so that advantage is
architecture-dependent, not general. And this is a deliberately scoped
claim rather than a universal record: two hosts, bounded by the roughly
250 hashes SMHasher3 tracks.

## Quality

- SMHasher3: **188/188 tests passed**, verification value `0xF3C4A9B4`,
  re-verified at `v0.4.0` on nine builds spanning four hosts, five
  compilers, and all three dispatch shapes the header compiles.
  (ChibiHash v2 also passes 188/188; v1 fails.)
- Local harness (`make -C tests run-quality`): strict avalanche
  criterion over input and seed bits, plus exact-collision tests over
  24 structured key sets, including reproductions of the SMHasher3
  keysets that broke earlier iterations. All clean.
- All nine ports are bit-exact against the C reference: shared
  known-answer vectors and the SMHasher3 verification value in every
  port's test suite, plus nightly differential fuzzing that replays a
  fresh randomized C-reference corpus through every language port
  (both JavaScript engines included; the MIPS64 assembly port relies
  on the shared vectors instead).
- Endianness and ABI are tested in CI: big-endian s390x, ILP32 wasm32,
  MSVC x64, and MIPS64 under qemu must all reproduce the shared KAT.

[`docs/quality.md`](docs/quality.md) details each of these;
[`paper/AUDIT.md`](paper/AUDIT.md) tracks claim-by-claim evidence.

## Usage

C - copy [`hayahash.h`](hayahash.h) into your project:

```c
#include "hayahash.h"

uint64_t h = hayahash64(buf, len, seed);
```

| language | package | call |
|---|---|---|
| [Rust](rust/) | `hayahash` on crates.io (`no_std`) | `hayahash::hayahash64(buf, seed)` |
| [Go](go/) | `github.com/thevilledev/hayahash/go` | `hayahash.Hash64(buf, seed)` |
| [Zig](zig/) | `hayahash` module (Zig 0.16) | `hayahash.hayahash64(buf, seed)` |
| [Java](java/) | `io.github.thevilledev:hayahash` (17+) | `Hayahash.hash64(buf, seed)` |
| [C#](csharp/) | `Hayahash` on NuGet (.NET 8+) | `Hayahash.Hash64(buf, seed)` |
| [Python](python/) | `hayahash` on PyPI (3.9+) | `hayahash64(buf, seed)` |
| [Swift](swift/) | `Hayahash` SwiftPM package (5.9+) | `Hayahash.hash64(buf, seed: 0)` |
| [JS/TS](js/) | `hayahash` on npm (wasm + pure JS) | `hayahash64(buf, seed)` |
| [MIPS64](mips/) | `hayahash.S` (n64 ABI) | `hayahash64(buf, len, seed)` |

Per-language examples and the full repository layout are in
[`docs/ports.md`](docs/ports.md).

## Design

Four ideas, explained in detail at the top of the header and in
[`docs/design.md`](docs/design.md):

1. **A short bulk dependency chain** - 8 independent lanes, nothing
   longer than `add -> mul` on the loop-carried path.
2. **A chained, injective absorb** - each lane absorbs
   `w + rotl(w_prev, 27)`; injective by first-difference induction.
3. **Derived lane constants** - seed and length premixed once; all
   lane IVs derived from it, no big per-lane literals.
4. **Overlapping tail reads, two-multiply short path** - wyhash-style
   whole-word tails, no byte loops; a dedicated <= 16-byte path.

The hard part was keeping speed while closing every structural
collision class SMHasher3 found in earlier iterations; those are
documented in [`docs/design.md`](docs/design.md), and the measured
history of every optimization (including rejected ideas) is in the
[optimization log](docs/optimization/).

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
