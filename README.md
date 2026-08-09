# hayahash

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

A small family of 64- and 128-bit hash functions that passes the full
[SMHasher3](https://gitlab.com/fwojcik/smhasher3) suite while staying
strictly portable: no SIMD, no 64x64-to-128-bit multiply, no
per-architecture code, no UB, and endianness-independent output.

That suits wasm, JVM, .NET, and portable C targets where only ordinary
64-bit multiplication is available. For 64-bit hashing on native x86-64
or ARM64, where a wide multiply is available, `rapidhash v3` is still the better default
unless that portability matters to you: it leads on small keys
everywhere and on both axes on the M1, although its sustained-bulk
lead on Zen 5 did not survive hayahash's auto-vectorized bulk loop
(see the [shootout](#smhasher3-shootout) below).

## 32-bit-arithmetic companion

Need a 64-bit digest where efficient 64-bit integer arithmetic is unavailable?
[`haya32x64`](https://github.com/thevilledev/haya32x64) keeps its state and
arithmetic strictly 32-bit while retaining the full result of each 32×32
multiply. It targets 32-bit processors, pure JavaScript without `BigInt`,
CSP-constrained runtimes, and similar environments; the JavaScript package is
[`haya32x64` on npm](https://www.npmjs.com/package/haya32x64).

`hayahash` and `haya32x64` both return 64-bit digests, but they are different
algorithms rather than interchangeable backends. They live in separate
repositories so each digest definition has independent reference code,
known-answer vectors, compatibility guarantees, versioning, and releases.
Switching between them changes persisted hashes and must be treated as a data
migration.

[ChibiHash](https://github.com/N-R-K/ChibiHash) sets out to do a
similar thing, which makes it the most useful baseline to measure
against, and the comparisons below use it that way. hayahash is its
own design rather than a fork of it; where it does borrow a specific
trick, the header says so.

*Haya* (速) is Japanese for "fast".

The reference implementation is the single C header
[`hayahash.h`](hayahash.h) at the repository root. Bit-exact ports to
Rust, Go, Zig, Java, C#, Python, Swift, JavaScript/TypeScript, and
MIPS64 assembly cover hayahash64; hayahash128 is currently exposed by
the C header. See [Usage](#usage).

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

| size    | chibihash v1 | chibihash v2 | hayahash64 | hayahash128 |
|--------:|-------------:|-------------:|-----------:|------------:|
| 64      | 7.36         | 9.74         | **12.59**  | 10.07       |
| 256     | 15.38        | 17.44        | **20.03**  | 18.24       |
| 1024    | 15.11        | 19.15        | **26.59**  | 23.97       |
| 16384   | 14.32        | 18.41        | **29.44**  | 26.98       |
| 1048576 | 14.21        | 18.35        | **29.78**  | 27.31       |

Small-input latency (ns/hash, seed-chained, lower is better):

| len | chibihash v1 | chibihash v2 | hayahash64 | hayahash128 |
|----:|-------------:|-------------:|-----------:|------------:|
| 4   | 9.89         | 10.15        | **8.01**   | 9.40        |
| 8   | **6.50**     | 9.69         | 7.99       | 9.43        |
| 16  | **6.85**     | 9.94         | 7.96       | 9.39        |
| 32  | 12.20        | 11.56        | **8.47**   | 10.81       |
| 64  | 14.32        | 13.22        | **9.86**   | 12.18       |
| 128 | 18.57        | 16.74        | **12.53**  | 15.02       |

Small-input throughput (ns/hash, independent hashes, lower is better):

| len | chibihash v1 | chibihash v2 | hayahash64 | hayahash128 |
|----:|-------------:|-------------:|-----------:|------------:|
| 4   | 7.35         | 4.32         | **2.83**   | 3.93        |
| 8   | 4.63         | 4.64         | **2.84**   | 3.96        |
| 16  | 5.16         | 4.96         | **2.76**   | 3.87        |
| 32  | 7.04         | 5.45         | **3.88**   | 5.21        |
| 64  | 8.61         | 6.51         | **5.05**   | 6.34        |
| 128 | 12.05        | 9.10         | **7.55**   | 8.81        |

The direct cost of selecting the 128-bit result was also measured on three
hosts. Each cell is the median of nine calibrated ~40 ms
samples on one pinned core; small-key latency chains each result into the
next seed, while the independent column allows overlap:

| host / compiler | 8 B chained, 64 / 128 (ns) | 8 B independent, 64 / 128 (ns) | 1 MiB, 64 / 128 (GB/s) |
|---|---:|---:|---:|
| Apple M1 Pro / Apple clang 21 | 7.99 / 9.43 | 2.84 / 3.96 | 29.78 / 27.31 |
| Ryzen AI 9 HX PRO 370 / GCC 16 | 4.29 / 4.93 | 1.88 / 2.77 | 61.60 / 33.88 |
| EPYC 9655 KVM guest / GCC 13 | 4.90 / 5.59 | 2.14 / 3.15 | 54.76 / 29.85 |

The M1 and Ryzen runs are bare metal. The EPYC is a KVM guest without
frequency control, so use its within-host ratio rather than comparing its
absolute rate with the other machines. `hayahash128.lo` is identical to
hayahash64 in every row; the extra work produces only the high word.

Baseline wasm32 is the target where the shared state walk pays off most.
This M1 Pro run used Zig 0.16 to compile one module with `-O3`, no SIMD,
and no wide multiply; all timing loops ran inside wasm under Node 26 / V8:

| hash | 8 B chained (ns/hash) | 1 MiB (GB/s) |
|---|---:|---:|
| **hayahash128** | 10.6 | 23.34 |
| hayahash64 | 7.7 | **23.55** |
| ChibiHash v2 | 10.4 | 18.57 |
| XXH3-64 | 8.2 | 17.41 |
| XXH64 | **5.9** | 14.71 |
| rapidhash v3 | 22.6 | 6.43 |

hayahash128 retains 99% of hayahash64's bulk rate in this build. The other
rows return 64 bits; ChibiHash v2 is the passing portable-scalar comparator
in this table, and hayahash128 is 26% faster in bulk here while returning
twice the output width.

v1's 8/16-byte latency wins come from special-cased paths that are also
part of why it fails SMHasher3; among the 64-bit functions that pass,
hayahash64 is fastest at every size, and its bulk rate is ~1.6x
ChibiHash v2. The 32..128-byte rows reflect the fifth optimization
pass's dispatch choice: clang targets take the compact dispatch, giving
up 5..9% at these fixed sizes to run 2..10% faster on mixed-size
workloads, which single-size tables cannot show (see the
[optimization log](docs/optimization/)).

The same comparison on native x86-64 (AMD Zen 5, GCC 16,
`-march=native`) now shows hayahash64 ahead of both ChibiHash versions
at every measured size: 128-byte keys 5.2 vs 6.6 ns independent and
24.3 vs 19.8 GB/s streamed, the 320..512-byte band that v2 led after
the fourth pass flipped to a 27..47% hayahash lead (512 bytes: 42.1
vs 29.3 GB/s), and sustained bulk essentially doubled to 61.6 vs
31.3 GB/s at 1 MiB after the fifth pass taught GCC to auto-vectorize
the bulk loop for AVX-512 (builds without AVX-512DQ keep the previous
35 GB/s scalar rate). hayahash128 reaches 6.37 ns at 128 bytes and
33.88 GB/s at 1 MiB on that same run. Dispatch shapes are tuned per
architecture and compiler; the [optimization log](docs/optimization/) documents the
measurements.

### SMHasher3 128-bit shootout

This is the current `v0.5` candidate against SMHasher3 commit
`51d3cd1ac0aa4934f6aacb44d9d234f50300b6e3`. Speed cells are medians of
three independent, round-robin processes on two bare-metal hosts. Small-key
latency is the 1-31-byte average corrected to one call-overhead baseline per
host; bulk is the fixed 256 KiB average. The full suite was run separately on
the EPYC 9655 with 128-bit-wide expectations.

| 128-bit hash | M1 small | M1 bulk | Zen 5 small | Zen 5 bulk | full suite | peak-performance requirement |
|---|---:|---:|---:|---:|---|---|
| **hayahash128** | 38.51 | 9.18 | 13.72 | 16.83 | pass | ordinary scalar source; auto-vectorized Zen 5 bulk |
| MuseAir-128 | 24.26 | 8.65 | 7.44 | 22.80 | pass | 64x64-to-128-bit multiply |
| a5hash-128 | **22.29** | 10.92 | **6.00** | 22.99 | pass | 64x64-to-128-bit multiply |
| MeowHash | - | - | 28.64 | 32.22 | pass | x86 AES instructions |
| XXH3-128 | 30.73 | **12.64** | 11.92 | **48.91** | fail (26) | wide multiply; SIMD for peak bulk |
| t1ha2-128 | 64.58 | 5.86 | 21.03 | 16.24 | pass | 64x64-to-128-bit multiply |
| SpookyHash2-128 | 53.37 | 4.20 | 24.39 | 15.35 | fail (10) | ordinary 64-bit operations |
| prvhash-128 | 67.45 | 1.00 | 25.49 | 2.58 | pass (187/187) | ordinary 64-bit operations |
| FarmHash-128.CC.seed1 | 60.09 | 5.64 | 21.09 | 16.45 | pass | ordinary 64-bit operations |

Small-key values are dependent latency, not independent throughput. The raw
SMHasher3 process averages also need correction because its once-per-process
call-overhead calibration shifts the whole 1-31-byte average. The archived
records contain every process value and the exact calculation. MeowHash has no
M1 row because the tested implementation requires x86 AES instructions.

The accelerated passers are faster on Zen 5: a5hash and MuseAir use a native
wide multiply result, while MeowHash uses AES. XXH3-128 is the bulk leader but
fails 26 of the 188 test groups. On M1, hayahash128 reaches 9.18 B/cy and on
Zen 5 it reaches 16.83 B/cy without a wide-result multiply or AES; the Zen 5
bulk loop is compiler-auto-vectorized.

The passing ordinary-scalar rows are hayahash128, prvhash-128, and FarmHash
CC. hayahash128 leads FarmHash in bulk by 1.63x on M1 and 1.02x on Zen 5, and
has lower small-key latency on both. The narrow Zen 5 result matters: this
supports "fastest passing portable-scalar 128-bit hash measured here," not a
claim that no fast portable-scalar 128-bit predecessor exists. prvhash exposes
187 applicable test groups in this SMHasher3 registration and passes all 187.

Reproduce the adapter with `make -C tests/smhasher3 run`; the competitive
sweep procedure and calibration correction are in
[`docs/smhasher3.md`](docs/smhasher3.md). Raw records are archived under
[`paper/results/`](paper/results/).

### SMHasher3 64-bit shootout (`v0.4.0` archive)

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

- SMHasher3: **188/188 tests passed**, verification value `0x65F2AC15`.
  The `v0.5` digest (see [Status](#status)) has passed the full suite on
  an Apple M1 Pro, with all dispatch shapes measured bit-identical
  locally; the nine-build sweep across four hosts and five compilers
  that `v0.4.0` went through has not been repeated yet.
  (ChibiHash v2 also passes 188/188; v1 fails.)
- hayahash128: **188/188 tests passed** with 128-bit-wide expectations;
  verification values `0x3F0411F4` (canonical little-endian) and
  `0x46140A64` (byte-swapped). Its low word is exactly hayahash64.
- Local harness (`make -C tests run-quality`): strict avalanche
  criterion over input and seed bits, plus exact-collision tests over
  24 structured key sets, including reproductions of the SMHasher3
  keysets that broke earlier iterations. The same target checks
  hayahash128 known answers, streaming equivalence, and low-word
  compatibility across exhaustive boundary lengths and random splits.
  All clean.
- All nine hayahash64 ports are bit-exact against the C reference: shared
  known-answer vectors and the SMHasher3 verification value in every
  port's test suite, plus nightly differential fuzzing that replays a
  fresh randomized C-reference corpus through every language port
  (both JavaScript engines included; the MIPS64 assembly port relies
  on the shared vectors instead).
- hayahash64 endianness and ABI are tested in CI: big-endian s390x, ILP32 wasm32,
  MSVC x64, and MIPS64 under qemu must all reproduce the shared KAT.

[`docs/quality.md`](docs/quality.md) details each of these;
[`paper/AUDIT.md`](paper/AUDIT.md) tracks claim-by-claim evidence.

## Usage

C - copy [`hayahash.h`](hayahash.h) into your project:

```c
#include "hayahash.h"

uint64_t h = hayahash64(buf, len, seed);
hayahash128_t h128 = hayahash128(buf, len, seed);
// h128.lo == h
```

Streaming (identical digests, any update split; both widths share the
same state, and digest() does not modify it, so it can be called
mid-stream):

```c
hayahash64_state st;
hayahash64_init(&st, seed);
hayahash64_update(&st, part1, n1);
hayahash64_update(&st, part2, n2);
uint64_t h = hayahash64_digest(&st);
hayahash128_t h128 = hayahash128_digest(&st);
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
3. **Derived lane constants, length in the finalizer** - the seed is
   premixed once and all lane IVs derive from it, no big per-lane
   literals. The length deliberately stays out of the premix and is
   absorbed in the finalizer through a multiply, which is what makes
   the streaming API's digests identical to one-shot calls.
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

`v0.5` exercised exactly that freedom: the length term moved from the
lane-IV premix to the finalizer, **changing every digest**, because the
premixed spelling made a streaming API with one-shot-identical digests
impossible (every lane IV depended on the total length before the first
byte was read - the same trap that keeps rapidhash one-shot-only today,
with its digests frozen). The move costs no multiplies; the naive
spelling that xors `len * K` in after the final multiplies fails
SMHasher3's SeedZeroes differentials, which is why it sits inside the
`t0` multiply. All nine ports, the SMHasher3 mirror, and the website
simulator were updated in the same change.

The self-contained SMHasher3 implementation lives in
[`tests/smhasher3/`](tests/smhasher3/). It uses SMHasher3's endian-aware
load, store, and rotate primitives and is kept byte-for-byte identical to
`hashes/hayahash.cpp` in the SMHasher3 repository.

## License

Public domain, under the [Unlicense](LICENSE). That covers `hayahash.h`,
every language port, and the mirrored SMHasher3 implementation.

The SMHasher3 harness Makefile is GPL-3.0-or-later; see
[`tests/smhasher3/COPYING`](tests/smhasher3/COPYING). SMHasher3 itself is
fetched at test time and is not part of any release artifact. Redistributing
the combined executable must comply with the GPL, while the copied
`hayahash.cpp` remains public domain.
