# Fourth pass: transition band, stock toolchains, algorithm screens

Part of the [optimization log](README.md). This is the historical
record of the fourth pass.

This pass worked through items 1..8 of the standing
[experiment list](README.md#the-experiment-list) on the same pinned
Zen 5 box and the Apple M1, with two toolchain additions:
distribution clang 22.1.8 with glibc beside GCC 16.1 on the Zen 5,
and an MSVC x64 CI job. Two pieces of methodology carried the pass: a
mixed-size A/B mode (deterministic pseudo-random offset/length
sequences, so per-hash size branches stay unpredictable while base
and candidate hash identical work), with mixed and small-delta
comparisons repeated under swapped candidate link order to cancel
code-placement bias (measured at +-1.7% for clang, well above some of
the effects under test); and a reduced-width structural screen for
the digest-changing candidates.

## Cheaper bulk exit: hoisted bookkeeping and a pinned K

Two output-identical changes attacked the 320..512-byte band where
ChibiHash v2 still led. Running the two-block loop on a precomputed
end pointer, with the post-loop remainder hoisted to entry where
there is ILP slack, removed GCC's stack-slot spills and post-loop
remainder rebuild: +2.5..4.6% independent throughput and +1..4.7%
chained latency on Zen 5 GCC, +12..56% and +57..77% on Zen 5
clang 22 (which had kept redundant exit state in its steady-state
schedule), +3.6..6.1% band throughput and +4.8..5.2% sustained bulk
on M1. Separately, pinning K in a register after IV derivation
stopped GCC rematerializing the 10-byte movabs four times in the
exit path: +0.2..1.7% and +0.1..1.3% on Zen 5 GCC with other
compilers' binaries unchanged by the gate. Bit-test spellings of
the trailing remainder checks (l & 64, l & 32, len & 31) gained
another 0.5..1% under GCC but cost stock clang 22 a quarter of its
bulk throughput (a register-pressure spill cliff), so the plain
comparisons stay.

## Stock clang 22: exclusions re-validated, the reason moved

The clang tier/mid-round exclusion from the
[third pass](pass-3-zen5.md) rested on clang 21 via zig/musl.
Re-measured with distribution clang 22.1.8/glibc on the same silicon
(digest-exactness checked, every delta link-order-swap controlled),
the exclusion survives but its mechanism does not: forcing the wide
dispatch now wins 4..16% independent throughput at fixed
32..319-byte sizes with chained latency flat, i.e. the 26..48%
callee-saved chained collapse is gone from the current long path.
What remains is a front-end cost that only mixed-size workloads
expose: with the whole tier chain live and per-hash branch targets
unpredictable, 64..512-byte mixes lose 15% independent and 12%
chained, 128..319 mixes lose 8% and 7%, against +6..9% gains on
single-band (192..255) and small-heavy (1..319) mixes. A
general-purpose default cannot assume single-band input sizes, so
x86-64 clang keeps the compact dispatch, now for front-end rather
than register reasons (the header comment was updated to match).

Disabling the two-block bulk unroll under stock clang 22 is a wash
(+1..2.8% at 320..512 bytes, -1.3..-1.6% sustained bulk, symmetric
under the swap): clang 22 auto-vectorizes the eight-lane bulk loop
with AVX-512 (GCC 16 does not; this is where its roughly 2x
sustained-bulk lead over GCC on this box comes from), and the
vector loop no longer depends on the manual unroll. The unroll
stays: GCC 16 (+4..10%) and Apple M1 (+4..5%) still need it.

MSVC x64 went from untested to a CI conformance job: the native KAT
generator gained a hayahash-only build so no competitor sources are
fetched, and cl /std:c11 /O2 /W4 now builds it and the quality
harness on every push, diffing digests against the committed KAT.
Shared-runner timing is too noisy to gate on, so MSVC performance
measurement remains an open item.

## A 192..255-byte tier, decided by mixed workloads

The tier chain grew one band: six spelled-out mid rounds plus the
same optional-round-and-tail shape as the earlier tiers, leaving
the mid loop to serve only 256..319-byte keys. Output-identical,
and gated by the tier macro, so x86-64 clang and wasm are
untouched. The experiment list's concern that the tier's I-cache
footprint would only show in mixed workloads is what the mixed A/B
mode was built for, and it answered in the tier's favor: Zen 5 GCC
gains +7.2% independent throughput at 192 bytes and +5.8..6.9% at
256..319 (the residual mid loop compiles better with its narrower
range) at fixed sizes, and mixed workloads containing the band gain
4.1..9.5% independent and 1.4..3.5% chained, symmetric under link
swap, with 1..64-byte mixes flat. M1: +8.1% at 192 bytes fixed;
mixed 192..255 +8.3% independent and +1.8% chained.

## Reduced-width screens for the algorithm candidates

Items 4..8 of the experiment list propose digest-changing bulk-cell
and fold redesigns. Before any full-width work, a reduced-width
harness reimplements the long-path skeleton (eight-lane bulk over
8-word blocks, per-block checkpoint, 8-to-4 fold, tail, finalizer)
at W = 8 and 16 bits with pluggable cells and folds, and runs three
screens: an exhaustive-differential erasure scan through one cell
application, the rotation-orbit ladder family generalized to width W
(carry-matched per-stripe differences following the rotation for a
full orbit), and end-to-end collision counting against binomial
expectation over sparse, repeat-block, and top-bit-stripe key
families. The harness validates by construction: the current cell
with the checkpoint removed collides on every carry-matched ladder
trial (16384 of 16384 at W=16, 8192 of 8192 at W=8), and the
checkpoint alone suppresses the family exactly (0 collisions at
both widths). No candidate produced erasures in the reset screen;
everything below fell to the other two gates.

- **Two-word cross-injected cell (item 5):** rejected at the
  screen. Top-bit-only stripe patterns collide at 6.7x binomial
  expectation at W=16 (219868 against 32768 expected) and the
  ladder leaks at W=8: the cross-injection is still too separable.
  The cell spends the same two multiplies per 16 bytes as the
  current one, so there was no speed upside to trade against.
- **Dynamic low-product pair cell (item 8):** rejected on two
  independent grounds. The generalized ladder leaks at a stable
  per-trial rate at W=8 (42 collisions in 8192 trials, 505 in
  131072; the current cell blocks the same family exactly, 0 in
  the same runs), so the defense degrades from structural to
  carry-luck. And the op-count premise fails: with the
  feed-forward needed to reach even that quality level, the cell
  costs 11 ops per word pair against the current 8, while halving
  multiplies buys nothing on the measured cores, whose two
  multiply pipes are not the bulk loop's bottleneck. One multiply
  per 16 bytes remains attractive only if the total op count also
  drops, and the feed-forward requirement prevents that.
- **Non-separable 8-to-4 fold (item 4):** the multiply-free
  add/xor/rotate butterfly is rejected at the screen: top-bit
  collisions run 12 sigma above expectation at W=16 (34947 against
  32768 +- 181), direct evidence that the fold multiplies suppress
  exactly the family the design defends against. A halved variant
  (two multiplies, every upper lane still reaching two lower
  lanes, two of them via unmultiplied rotated copies) passes both
  screen widths and the full-width SAC and collision harness, but
  was rejected at the benchmark stage: M1 -0.5..-2.2% throughput
  and -0.1..-1.5% latency, Zen 5 GCC -0.1..-0.9% throughput, Zen 5
  clang +-0.2% once the +-1.7% placement bias is swap-cancelled.
  The butterfly adds one level to the fold's dependency chain, and
  the fold runs once per hash, so two saved multiplies never pay
  it back. The planned SMT stage was skipped as moot after the
  performance rejection.
- **Distance-coded block injection (item 6):** rejected by cost
  analysis. The existing checkpoint costs one add per block and
  already blocks the orbit family exactly (fresh reduced-width
  evidence above); two or three rotated checksums cost 7..11 ops
  per block and would still need the GF(2) nullspace audit before
  benchmarking. As defense-in-depth it fails the clear-win bar
  while quality screens show nothing to defend, and the
  cheaper-cell family it might have enabled (item 8) is
  independently rejected.
- **High-half surrogate (item 7):** rejected by cost analysis. An
  extra 32x32 product with combines per word pair adds roughly a
  third to the bulk op count to buy quality margin that no current
  screen, SAC run, or collision family shows lacking. Revisit only
  if the SMHasher3 rerun surfaces a cross-half weakness.

Items 9 and 10 stay deferred as research infrastructure; the
reduced-width harness built here is their seed.
