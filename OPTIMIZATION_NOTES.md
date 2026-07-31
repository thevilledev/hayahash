# Hayahash optimization and design notes

This file records the optimization passes performed against the development
version of `hayahash64`. The scope is intentionally narrow: a 64-bit result,
portable scalar operations, ordinary 64x64-to-64-bit multiplication, no SIMD,
and no dependency on the high half of a 128-bit product.

The main lesson was that instruction-count improvements and structural
collision analysis have to proceed together. Several attractive, faster
cores passed ordinary avalanche tests but admitted algebraically constructed
collisions.

## Changes retained

### Preserve one multiply across rotate

Clang can turn `rotl(x * C, n)` into two independent multiplies, shortening
the dependency chain at the cost of an extra multiply and another materialized
constant. An empty compiler barrier, following the technique used by
[xxHash](https://github.com/Cyan4973/xxHash/blob/dev/xxhash.h), keeps the
already-computed product opaque. The same barrier helps schedule the two XOR
halves of the tail injections and keeps the common multiplier in a register on
AArch64. These barriers do not change the digest.

### XOR the absorb into lane state

The long-path recurrence is now:

```text
t = w + rotl(previous_word, 27)
h = (h XOR t) * K
```

LLVM could reassociate the former outer addition in ways that lengthened the
critical path. The XOR form emitted a shorter recurrence on the measured
AArch64 target and retained the deliberately non-linear addition inside the
chained word transform.

### Use a one-multiply long-path avalanche

Inputs longer than 16 bytes have already crossed per-word multiplication and
non-linear lane folding. Their finalizer was reduced from two multiplies to:

```text
x ^= x >> 37
x *= K
x ^= x >> 32
```

This is a bijection, saves one serial multiply, and passed the complete quality
gate as part of the full hash. It is not recommended as a standalone mixer or
as a replacement for the stronger two-multiply short-path finalizer.

### Specialize 17–31 bytes on AArch64

A straight-line spelling of the existing four overlapping loads is profitable
on AArch64. It is nested below the existing long-input branch, so it adds no
test to larger keys, is bit-identical to the generic tail, and avoids harming
x86-64 shrink-wrapping.

### Add a per-block raw-word checkpoint

The original chained rotation has a finite bit orbit. Since
`gcd(27, 64) = 1`, a bit returns to its starting position after 64 stripes.
Bit 63 is also invariant under multiplication by any odd constant when viewed
as an XOR differential. Those two facts allowed an exact, seed-independent
576-byte collision family: carry-matched differences followed the rotation
for one complete orbit and reconverged in the same lane.

After every 64-byte bulk block, the last raw word is now added to lane 0.
This exposes the known differential every eight stripes rather than letting it
stay hidden for 64. On AArch64, the compiler folds lane 0's multiply and the
checkpoint into one `madd`; on x86-64 it is one extra add per 64 bytes. The
bulk threshold is fixed at 320 bytes so the four-lane loop alone can never
reach a complete 64-stripe orbit.

This is a targeted structural defense, not a proof that no collision family
exists. In a full-width bounded Z3 model, the discovered ladder was
unsatisfiable for arbitrary incoming lane 0 state over two through nine
blocks, all eight starting-lane offsets, and tested pair-swap compositions.
A broader top-bit-differential model was unsatisfiable through eight blocks;
its nine-block query timed out. A deterministic regression for the discovered
family is kept in `tests/quality.c`.

## Second pass: dispatch and code layout

A follow-up pass took the two output-identical items from the experiment
list below (more length tiers, a two-block unroll) and landed them plus
the restructuring they turned out to require. Every change in this pass
preserves the digest bit for bit: each candidate was gated on an
exhaustive comparison against the previous commit (all lengths 0..8192
over five buffer patterns, several seeds and alignments, plus large spot
sizes, verified on both AArch64 and x86-64) before any benchmark was
read. Deltas below are medians of interleaved A/B runs on the same
Apple M1 generation hardware as the first pass; x86-64 numbers in this
section are from Rosetta 2. The third pass below re-measured them on
native silicon and overturned several, so read them as historical.

### Outline the long-input path into a non-inlined function

Inlining the two-block-unrolled bulk loop made clang allocate
callee-saved registers whose unconditional prologue/epilogue traffic
serialized back-to-back short hashes through the same stack slots:
multiple-of-32 lengths lost up to half their independent-hash
throughput while chained latency looked fine. Moving everything at or
above the 320-byte bulk threshold into a `static` `noinline` function
fixed that and turned out to be a large win on its own: the monolithic
function's short paths shrink-wrap and lay out better on both
architectures. Under Rosetta/x86-64, 17..31-byte keys gained 13..15%
independent-hash throughput from the outlining alone.

The outlined function deliberately mirrors `hayahash64`'s signature and
recomputes the seed/length premix itself. Passing the premixed value
instead (in any argument order) pinned it to an argument register for a
zero-move tail call, and the register allocator then either mangled the
caller's mid/tail address arithmetic or shuffled registers on entry for
every key, both measured at 2..5% on 17..160-byte keys. With the
mirrored signature the bulk branch compiles to a bare tail jump and the
caller's registers stay unconstrained.

The wasm build opts out: it has no callee-saved-register pressure to
begin with, and the second function body means a second copy of the
fold/tail code, growing the embedded module from 1424 to 2004 bytes
for no measured benefit. `hayahash64` keeps the pre-split fall-through
there (inline bulk loop into the shared mid loop and tail), which the
native/wasm KAT harness confirms is bit-exact.

### Unroll the bulk loop two blocks deep

Inside the outlined function, processing two 64-byte blocks per
iteration (same block sequence, checkpoint after each block) gained
4..5% bulk throughput on the M1. On x86-64 the unroll's live state
exceeds the 15 usable registers on paper, and Rosetta measured a 7%
bulk loss, so this pass shipped it AArch64-only. Native Zen 5 later
reversed that call (see the third pass below); the unroll now covers
x86-64 as well. It remains a code shape choice per architecture, not
an algorithm change.

### Simplify the generic tail absorbs

The two tail branches used to consume 16 bytes (pointer and length
updates) before the final overlapping last-16 absorbs. Testing the
untouched post-loop remainder for both branches is equivalent
(`l > 16` implies `l - 16 > 0`), so the updates are gone and the
last-16 loads always address one loop-invariant end-of-input position.
This removed address arithmetic the allocator otherwise rematerialized
badly around the tail call, and gained 1..2.5% on 40..127-byte keys.

### Straight-line 32..63 and 64..127-byte tiers on AArch64

Spelled-out copies of one, two, or three mid rounds plus the generic
tail, nested under the existing 17..31-byte tier. The first absorb of
the first round folds in the known zero chain value, and the tail
conditions test the original length (for the 32..63 tier, `l > 48` and
`l > 32` replace the generic path's post-round `l > 16` and `l > 0`).
Measured +5..11% independent-hash throughput and +1..4% chained latency
across 32..127 bytes. The added dispatch compares cost 128..192-byte
keys about 1..1.7%, which the next item recovers.

### Unroll the AArch64 mid loop to 64-byte rounds

With the tiers returning everything below 128 bytes and the outlined
function taking everything at or above 320, the AArch64 mid loop only
serves 128..319-byte keys. Two rounds per iteration (with at most one
single round left over) halves its loop control: +4..5% for that band,
with the main function still free of callee-saved spills.

### A straight-line 128..191-byte tier on AArch64

A follow-up extended the tier chain by one band: four spelled-out
mid rounds, then the same optional-round-plus-tail shape as the
64..127 tier. Measured +6..7% independent-hash throughput on 128..159
bytes and +10..11% on 160..191 (the sizes that previously ran two
64-byte loop iterations), with chained latency up +0.2..3.2% across
the band. The extra dispatch compare costs 192..288-byte keys at most
0.5%, at the edge of the noise floor. 192..319 stays on the unrolled
mid loop: its loop control is a shrinking fraction of per-hash time,
so another tier would mostly add I-cache footprint.

### Second-pass ideas tested and rejected

- **Bulk dispatch after the tiers:** moving the 320-byte check below
  the tier chain saved one compare per tier but cost 3..6% on
  128..512-byte keys; the compare count for mid keys did not actually
  drop and both the mid and bulk layouts got worse.
- **Enabling the tiers on x86-64:** under Rosetta, 32..127-byte keys
  gained 3..7%, but 17..31-byte and 128..319-byte keys lost 1..3%.
  Mixed results from an emulation proxy did not justify committing.
  Superseded: the third pass measured native silicon and landed the
  tiers for x86-64 GCC while keeping clang excluded.
- **Unrolling the x86-64 mid loop:** the same 64-byte rounds that the
  AArch64 mid loop runs gained 1..5% on 80..319-byte keys under
  Rosetta but cost up to 9% of 17..63-byte independent throughput.
  Superseded: on native silicon the shape wins under GCC when the
  tiers keep short keys out of the loop, and loses badly under any
  measured clang; the third pass gates it accordingly.
- **An inline two-block unroll:** the first attempt kept the unrolled
  loop inline and lost up to half of small-key throughput to
  callee-saved register traffic; see the outlining note above for the
  mechanism. Rejecting the inline shape, not the unroll, was the
  lesson.

## Third pass: native x86-64 (Zen 5)

The Rosetta-proxied x86-64 decisions were re-measured on an AMD Ryzen
AI 9 HX PRO 370 (Zen 5, pinned to a 5.16 GHz classic core with the
performance governor; A/B noise floor within 0.1%). GCC 16.1 with
`-march=native` is the primary toolchain; clang 21 numbers come from
a zig-cc musl cross build run on the same machine. Everything below
kept the exhaustive bit-exactness gate from the second pass.

The headline lesson: Rosetta 2 predicted the wrong sign for two of
three experiments, and GCC and clang want different dispatch shapes
on identical silicon. Per-compiler measurement is not optional here.

### Validate the second pass, fix the lost bulk invariant

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

### The two-block bulk unroll transfers to x86-64

Native Zen 5 does not reproduce Rosetta's 7% penalty: the unroll
gains 4..10% (GCC) and 10..48% (clang) on 320-byte-and-up keys, with
chained latency flat for both. The clang upper end is a second-order
effect worth naming: with half as many loop exits, the out-of-order
window overlaps independent hashes much more deeply, so 320..1024-
byte independent-hash throughput jumps far past the loop's
steady-state rate. Now enabled for x86-64 alongside AArch64.

### Tiers and mid rounds: GCC yes, clang no

The full AArch64 dispatch shape (17..191 tiers plus 64-byte mid
rounds) transfers cleanly to x86-64 GCC: +5..15% independent-hash
throughput on 32..191-byte keys, +1% on 17..31, flat chained latency,
at worst 1% taken from 192..319. Under clang 21 on the same machine
the identical shapes fail in two distinct ways: the tiers alone cost
6% on 17..31-byte keys and 1..4% on 192..319 (against 1..10% gains in
between), and the mid rounds collapse chained latency by 26..48%
across the board while independent-hash throughput doubles at some
sizes. The mechanism is the familiar one from the outlining note:
clang assigns the enlarged monolithic function callee-saved
registers, and their unconditional save/restore traffic lands on the
seed dependency chain of back-to-back hashes. GCC shrink-wraps the
same shapes without touching the chain. A single
`HAYAHASH64_INTERNAL_TIERS` gate now selects the wide dispatch for
AArch64 and x86-64-non-clang, and the compact one for x86-64 clang
(unmeasured targets also stay compact).

### Reference numbers and an open transition band

Against the vendored ChibiHash baselines on the same Zen 5 (GCC 16),
hayahash leads at every size below 320 bytes (128-byte keys: 5.2 vs
6.5 ns independent) and in sustained bulk (35.3 vs 31.2 GB/s at
1 MiB). ChibiHash v2 leads by 2..8% only in the 320..512-byte
transition band, where the eight-lane bulk loop is still amortizing
its lane setup and fold; the band shrank with the invariant fix but
did not close. The bulk threshold itself is an algorithm parameter
(digests change with it), so any further gain there must come from
cheaper entry/exit code, not from moving the boundary.

## Ideas tested and rejected

- **Six or seven bulk lanes:** reduced instruction-level parallelism enough
  to lose to eight lanes on the measured M1 and Rosetta/x86-64 paths.
- **A separate x86 dispatcher:** introduced save/restore overhead and made
  short inputs slower; the monolithic function already shrink-wraps well.
- **A configurable bulk threshold:** changing it can move an unbounded number
  of stripes into the uncheckpointed four-lane loop, so it is an algorithm
  parameter rather than a tuning knob.
- **Linear post-state rotates or XOR-shifts:** these only move a universal
  differential through another invertible linear map. Constructed collisions
  remained possible, while the extra recurrence cost was substantial.
- **XOR-shifting the chained word before absorption:** several variants still
  admitted exact generalized orbit collisions at 320 or 576 bytes.
- **XOR-shifting the previous word before rotation:** candidate transforms had
  short algebraic orders and produced exact 96-byte collisions.
- **One multiply per 16 input bytes:** a fast ARX companion-lane prototype
  gained throughput, but its strict-avalanche score fell to roughly 0.41 and
  it admitted an exact constructed 320-byte collision.
- **Alternating which half receives multiplication:** the separable structure
  exposed an XOR nullspace even when each half looked locally well mixed.
- **A late state-only “wall”:** because it did not directly observe raw input,
  the offending differential could be translated to compensate for it.

## Research context

- [ChibiHash v2](https://github.com/N-R-K/ChibiHash) is the closest
  same-category baseline. It motivated the ordinary-multiply constraint and
  several tail ideas, but the new orbit regression also finds the same
  deterministic collision family in its long recurrence.
- [rapidhash v3](https://github.com/Nicoshev/rapidhash) demonstrates the
  performance and quality available when a folded 128-bit product is allowed.
  That primitive is precisely outside this experiment's portability category,
  so its core mixing step cannot be copied fairly.
- [wyhash](https://github.com/wangyi-fudan/wyhash) is the source of the
  overlapping-tail style, but likewise depends on wide multiplication for its
  central mix.
- [HalftimeHash](https://arxiv.org/abs/2104.08865) shows a different route to
  long-input hashing without widening 64-bit multiplication: encode blocks so
  that input differences affect several independently mixed positions. Its
  tree/SIMD-oriented construction is not a drop-in fit, but its explicit
  distance argument is useful guidance for future bulk designs.
- [SMHasher3](https://gitlab.com/fwojcik/smhasher3) remains the statistical
  gate. Its own documentation cautions that it is a broad black-box test suite,
  not a proof or a total ordering of hash quality; the orbit collision found
  after a clean run is a concrete example of that distinction.

## Promising next experiments

Items one and two of the original list (more output-identical length
tiers, and an output-identical two-block unroll) were landed by the
second pass documented above; the native x86-64 measurement item was
resolved by the third pass, which landed the unroll for all of x86-64
and the tier shape for x86-64 GCC.

1. **Cheaper bulk entry and exit.** The 320..512-byte transition band
   is the one place ChibiHash v2 still leads on Zen 5 (2..8%). The
   eight-lane IV derivation and the 8-to-4 fold are the fixed costs;
   any output-identical scheduling or spelling change that trims them
   pays off exactly where the function is weakest.
2. **Stock-toolchain confirmation on x86-64.** The clang exclusion
   rests on clang 21 via zig/musl (and directionally on Rosetta).
   Confirm with a distribution clang/glibc build, and measure MSVC
   x64, which currently takes the compact dispatch and the one-block
   bulk loop untested.
3. **A 192..255-byte tier.** The 128..191 tier landed (see above);
   extending the chain another band would trade a further dispatch
   compare on 256..319 plus I-cache footprint against loop control
   that is already down to three or four unrolled iterations. Only
   worth measuring with a realistic mixed-size workload, where the
   footprint shows.
4. **A synthesized non-separable 8-to-4 fold.** The four upper-lane multiply
   folds are fixed overhead around 320–1024 bytes. Search small add/XOR/rotate
   butterflies in which every upper lane reaches at least two lower lanes
   before the existing final products. This could remove multiplies, but it is
   high risk: reduced-width exhaustive search and full-width SMT must precede
   performance testing.
5. **A non-separable two-word cell.** Spend two ordinary 64-bit multiplies per
   16 bytes, but make both outputs depend on both words before either lane is
   committed. This costs the same multiply density as the current core while
   attacking the separability that broke the half-multiply prototypes.
6. **Distance-coded block injection.** Borrow HalftimeHash's design principle,
   not its SIMD implementation: derive two or three cheap, differently rotated
   checksums from each 64-byte block and inject them into distinct lanes. Any
   candidate must be audited for GF(2) nullspaces and carry-matched modular
   differentials before benchmarking.
7. **A high-half surrogate.** Test whether one extra 32x32 product per two
   words can cheaply approximate some cross-half information normally supplied
   by a folded 128-bit product. This may be attractive on dual-multiply cores,
   but is likely a loss on small in-order targets.
8. **A dynamic low-product pair cell.** The only plausible large bulk gain is
   still one multiply per 16 bytes, but both words and both states must enter
   one dynamic product and survive by feed-forward, rather than forming the
   separable multiplied/ARX halves already rejected. This could halve multiply
   density, but low multiplication remains triangular and an odd operand keeps
   the exact bit-63 differential. Reject candidates with reduced-width state
   resets before spending time on benchmarks.
9. **Hayahash-specific circuit synthesis.** Extend unary-mixer searches such as
   [hash-prospector](https://github.com/skeeto/hash-prospector) to score small
   multiword circuits on real AArch64/x86 instruction counts, SAC/BIC, the
   known orbit corpus, seed-erasure families, and reduced-width exact
   collisions. Avalanche alone is not an adequate objective.
10. **Continuous algebraic regression.** Add bounded solver and constructed
   differential searches beside SMHasher3. Statistical tests should remain the
   release gate, while structural searches specifically target bit 63, rotation
   periods, lane reunions, modular carry control, and fold cancellation.
