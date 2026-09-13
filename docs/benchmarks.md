# Benchmarks

**Both widths have similar bulk throughput; 128-bit costs more on short
inputs.** These runs measure the current digest on the named hosts and
builds. Compare results within a run, then measure your own workload.

- [Native 64 vs 128 bits](#native-64--and-128-bit-cost)
- [WebAssembly](#baseline-wasm32)
- [ChibiHash](#chibihash-comparison)
- [128-bit comparison](#smhasher3-128-bit-shootout)
- [Coverage gaps](#what-is-not-measured-here)

[Raw records](../paper/results/) include host, compiler, and source details.
The [SMHasher3 guide](smhasher3.md#measuring-speed) covers reproduction and
timing corrections.

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
frequency control; compare widths within its row, not absolute rates
against other hosts. Both widths share one pass over the input.

## Baseline wasm32

Zig 0.16 compiled one baseline wasm32 module with `-O3`: no SIMD or
wide-multiply instruction. Timing loops ran inside wasm under Node 26 / V8
on an M1 Pro, excluding the JS boundary.

| hash | 8 B chained (ns/hash) | 1 MiB (GB/s) |
|---|---:|---:|
| **hayahash128** | 10.6 | 23.34 |
| hayahash64 | 7.7 | **23.55** |
| ChibiHash v2 | 10.4 | 18.57 |
| XXH3-64 | 8.2 | 17.41 |
| XXH64 | **5.9** | 14.71 |
| rapidhash v3 | 22.6 | 6.43 |

hayahash128 retains 99% of hayahash64's bulk rate here. All other rows
return 64 bits. Baseline wasm must emulate the wide multiplication used by
rapidhash, so these rankings need not hold in native builds.

The wasm benchmark and its native-equivalence check live in
[`tests/wasm/`](../tests/wasm/). Competitor sources are fetched at pinned
upstream revisions.

## ChibiHash comparison

[ChibiHash](https://github.com/N-R-K/ChibiHash) uses the same portable
arithmetic, making it a useful baseline.

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

hayahash64 is faster than ChibiHash v2 at every size shown. ChibiHash v1
wins some short-input timings but fails SMHasher3. The compact clang
dispatch trades 5–9% at fixed 32–128-byte sizes for 2–10% on mixed-size
workloads, which these tables do not show.

On AMD Zen 5 with GCC 16 (`-march=native`), 1 MiB throughput reaches
61.6 GB/s for hayahash64 and 61.3 GB/s for hayahash128, versus 31.3 GB/s for
ChibiHash v2. GCC auto-vectorizes the bulk loop for AVX-512DQ. Without it,
the same portable source reaches about 35 GB/s for hayahash64.

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

| 128-bit hash | M1 small (cy) | M1 bulk (B/cy) | Zen 5 small (cy) | Zen 5 bulk (B/cy) | full suite | peak-performance requirement |
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

- XXH3-128 has the highest bulk throughput and fails 26 of 188 test groups.
- Among hashes that pass, a5hash has the lowest small-key latency.
- hayahash128's Zen 5 bulk result uses compiler auto-vectorization.
- hayahash128 leads the passing ordinary-scalar implementations in this
  comparison. prvhash has 187 applicable groups; the others have 188.

Reproduce the adapter with `make -C tests/smhasher3 run`; the sweep procedure
and the calibration correction are in [`smhasher3.md`](smhasher3.md).

## What is not measured here

- **A native 64-bit competitive sweep at the current digest.** hayahash64's position
  against rapidhash, wyhash, komihash, XXH3-64, gxhash, and mx3 has not been
  re-measured since the digest changed. Until it is, this page makes no
  64-bit competitive claim beyond the ChibiHash baseline above.
- **Multi-host, multi-compiler coverage of every dispatch shape at speed.**
  Conformance across shapes is enforced; their relative speed is measured only
  on the hosts named above.
- **MSVC x64 timing.** Conformance is in CI; shared-runner timing is too noisy
  to publish.
