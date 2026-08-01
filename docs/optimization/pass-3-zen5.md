# Third pass: native x86-64 (Zen 5)

Part of the [optimization log](README.md). This is the historical
record of the third pass.

The Rosetta-proxied x86-64 decisions of the
[second pass](pass-2-dispatch.md) were re-measured on an AMD Ryzen
AI 9 HX PRO 370 (Zen 5, pinned to a 5.16 GHz classic core with the
performance governor; A/B noise floor within 0.1%). GCC 16.1 with
`-march=native` is the primary toolchain; clang 21 numbers come from
a zig-cc musl cross build run on the same machine. Everything below
kept the exhaustive bit-exactness gate from the second pass.

The headline lesson: Rosetta 2 predicted the wrong sign for two of
three experiments, and GCC and clang want different dispatch shapes
on identical silicon. Per-compiler measurement is not optional here.

## Validate the second pass, fix the lost bulk invariant

The committed second pass held up natively for short keys (+2..7%
independent-hash throughput across 17..192 bytes) but showed a
1..3.5% regression from 320 bytes up that Rosetta never surfaced: the
dispatch's `len >= 320` guarantee dies at the noinline boundary, so
GCC kept a live remaining-length computation in the bulk loop exit
and guarded the post-loop remainder with a cmov chain for a zero-trip
case that cannot happen. Restating the invariant inside the outlined
function with `__builtin_unreachable()` (`__assume` under MSVC)
restored the fused pointer-compare exit: +2..5% on 320..1024-byte
keys, M1 and wasm unchanged.

## The two-block bulk unroll transfers to x86-64

Native Zen 5 does not reproduce Rosetta's 7% penalty: the unroll
gains 4..10% (GCC) and 10..48% (clang) on 320-byte-and-up keys, with
chained latency flat for both. The clang upper end is a second-order
effect worth naming: with half as many loop exits, the out-of-order
window overlaps independent hashes much more deeply, so 320..1024-
byte independent-hash throughput jumps far past the loop's
steady-state rate. Now enabled for x86-64 alongside AArch64.

## Tiers and mid rounds: GCC yes, clang no

The full AArch64 dispatch shape (17..191 tiers plus 64-byte mid
rounds) transfers cleanly to x86-64 GCC: +5..15% independent-hash
throughput on 32..191-byte keys, +1% on 17..31, flat chained latency,
at worst 1% taken from 192..319. Under clang 21 on the same machine
the identical shapes fail in two distinct ways: the tiers alone cost
6% on 17..31-byte keys and 1..4% on 192..319 (against 1..10% gains in
between), and the mid rounds collapse chained latency by 26..48%
across the board while independent-hash throughput doubles at some
sizes. The mechanism is the familiar one from the
[outlining note](pass-2-dispatch.md#outline-the-long-input-path-into-a-non-inlined-function):
clang assigns the enlarged monolithic function callee-saved
registers, and their unconditional save/restore traffic lands on the
seed dependency chain of back-to-back hashes. GCC shrink-wraps the
same shapes without touching the chain. A single
`HAYAHASH64_INTERNAL_TIERS` gate now selects the wide dispatch for
AArch64 and x86-64-non-clang, and the compact one for x86-64 clang
(unmeasured targets also stay compact).

(The [fifth pass](pass-5-vectorization.md) later re-measured mixed-size
workloads on the M1 and moved AArch64 clang to the compact dispatch as
well; the gate is now `(aarch64 || x86_64) && !clang`.)

## Drop the product guard for GCC

The empty-asm guard on computed products exists to stop clang from
distributing a following rotate into two independent multiplies. GCC
never applies that transform, so for it the barrier only pinned
values and serialized scheduling around the folds and the finalizer,
squarely on the chained-hash critical path. Making the guard
clang-only gains 0.6..4.1% chained latency across all measured
17..319-byte keys and 1..3% independent-hash throughput on most of
them, with bulk rates and every clang target (wasm module included)
unchanged.

## Reference numbers and an open transition band

Against the vendored ChibiHash baselines on the same Zen 5 (GCC 16),
hayahash leads at every size below 320 bytes (128-byte keys: 5.2 vs
6.5 ns independent) and in sustained bulk (35.4 vs 31.5 GB/s at
1 MiB). ChibiHash v2 leads by 2..8% only in the 320..512-byte
transition band, where the eight-lane bulk loop is still amortizing
its lane setup and fold; the band shrank with the invariant fix but
did not close. The bulk threshold itself is an algorithm parameter
(digests change with it), so any further gain there must come from
cheaper entry/exit code, not from moving the boundary.

(The transition band was closed for good by the
[fifth pass](pass-5-vectorization.md)'s vectorized bulk spelling.)
