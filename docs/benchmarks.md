# Benchmarks

This page keeps the detailed benchmark record behind the high-level snapshot
in the project README. Results are point measurements on the named machines,
compilers, and builds; they are useful for comparing implementations within a
run, not for claiming a universal ranking across all hardware and workloads.

Raw records are archived under [`paper/results/`](../paper/results/). The
[SMHasher3 measurement guide](smhasher3.md#measuring-speed) documents the
competitive sweep and its calibration corrections, while the
[optimization log](optimization/) records the experiments that produced the
current implementation.

## Native 64- and 128-bit cost

The direct cost of selecting the 128-bit result was measured on three hosts.
Each cell is the median of nine calibrated ~40 ms samples on one pinned core;
small-key latency chains each result into the next seed, while the independent
column allows overlap:

<!-- markdownlint-disable MD013 -->

| host / compiler | 8 B chained, 64 / 128 (ns) | 8 B independent, 64 / 128 (ns) | 1 MiB, 64 / 128 (GB/s) |
|---|---:|---:|---:|
| Apple M1 Pro / Apple clang 21 | 7.88 / 9.25 | 2.70 / 3.81 | 30.73 / 30.76 |
| Ryzen AI 9 HX PRO 370 / GCC 16 | 4.29 / 4.90 | 1.97 / 2.67 | 61.47 / 61.31 |
| EPYC 9655 KVM guest / GCC 13 | 4.92 / 5.66 | 2.14 / 3.27 | 54.15 / 53.99 |

<!-- markdownlint-enable MD013 -->

The M1 and Ryzen runs are bare metal. The EPYC is a KVM guest without
frequency control, so use its within-host ratio rather than comparing its
absolute rate with the other machines. `hayahash128.lo` is identical to
hayahash64 in every row; the extra work produces only the high word.

All three rows reflect the
[sixth optimization pass](optimization/pass-6-128-dispatch.md)
(2026-08-09), which brought hayahash128's sustained bulk rate to parity
with hayahash64 on every host - the EPYC guest went from 29.85 to
53.99 GB/s against a 54 GB/s hayahash64 in the same runs. The GCC rows
also carry the pass's follow-up 17..319-byte tiers.

## Baseline wasm32

Baseline wasm32 is the target where the shared state walk pays off most. This
M1 Pro run used Zig 0.16 to compile one module with `-O3`, no SIMD, and no wide
multiply; all timing loops ran inside wasm under Node 26 / V8:

| hash | 8 B chained (ns/hash) | 1 MiB (GB/s) |
|---|---:|---:|
| **hayahash128** | 10.6 | 23.34 |
| hayahash64 | 7.7 | **23.55** |
| ChibiHash v2 | 10.4 | 18.57 |
| XXH3-64 | 8.2 | 17.41 |
| XXH64 | **5.9** | 14.71 |
| rapidhash v3 | 22.6 | 6.43 |

hayahash128 retains 99% of hayahash64's bulk rate in this build. The other
rows return 64 bits; ChibiHash v2 is the passing portable-scalar comparator in
this table, and hayahash128 is 26% faster in bulk here while returning twice
the output width.

The wasm benchmark and native-equivalence check live in
[`tests/wasm/`](../tests/wasm/). Competitor sources are fetched at pinned
upstream revisions.

## ChibiHash comparison

[ChibiHash](https://github.com/N-R-K/ChibiHash) pursues a similar portable
scalar target, which makes it a useful same-category baseline. hayahash is its
own design rather than a fork; specific borrowed techniques are attributed in
the reference header.

The following results were measured on an Apple M1 P-core at about 3.2 GHz
with Apple clang `-O3 -mcpu=native`, against the C reference implementations
of ChibiHash v1/v2 vendored in [`tests/`](../tests/). Run the comparison with
`make -C tests run-bench` from the repository root.

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

ChibiHash v1's 8/16-byte latency wins come from special-cased paths that are
also part of why it fails SMHasher3. Among the 64-bit functions that pass,
hayahash64 is fastest at every size in this M1 table, and its bulk rate is
about 1.6x ChibiHash v2. The 32..128-byte rows reflect the fifth optimization
pass's dispatch choice: clang targets take the compact dispatch, giving up
5..9% at these fixed sizes to run 2..10% faster on mixed-size workloads,
which single-size tables cannot show.

The same comparison on native x86-64 (AMD Zen 5, GCC 16,
`-march=native`) shows hayahash64 ahead of both ChibiHash versions at every
measured size: 128-byte keys take 5.2 vs 6.6 ns independently and stream at
24.3 vs 19.8 GB/s. The 320..512-byte band that ChibiHash v2 led after the
fourth pass flipped to a 27..47% hayahash lead (512 bytes: 42.1 vs 29.3
GB/s), and sustained bulk reached 61.6 vs 31.3 GB/s at 1 MiB after the fifth
pass enabled GCC auto-vectorization for AVX-512. Builds without AVX-512DQ
retain the previous 35 GB/s scalar rate. After the
[sixth pass](optimization/pass-6-128-dispatch.md) moved the 128-bit bulk
dispatch out of line for GCC and gave hayahash128 the same length tiers,
hayahash128 reaches 6.06 ns at 128 bytes and 61.3 GB/s at 1 MiB there -
99.7% of hayahash64's sustained rate.

Dispatch shapes are selected per architecture and compiler. The
[optimization log](optimization/) documents those measurements and their
tradeoffs.

## SMHasher3 128-bit shootout

This is the current `v0.5` candidate against SMHasher3 commit
`51d3cd1ac0aa4934f6aacb44d9d234f50300b6e3`. Speed cells are medians of
three independent, round-robin processes on two bare-metal hosts. Small-key
latency is the 1-31-byte average corrected to one call-overhead baseline per
host; bulk is the fixed 256 KiB average. The full suite was run separately on
the EPYC 9655 with 128-bit-wide expectations.

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

Small-key values are dependent latency, not independent throughput. The raw
SMHasher3 process averages also need correction because its once-per-process
call-overhead calibration shifts the whole 1-31-byte average. The archived
records contain every process value and the exact calculation. MeowHash has no
M1 row because the tested implementation requires x86 AES instructions.

The hayahash128 cells were re-measured on 2026-08-09 after the
[sixth optimization pass](optimization/pass-6-128-dispatch.md)'s
digest-identical dispatch change and its tier follow-up: three fresh process
replicates per host at the same pinned commit, corrected to each host
record's overhead baseline (the refresh records sit beside the sweep records
in `paper/results/`). The competitor rows are from the original sweep. The
small-key cells are unchanged within replicate spread; the Zen 5 bulk change
is the dispatch fix.

XXH3-128 is the bulk leader on both hosts but fails 26 of the 188 test
groups. Among the passers, small-key latency still favors the wide-multiply
pair (a5hash, MuseAir), but the sixth pass reordered Zen 5 bulk: hayahash128
reaches 31.02 B/cy there without a wide-result multiply or AES - past every
wide-multiply passer and within 4% of AES-based MeowHash - because the
outlined dispatch lets GCC auto-vectorize the 128-bit bulk walk. On M1 it
reaches 9.77 B/cy, still behind a5hash's 10.92 bulk on that host.

The passing ordinary-scalar rows are hayahash128, prvhash-128, and FarmHash
CC. hayahash128 leads FarmHash in bulk by 1.73x on M1 and 1.89x on Zen 5, and
has lower small-key latency on both. The narrow Zen 5 result matters: this
supports "fastest passing portable-scalar 128-bit hash measured here," not a
claim that no fast portable-scalar 128-bit predecessor exists. prvhash exposes
187 applicable test groups in this SMHasher3 registration and passes all 187.

Reproduce the adapter with `make -C tests/smhasher3 run`; the competitive
sweep procedure and calibration correction are in [`smhasher3.md`](smhasher3.md).

## SMHasher3 64-bit shootout (`v0.4.0` archive)

Run with `make -C tests/smhasher3 run`, which pins SMHasher3 to an exact
upstream commit. [`smhasher3.md`](smhasher3.md) covers how to reproduce the
results. Small-key numbers are cycles per hash over 1-31-byte keys (lower is
better); bulk is bytes per cycle on 256 KiB keys (higher is better).
Full-suite results are project runs at that pinned commit, not upstream's
published tables. Every cell was re-measured at `v0.4.0`, five replicates per
hash.

<!-- markdownlint-disable MD013 -->

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

<!-- markdownlint-enable MD013 -->

[^avx]: That figure needs the compiler to auto-vectorize the bulk loop,
    which requires AVX-512DQ for its packed 64-bit multiply. A separate
    five-replicate sweep across the three shapes on the same host gives
    31.1 B/cy under GCC with `-march=native`, 31.1 under Clang, and
    **17.6 under GCC without AVX-512DQ** (the 31.2 above is the same
    quantity from the main sweep; the 0.1 is run-to-run). An EPYC 9655
    reproduces the same 1.8x gap under GCC 13 and Clang 18, and an EPYC
    7B13 with no AVX-512 at all cannot reach the vectorized shape however
    it is tuned. The source stays portable scalar C either way - nothing
    here is hand-vectorized - but a machine without AVX-512 gets the lower
    rate. Small-key figures are unaffected.

Two things about the small-key column matter. It is *dependent latency*, not
throughput: SMHasher3 feeds each hash's output back into the next key, so
hashes cannot overlap. It is therefore not comparable to the independent-hash
figures in the ChibiHash tables above. It also needs calibration correction:
SMHasher3 measures call overhead once per process and subtracts it from all 31
lengths. That calibration varied by about two cycles between runs, or 10-40%
on Zen 5. After correction the five Zen 5 replicates agree to within 0.35
cycles. The fanless M1 is noisier, so its figures are medians; the archived
record contains raw and outlier-trimmed dispersion.

gxhash is measured on Zen 5 only. Its SMHasher3 port takes the hardware path
solely under x86 AES, so an ARM number would describe a software-AES fallback
rather than gxhash.

The table uses the two bare-metal hosts. Two further machines, an EPYC 7B13
(Zen 3) and an EPYC 9655 (Zen 5 Turin), were measured as shared KVM guests
without frequency control, so their absolute rates are only indicative. Their
within-host ratios reproduce the conclusions on different silicon and older
compilers: on the EPYC 9655, hayahash leads ChibiHash v2 in bulk by 1.99x and
leads rapidhash v3 21.3 to 19.8. On the Zen 3 host without AVX-512, the lead
over ChibiHash v2 collapses to 1.05x. All three x86 hosts agree that ChibiHash
v2 is ahead on small keys.

The results fall into distinct instruction classes:

- **Wide multiply:** rapidhash v3 passes and stays ahead of hayahash on small
  keys everywhere and on both axes on the M1. It is the more direct choice
  when a native 128-bit multiply result is available, which is precisely the
  instruction hayahash's portability rules exclude. Its Zen 5 bulk lead did
  not survive the fifth pass: 28.6 B/cy against hayahash's 31.2, and only
  where hayahash's bulk loop vectorizes.
- **SIMD or hardware acceleration:** these win bulk decisively where the
  hardware provides them. XXH3-64 reaches 48.9 B/cy on Zen 5 with AVX-512 and
  12.6 B/cy on the M1 with NEON, and gxhash reaches 64.7 B/cy on Zen 5 with
  AES-NI, against hayahash's 31.2 and 9.8. Both fail the suite, but they are
  not beaten on speed.
- **Portable 64-bit scalar:** the only other hash in this sweep that meets
  hayahash's constraints and passes is ChibiHash v2. hayahash leads it in
  bulk on both hosts, by 1.61x on the M1 and 1.99x on Zen 5. On small keys the
  two split: hayahash takes 11% less time on the M1 but 30% more on Zen 5,
  where ChibiHash v2's simpler short path wins the 8-15-byte band by about 5.3
  cycles. mx3.v3 is the remaining ordinary-multiply candidate and fails 26
  tests.

Within this deliberately bounded portable-scalar comparison, hayahash is the
fastest hash measured in sustained bulk that passes the complete SMHasher3
suite on both hosts. On Zen 5 it also beats every measured wide-multiply hash
in bulk, including rapidhash v3; only XXH3-64 and gxhash stream faster, and
both fail the suite. That result is conditional on vectorization: without
AVX-512DQ hayahash falls back to 17.6 B/cy, behind rapidhash's 28.6.

hayahash does not hold a small-key lead everywhere - ChibiHash v2 is ahead on
Zen 5 - so that advantage is architecture-dependent. The comparison covers
two primary hosts and the roughly 250 hashes tracked by SMHasher3; it is not a
universal record claim.
