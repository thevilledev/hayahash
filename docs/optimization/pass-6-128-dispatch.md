# Sixth pass: hayahash128 dispatch, per-compiler bulk shapes

Part of the [optimization log](README.md). This is the historical
record of the sixth pass, the first whose subject is `hayahash128`
rather than `hayahash64`; the 64-bit function is untouched and every
change here is output-identical (the differential suite in
[`tests/hash128.c`](../../tests/hash128.c) and the SMHasher3
verification values 0x3F0411F4/0x46140A64 gate each experiment).

The starting point was the dual-width cost table: on Zen 5 GCC,
hayahash128 sustained 33.9 GB/s against hayahash64's 61.6, and on the
M1 it held 91.5% of the 64-bit bulk rate. hayahash128 had shipped as a
single inline function carrying the whole 8-lane bulk loop, the shape
hayahash64 abandoned in the [second pass](pass-2-dispatch.md).
Toolchains: Apple clang 21 on the M1 (unpinned), GCC 16.1 and stock
clang 22.1.8 on the Zen 5 box pinned to core 0, performance governor.

## Methodology: in-process A/B

Two-binary comparisons on Zen 5 moved the *unchanged* hayahash64
column by up to 0.44 ns at 17..64-byte independent throughput - code
layout luck, not the change under test. All decisions below therefore
came from an in-process harness: the old and new headers compiled in
two translation units (everything in the header is `static`), all four
functions benchmarked in one process with rotated candidate order.
The old64/new64 pair is byte-identical code compiled twice; its spread
(±0.1 ns, ±1-2%) is the noise floor that a claimed effect has to beat.

## GCC: outlining the long path doubles bulk

Moving the >= 320-byte path into a `noinline`
`hayahash128_internal_long` - same two-block loop, dispatch-invariant
restatement, and VECGCC array spelling as `hayahash64_internal_long` -
took Zen 5 GCC bulk from 33.9 to 62.3 GB/s at 1 MiB (183%, now within
noise of hayahash64), +26% at 320 bytes rising through +65% at 1 KiB.
The mechanism is the [fifth pass](pass-5-vectorization.md)'s SLP story
finishing the job: inlined into hayahash128, SRA scalarizes the
`hv[]` store seed away and the loop stays scalar - the old inline
path contains zero `vpmullq`; the outlined copy has the same two as
the 64-bit function. The 320..383-byte band had actually been *slower*
than the sub-320 mid loop (23.0 vs 25.9 GB/s); outlining fixed that
inversion too. Every sub-320 row measured flat to slightly better
(GCC shrink-wraps the never-taken call), and 8-byte independent
throughput improved 2.78 to 2.59 ns.

## clang: outlining backfires twice, inline two-block wins

The same outline under Apple clang 21 lifted M1 bulk 28.0 to 30.6 GB/s
but destabilized the inline 17..319 path it left behind: chained
17..24-byte latency +2.0 ns (+18%) and independent 96..128-byte time
+20..24%, with the disassembly showing only benign-looking differences
(a frame pair and a rematerialized K, both off the seed chain).
Attempts to tune it stayed whack-a-mole: dropping the AArch64 K guard
fixed chained and broke independent 32..96 bytes (+3.0/+3.4 ns);
outlining the mid path as its own `noinline` function broke
independent throughput at every mid size (extra call depth kills
cross-hash overlap, up to +4.4 ns at 96 bytes).

Stock clang 22 on Zen 5 failed differently: the shipped inline shape
had auto-vectorized the mid loop (the latency/throughput trade the
[third pass](pass-3-zen5.md) guards block for hayahash64), so chained
32..319-byte latency sat at 10.1..19.6 ns while independent mid-size
throughput looked anomalously good. Outlining restored a scalar mid
path (chained down to 5.6..13.5 ns) but pushed 8-byte *chained*
latency from 4.73 to 8.00 ns; `preserve_most` on the callee
reproduced the outlined numbers exactly, ruling out caller-side
register clobbering as the mechanism, and the cause was not
identified.

What ended the whack-a-mole: keep clang's bulk loop inline - the old
shape whose sub-320 schedule both clangs already compile well - and
upgrade only the loop body to the outlined path's two-block spelling.
That reached the outlined bulk rate on both machines with every
sub-320 row at parity:

- M1: 1 MiB 28.0 -> 30.6 GB/s (109% of old, 100% of hayahash64),
  320..1024 bytes +6..7%, sub-320 and short paths ±2%.
- Zen 5 clang: 8-byte chained stays 4.73 ns; chained 32..319 improves
  2.9..4.3 ns over the shipped shape (the two-block loop's presence
  changes the vectorizer's mid-loop decision); bulk 63.8 -> 64.2 GB/s.
  The old shape's anomalous vectorized independent throughput at
  192..319 bytes gives back 3..6% large-input and ~0.3-0.4 ns
  independent at 256..319 - accepted, as the project values the
  latency chain (same trade the product guards make for hayahash64).

## Result

`HAYAHASH128_INTERNAL_INLINEBULK` selects the shape:
clang-and-not-wasm keeps the bulk loop inline with the two-block
unroll; everything else calls the outlined
`hayahash128_internal_long`; wasm keeps the single-block inline loop
(module size, and the pre-change text) - three shapes, all
digest-identical, mirrored in the SMHasher3 translation. Final
canonical sweeps (`tests/bench.c`, 9 samples):

| host / compiler | 1 MiB before | 1 MiB after | of hayahash64 |
|---|---:|---:|---:|
| M1 / Apple clang 21 | 27.31 GB/s | 30.76 GB/s | 100.1% |
| Zen 5 / GCC 16 | 33.88 GB/s | 61.29 GB/s | 99.7% |

Small-key rows moved little: M1 8-byte independent 3.96 -> 3.81 ns,
Zen 5 4.93 -> 4.88 ns chained / 2.77 -> 2.61 ns independent.

## Open after this pass

- The 17..319-byte band still trails hayahash64 by roughly the high
  word's finalizer cost (+1.0..1.3 ns at every size on both hosts, the
  expected price of the second fmix chain on these harnesses). GCC
  tiers for hayahash128 (hayahash64's tiers bought 3..13% there) would
  attack the base cost, not the finalizer, for ~330 duplicated lines;
  unmeasured and deferred.
- The EPYC 9655 KVM row of the cost table predates this pass and needs
  a re-run; its GCC 13 targets znver4, which satisfies the VECGCC
  gate, so its within-host ratio is expected to move the same way.
- The Zen 5 stock-clang 8-byte chained regression under the outlined
  shape was routed around, not explained.
