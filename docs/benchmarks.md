# Benchmarks

Measured throughput at the current digest. These are point measurements on
the named machines, compilers, and builds: useful for comparing
implementations within a run, not for claiming a ranking across all hardware
and workloads. Raw records with provenance are under
[`paper/results/`](../paper/results/); the
[SMHasher3 measurement guide](smhasher3.md#measuring-speed) documents the
competitive sweep and its calibration corrections.

The design target behind these numbers is analytic rather than empirical:
the bulk loop is shaped so that multiplier issue, not dependency latency,
is the binding resource. That argument is in the
[working paper](../paper/)'s cost model and needs no measurement; the tables
here are the check on it.

## Native 64- and 128-bit cost

The direct cost of selecting the 128-bit result, on three hosts. Each cell
is the median of nine calibrated ~40 ms samples on one pinned core.
Small-key latency chains each result into the next seed; the independent
column allows overlap.

<!-- markdownlint-disable MD013 -->

| host / compiler | 8 B chained, 64 / 128 (ns) | 8 B independent, 64 / 128 (ns) | 1 MiB, 64 / 128 (GB/s) |
|---|---:|---:|---:|
| Apple M1 Pro / Apple clang 21 | 7.88 / 9.25 | 2.70 / 3.81 | 30.73 / 30.76 |
| Ryzen AI 9 HX PRO 370 / GCC 16 | 4.29 / 4.90 | 1.97 / 2.67 | 61.47 / 61.31 |
| EPYC 9655 KVM guest / GCC 13 | 4.92 / 5.66 | 2.14 / 3.27 | 54.15 / 53.99 |

<!-- markdownlint-enable MD013 -->

The M1 and Ryzen runs are bare metal. The EPYC is a KVM guest without
frequency control, so read its within-host ratio rather than its absolute
rate. `hayahash128.lo` is identical to hayahash64 in every row; the extra
work produces only the high word, and bulk throughput is at parity on all
three hosts.

## Baseline wasm32

Baseline wasm32 is where the primitive-class constraint pays off most: no
SIMD, no widening multiply, only `i64.mul`. This M1 Pro run used Zig 0.16 to
compile one module with `-O3`; all timing loops ran inside wasm under
Node 26 / V8.

| hash | 8 B chained (ns/hash) | 1 MiB (GB/s) |
|---|---:|---:|
| **hayahash128** | 10.6 | 23.34 |
| hayahash64 | 7.7 | **23.55** |
| ChibiHash v2 | 10.4 | 18.57 |
| XXH3-64 | 8.2 | 17.41 |
| XXH64 | **5.9** | 14.71 |
| rapidhash v3 | 22.6 | 6.43 |

rapidhash's 3.7x deficit against hayahash64 here is the primitive-class
argument in one cell: its central mix needs a wide product that this target
does not have. hayahash128 retains 99% of hayahash64's bulk rate in the same
build while returning twice the output width. The other rows return 64 bits.

The wasm benchmark and its native-equivalence check live in
[`tests/wasm/`](../tests/wasm/). Competitor sources are fetched at pinned
upstream revisions.

## ChibiHash comparison

[ChibiHash](https://github.com/N-R-K/ChibiHash) targets the same portable
scalar class, which makes it the useful same-category baseline: differences
are between design choices, not instruction sets. hayahash is its own design
rather than a fork; borrowed techniques are attributed in the reference
header.

Measured on an Apple M1 P-core at about 3.2 GHz with Apple clang
`-O3 -mcpu=native`, against the C reference implementations vendored in
[`tests/`](../tests/). Run it with `make -C tests run-bench`.

Large-input throughput (GB/s, higher is better):

| size | chibihash v1 | chibihash v2 | hayahash64 | hayahash128 |
|---:|---:|---:|---:|---:|
| 64 | 7.70 | 10.04 | **13.00** | 10.39 |
| 256 | 15.84 | 18.00 | **20.67** | 19.03 |
| 1024 | 15.62 | 19.72 | **27.55** | 26.28 |
| 16384 | 14.76 | 19.00 | **30.41** | 30.38 |
| 1048576 | 14.73 | 18.99 | 30.73 | **30.76** |

Small-input latency (ns/hash, seed-chained, lower is better):

| len | chibihash v1 | chibihash v2 | hayahash64 | hayahash128 |
|---:|---:|---:|---:|---:|
| 4 | 9.62 | 10.00 | **7.88** | 9.23 |
| 8 | **6.32** | 9.54 | 7.88 | 9.25 |
| 16 | **6.70** | 9.77 | 7.87 | 9.25 |
| 32 | 12.13 | 11.34 | **8.41** | 10.52 |
| 64 | 13.98 | 12.91 | **9.68** | 11.74 |
| 128 | 18.27 | 16.52 | **12.18** | 14.46 |

Small-input throughput (ns/hash, independent hashes, lower is better):

| len | chibihash v1 | chibihash v2 | hayahash64 | hayahash128 |
|---:|---:|---:|---:|---:|
| 4 | 7.13 | 4.21 | **2.76** | 3.83 |
| 8 | 4.41 | 4.42 | **2.70** | 3.81 |
| 16 | 5.05 | 4.88 | **2.70** | 3.81 |
| 32 | 6.81 | 5.17 | **3.72** | 4.98 |
| 64 | 8.38 | 6.38 | **4.92** | 6.15 |
| 128 | 11.69 | 8.90 | **7.38** | 8.53 |

ChibiHash v1's 8- and 16-byte latency wins come from special-cased paths that
are also part of why it fails SMHasher3. Among the 64-bit functions that
pass, hayahash64 is fastest at every size in this table, with about 1.6x
ChibiHash v2's bulk rate — the ratio the cost model predicts from dependency
placement alone, since the two loops spend the same five operations per
8-byte stripe. The 32..128-byte rows reflect the compact dispatch clang
targets take: it gives up 5..9% at these fixed sizes to run 2..10% faster on
mixed-size workloads, which single-size tables cannot show.

The same comparison on native x86-64 (AMD Zen 5, GCC 16, `-march=native`)
puts hayahash64 ahead of both ChibiHash versions at every measured size: 128
byte keys take 5.2 vs 6.6 ns independently and stream at 24.3 vs 19.8 GB/s,
512-byte keys at 42.1 vs 29.3 GB/s, and sustained bulk reaches 61.6 vs
31.3 GB/s at 1 MiB where GCC auto-vectorizes the bulk loop for AVX-512DQ.
Builds without AVX-512DQ get about 35 GB/s from the same portable source.
hayahash128 reaches 6.06 ns at 128 bytes and 61.3 GB/s at 1 MiB there,
99.7% of hayahash64's sustained rate.

Which shape a target compiles is documented in
[`implementation.md`](implementation.md#compiled-shapes); every shape
produces the same digest.

## SMHasher3 128-bit shootout

Against SMHasher3 commit `51d3cd1ac0aa4934f6aacb44d9d234f50300b6e3`. Speed
cells are medians of three independent, round-robin processes on two
bare-metal hosts. Small-key latency is the 1-31-byte average corrected to one
call-overhead baseline per host; bulk is the fixed 256 KiB average. The full
suite was run separately on an EPYC 9655 with 128-bit-wide expectations.

<!-- markdownlint-disable MD013 -->

| 128-bit hash | M1 small | M1 bulk | Zen 5 small | Zen 5 bulk | full suite | peak-performance requirement |
|---|---:|---:|---:|---:|---|---|
| **hayahash128** | 38.53 | 9.77 | 13.71 | 31.02 | pass | ordinary scalar source; auto-vectorized Zen 5 bulk |
| MuseAir-128 | 24.26 | 8.65 | 7.44 | 22.80 | pass | 64x64-to-128-bit multiply |
| a5hash-128 | **22.29** | 10.92 | **6.00** | 22.99 | pass | 64x64-to-128-bit multiply |
| MeowHash | - | - | 28.64 | 32.22 | pass | x86 AES instructions |
| XXH3-128 | 30.73 | **12.64** | 11.92 | **48.91** | fail (26) | wide multiply; SIMD for peak bulk |
| t1ha2-128 | 64.58 | 5.86 | 21.03 | 16.24 | pass | 64x64-to-128-bit multiply |
| SpookyHash2-128 | 53.37 | 4.20 | 24.39 | 15.35 | fail (10) | ordinary 64-bit operations |
| prvhash-128 | 67.45 | 1.00 | 25.49 | 2.58 | pass (187/187) | ordinary 64-bit operations |
| FarmHash-128.CC.seed1 | 60.09 | 5.64 | 21.09 | 16.45 | pass | ordinary 64-bit operations |

<!-- markdownlint-enable MD013 -->

Small-key values are dependent latency, not independent throughput, and are
therefore not comparable with the independent-hash figures above. The raw
SMHasher3 process averages also need correction, because its once-per-process
call-overhead calibration shifts the whole 1-31-byte average; the archived
records contain every process value and the exact calculation. MeowHash has
no M1 row because the tested implementation requires x86 AES.

Reading the table:

- XXH3-128 is the bulk leader on both hosts and fails 26 of 188 test groups.
- Among passers, small-key latency favors the wide-multiply pair (a5hash,
  MuseAir) on both hosts.
- In Zen 5 bulk, hayahash128 reaches 31.02 B/cy without a wide-result
  multiply or AES — past every wide-multiply passer and within 4% of
  AES-based MeowHash — because the outlined dispatch lets GCC auto-vectorize
  the 128-bit bulk walk. On M1 it reaches 9.77 B/cy, behind a5hash's 10.92.
- The passing ordinary-scalar rows are hayahash128, prvhash-128, and FarmHash
  CC. hayahash128 leads FarmHash in bulk by 1.73x on M1 and 1.89x on Zen 5,
  with lower small-key latency on both. prvhash exposes 187 applicable test
  groups in this registration and passes all 187.

That supports "the fastest passing portable-scalar 128-bit hash measured
here," not a claim that no fast portable-scalar 128-bit predecessor exists.

Reproduce the adapter with `make -C tests/smhasher3 run`; the sweep procedure
and the calibration correction are in [`smhasher3.md`](smhasher3.md).

## What is not measured here

- **A 64-bit competitive sweep at the current digest.** hayahash64's position
  against rapidhash, wyhash, komihash, XXH3-64, gxhash, and mx3 has not been
  re-measured since the digest changed. Until it is, this page makes no
  64-bit competitive claim beyond the ChibiHash baseline above.
- **Multi-host, multi-compiler coverage of every dispatch shape at speed.**
  Conformance across shapes is enforced; their relative speed is measured only
  on the hosts named above.
- **MSVC x64 timing.** Conformance is in CI; shared-runner timing is too noisy
  to publish.
