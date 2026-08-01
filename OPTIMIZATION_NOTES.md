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

### Drop the product guard for GCC

The empty-asm guard on computed products exists to stop clang from
distributing a following rotate into two independent multiplies. GCC
never applies that transform, so for it the barrier only pinned
values and serialized scheduling around the folds and the finalizer,
squarely on the chained-hash critical path. Making the guard
clang-only gains 0.6..4.1% chained latency across all measured
17..319-byte keys and 1..3% independent-hash throughput on most of
them, with bulk rates and every clang target (wasm module included)
unchanged.

### Reference numbers and an open transition band

Against the vendored ChibiHash baselines on the same Zen 5 (GCC 16),
hayahash leads at every size below 320 bytes (128-byte keys: 5.2 vs
6.5 ns independent) and in sustained bulk (35.4 vs 31.5 GB/s at
1 MiB). ChibiHash v2 leads by 2..8% only in the 320..512-byte
transition band, where the eight-lane bulk loop is still amortizing
its lane setup and fold; the band shrank with the invariant fix but
did not close. The bulk threshold itself is an algorithm parameter
(digests change with it), so any further gain there must come from
cheaper entry/exit code, not from moving the boundary.

## Fourth pass: transition band, stock toolchains, algorithm screens

This pass worked through items 1..8 of the experiment list below on
the same pinned Zen 5 box and the Apple M1, with two toolchain
additions: distribution clang 22.1.8 with glibc beside GCC 16.1 on
the Zen 5, and an MSVC x64 CI job. Two pieces of methodology carried
the pass: a mixed-size A/B mode (deterministic pseudo-random
offset/length sequences, so per-hash size branches stay
unpredictable while base and candidate hash identical work), with
mixed and small-delta comparisons repeated under swapped candidate
link order to cancel code-placement bias (measured at +-1.7% for
clang, well above some of the effects under test); and a
reduced-width structural screen for the digest-changing candidates.

### Cheaper bulk exit: hoisted bookkeeping and a pinned K

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

### Stock clang 22: exclusions re-validated, the reason moved

The clang tier/mid-round exclusion from the third pass rested on
clang 21 via zig/musl. Re-measured with distribution clang
22.1.8/glibc on the same silicon (digest-exactness checked, every
delta link-order-swap controlled), the exclusion survives but its
mechanism does not: forcing the wide dispatch now wins 4..16%
independent throughput at fixed 32..319-byte sizes with chained
latency flat, i.e. the 26..48% callee-saved chained collapse is
gone from the current long path. What remains is a front-end cost
that only mixed-size workloads expose: with the whole tier chain
live and per-hash branch targets unpredictable, 64..512-byte mixes
lose 15% independent and 12% chained, 128..319 mixes lose 8% and
7%, against +6..9% gains on single-band (192..255) and small-heavy
(1..319) mixes. A general-purpose default cannot assume single-band
input sizes, so x86-64 clang keeps the compact dispatch, now for
front-end rather than register reasons (the header comment was
updated to match).

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

### A 192..255-byte tier, decided by mixed workloads

The tier chain grew one band: six spelled-out mid rounds plus the
same optional-round-and-tail shape as the earlier tiers, leaving
the mid loop to serve only 256..319-byte keys. Output-identical,
and gated by the tier macro, so x86-64 clang and wasm are
untouched. The notes' concern that the tier's I-cache footprint
would only show in mixed workloads is what the mixed A/B mode was
built for, and it answered in the tier's favor: Zen 5 GCC gains
+7.2% independent throughput at 192 bytes and +5.8..6.9% at
256..319 (the residual mid loop compiles better with its narrower
range) at fixed sizes, and mixed workloads containing the band gain
4.1..9.5% independent and 1.4..3.5% chained, symmetric under link
swap, with 1..64-byte mixes flat. M1: +8.1% at 192 bytes fixed;
mixed 192..255 +8.3% independent and +1.8% chained.

### Reduced-width screens for the algorithm candidates

Items 4..8 propose digest-changing bulk-cell and fold redesigns.
Before any full-width work, a reduced-width harness reimplements
the long-path skeleton (eight-lane bulk over 8-word blocks,
per-block checkpoint, 8-to-4 fold, tail, finalizer) at W = 8 and
16 bits with pluggable cells and folds, and runs three screens: an
exhaustive-differential erasure scan through one cell application,
the rotation-orbit ladder family generalized to width W
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

## Fifth pass: mixed-size dispatch re-audit, GCC bulk vectorization

This pass started from a doubt about the straight-line tiers ("is the
weird branching really faster than the regular way?") and re-measured
the dispatch question on both reference machines with the fourth
pass's mixed-size A/B methodology, then landed the largest single
throughput win so far by teaching GCC to auto-vectorize the bulk
loop. Toolchains: Apple clang 21 on the M1 (unpinned; see the
thermal note below), GCC 16.1 and stock clang 22.1.8 on the Zen 5
box pinned to a 5.16 GHz classic core. Every change kept the
exhaustive bit-exactness gate, run per experiment on both machines.

### The tiers are a GCC shape; clang now gets compact everywhere

The fourth pass measured broad mixed-size workloads only on Zen 5
clang. Running them on the M1 shows Apple clang 21 has the same
front-end problem: with the whole tier chain live and per-hash branch
targets unpredictable, the compact dispatch beats the tiers by 2..8%
independent and up to 10% chained on broad mixes (1..319, 17..319,
128..319, 1..512, 64..512), is flat on 1..64 mixes and at fixed
17..31, and gives back 6..11% only at fixed 32..192-byte sizes plus
about 9% on a single-band 192..255 mix. On Zen 5 GCC the same
comparison goes the other way at every point: compact costs 3..13%
at fixed sizes and 2..10% on every mixed band, chained included.
GCC shrink-wraps the chain and predicts it well; both measured
clangs pay for it in exactly the workloads a general-purpose
default has to assume. The tier gate is now
`(aarch64 || x86_64) && !clang`: GCC keeps the tiers (aarch64-GCC
behavior is unchanged from before), all clang targets take the
compact dispatch, and the M1 fixed-size numbers in the README gave
up a measured 5..9% at 32..128 bytes in exchange (hayahash still
leads ChibiHash v2 at every size there, and the seed-chained
latencies SMHasher3 reports for 1..31-byte keys are unchanged).

Two dispatch alternatives were built to try to keep both properties
and both failed everywhere. A switch over `l >> 5` (one jump table,
same tier bodies) loses 16..35% on mixed sizes under GCC, which
also bloats the bodies with duplicated tails; Apple clang refuses
to emit a table for it and produces a wash. A computed-goto table
(GNU labels-as-values, guaranteed single indirect branch) loses
4..15% on M1 mixes and 11..30% under Zen 5 clang: one
always-mispredicting indirect branch is worse than a short compare
chain whose early branches are heavily biased. Dispatch by compare
chain survives its re-audit; only who compiles it changed.

### Thermal discipline for M1 mixed numbers

The fanless M1 Air's mixed-size results move by tens of percent on
specific bands (1..64 swung from +13% to -16% between repeats of
byte-identical binaries) once the machine has been benching for
half an hour. Decisions in this pass used three cool-machine
replicates after a four-minute idle window, which brought 1..64 to
a stable +-2%. Fixed-size rows were far less sensitive. Treat any
future M1 mixed delta measured on a warm machine as noise until it
reproduces cold.

### Guards re-audited: obsolete purpose, new job, unchanged verdict

Neither Apple clang 21 nor stock clang 22 still distributes
`rotl(x * K, n)` into two multiplies, the transform the empty-asm
product guards were introduced against. Removing them is
nevertheless a loss on both clang platforms, for two different
reasons. On the M1 the guard-free build is smaller (24 fewer
multiplies, 16% less code) and slower: 2..15% independent
throughput at 17..319 bytes and 2..4% of 320..1024-byte throughput,
with only 320..512-byte chained latency improving. On Zen 5, clang
22 without the guards auto-vectorizes the mid loop and tails, which
raises fixed-size independent throughput 15..51% and mixed
independent throughput 15..45% while collapsing seed-chained
latency 20..42% at every size (17-byte chained: 4.3 to 7.4 ns); the
barrier turns out to be what keeps the latency-critical path scalar.
Both effects are documented in the header now; the guards stay
exactly as they were (clang-only), doing a different job than the
one they were hired for.

### Store-seeded SLP: GCC reaches clang's AVX-512 bulk rate

clang 22 auto-vectorizes the plain bulk block into a 4+4 split:
lanes h2..h5 become one vector whose rotate source and absorb words
are two overlapping vector loads from the input (vprolq and vpaddq
with memory operands, no shuffles), while h0/h1/h6/h7 stay scalar
because they carry the wp chain and the checkpoint. GCC 16 never
finds this by itself, and the reason is structural: its SLP
vectorizer only seeds from groups of adjacent stores, and
register-resident lanes make no stores. Two bit-identical
respellings proved the mechanism. Straight-line re-loaded lanes
(the exact dataflow clang vectorizes) compile to byte-identical
scalar code under GCC: CSE folds the duplicate loads back and no
seed ever forms. The same four lanes as a tiny local array updated
through a countable 4-iteration loop do vectorize ("loop versioned
for vectorization" plus 32-byte vectors): the array defeats SRA, the
stores are adjacent, and the loop vectorizer takes it from there. A
third spelling with the array but straight-line stores fails again
because SRA scalarizes the array away before SLP runs. The loop
form is gated to x86-64 GCC with AVX-512DQ (vpmullq needs it;
without the macro the header preprocesses token-identically to the
old spelling, so default x86-64 builds and every other target are
untouched, which was verified by diffing the emitted asm for M1
clang, Zen clang, and non-AVX-512 GCC).

Measured on the Zen 5 with GCC 16 at `-march=native`, digests
identical, link-order swapped: sustained bulk goes from 35.3 to
61.8 GB/s (+75%, now equal to clang 22's auto-vectorized rate on
the same machine), 4 KiB +77%, and the 320..512-byte transition
band, the one place ChibiHash v2 still led after the fourth pass,
improves 30..46% in throughput and 10..29% in latency (320 bytes:
25.8 to 33.8 GB/s against v2's 26.4; 512 bytes: 28.9 to 42.5
against 28.8). Chained latency at 1 MiB improves 54%. Every row
below 320 bytes moves 0.0%: the sub-bulk paths compile to the same
instructions. The transition band is no longer a weakness at all,
and the last entry on the fourth pass's competitive scorecard
flips: hayahash now leads both ChibiHash versions at every measured
size on Zen 5 GCC.

The aliasing loop-versioning GCC adds (it cannot prove the local
array does not alias the input) costs nothing measurable; the
check hoists. The residual risk is old AVX-512 servers with slow
512-bit datapaths (Skylake-X class), where GCC's own cost model is
trusted to decline vectorization; nothing forces it.

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

The fourth pass resolved items 1..8 below. Items 1 and 3 landed
(exit bookkeeping, K pinning, the 192..255 tier); item 2
re-validated both clang exclusions on a stock toolchain and put
MSVC x64 conformance in CI, with MSVC timing still open; candidates
4..8 all ended as documented rejections at their screening or
benchmark gates. Items 9..10 remain deferred as research
infrastructure.

The fifth pass closed what remained of item 1: the store-seeded SLP
spelling ends the 320..512-byte deficit outright (hayahash now leads
ChibiHash v2 there by 22..48% under Zen 5 GCC) and doubles GCC
sustained bulk. It also re-decided the dispatch shape per compiler
(tiers GCC-only, compact for all clang targets) after broad
mixed-size measurement on the M1, and re-audited the compiler
guards to their new anti-vectorization role. Still open: MSVC x64
timing, an SMHasher3 shootout rerun to refresh the Zen 5 bulk
column, and whether GCC on AArch64 servers (Graviton class) wants
the vectorized spelling via SVE once such a machine is available.

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
