# Implementation notes

How the reference header compiles, and why. Everything here is
output-preserving: the digest is defined by [`design.md`](design.md), and
every shape below is required to produce it bit for bit. The header carries
the same rationale next to the code it applies to.

## The one non-negotiable

Dispatch shape is a per-target choice. Algorithm parameters are not. The
320-byte bulk threshold, the absorb rotation, and the 32- and 64-byte block
sizes change the digest and are bounded by the structural arguments in
[`design.md`](design.md#cancellation-channels). A change to any of them is a
digest break, not a tuning experiment.

## Compiled shapes

Five preprocessor gates select between spellings of the same dataflow.

<!-- markdownlint-disable MD013 -->

| gate | enabled when | effect |
|---|---|---|
| `HAYAHASH64_INTERNAL_TIERS` | AArch64 or x86-64, **and not clang** | Straight-line length tiers for 17..319-byte keys instead of a compact loop. Both widths use the same gate. |
| `HAYAHASH64_INTERNAL_VECGCC` | x86-64 GCC with AVX-512DQ **and** a Zen 4/5 target | Spells the middle four bulk lanes as a countable 4-iteration array loop, which is the only shape GCC's SLP vectorizer will seed from. Token-identical to the plain spelling everywhere else. |
| `HAYAHASH64_INTERNAL_BULK_LANE_GUARD` | clang, x86-64, AVX-512DQ, **not** Zen 4/5 | Pins the middle bulk lanes to scalar registers at block boundaries, denying the vectorizer its seed. Costs no instructions. |
| `HAYAHASH64_INTERNAL_COMPILER_GUARD` | clang | An empty `asm` barrier on already-computed products. Keeps the latency-critical path scalar and short. GCC has never needed it. |
| `HAYAHASH128_INTERNAL_INLINEBULK` | clang, not wasm | Keeps the 128-bit eight-lane loop inline. Every other target calls the outlined 128-bit long path, which is where GCC's bulk vectorization fires. |

<!-- markdownlint-enable MD013 -->

Two more shapes are unconditional within their target sets: the 64-bit long
path is always outlined (except on wasm, which falls through inline to avoid
a second copy of the fold and tail code), and the bulk loop is unrolled two
blocks deep on AArch64 and x86-64.

### Why they split the way they do

**Tiers are a GCC shape.** GCC shrink-wraps the tier chain and wins with it
on every measured workload. Both measured clangs lose with the chain live
once per-hash branch targets are unpredictable, so clang gets the compact
dispatch. Jump-table dispatch over the same tier bodies — both `switch` and
computed `goto` — was measured and rejected on every compiler/architecture
pair: one indirect branch predicts worse than a short compare chain on
mixed sizes.

**Vectorization is a Zen 4/5 property, not an AVX-512 property.** The
vectorized bulk loop emits 32-byte `vpmullq`, which Zen 4/5 runs as one uop
and Skylake-X-class servers microcode into three with high latency, landed
directly on each lane's carried chain. Gated on AVX-512DQ alone it is a large
loss on Cascade Lake, so both the GCC opt-in and the clang opt-out check for
a Zen 4/5 target.

**The 128-bit bulk dispatch is inverted from the 64-bit one.** GCC only
reaches the vectorized spelling through the outlined path; inlined, GCC's SRA
pass scalarizes the lane array and the vectorizer's store seed disappears.
Both measured clangs instead destabilize their 17..319-byte schedules when
the 128-bit long path is outlined, and reach the same bulk rate with the loop
inline plus the two-block unroll.

**Outlining the 64-bit long path is about register pressure.** Its unrolled
loop wants more registers than the short paths can afford; inlined, the
spills become unconditional prologue stores that serialize back-to-back short
hashes through the same stack slots.

### Covering every shape

A conformance run must exercise every compiled shape, not just the one the
build host picks. [`smhasher3.md`](smhasher3.md#covering-every-dispatch-shape)
lists the build matrix that does it. Any two shapes must produce identical
non-timing output; that equality is the gate, and speed differences between
them are not.

## Algorithm ideas tested and rejected

These are digest-changing candidates that were screened and turned down.
Each is recorded because the reason survives the experiment.

**Rejected on structure** — each reopened a cancellation channel that
[`design.md`](design.md#cancellation-channels) closes:

- **XOR of staggered stripe copies.** GF(2)-linear in the stripe sequence,
  so it has a nullspace once absorbs chain: difference patterns exist that
  vanish in every absorbed value. This is what the chained add-and-rotate
  absorb replaced.
- **XOR-shifting the chained word before absorption**, and **XOR-shifting the
  previous word before rotation.** Both keep short algebraic orders; variants
  admitted exact generalized orbit collisions at 96, 320, and 576 bytes.
- **Linear post-state rotates or XOR-shifts.** These move a universal
  differential through another invertible linear map and change nothing about
  whether it exists, at substantial recurrence cost.
- **Alternating which half receives the multiplication.** The separable
  structure exposes an XOR nullspace even when each half looks locally well
  mixed.
- **A late state-only wall.** Because it never observes raw input, the
  offending differential can be translated to compensate for it. The wall
  absorbs a raw rotated stripe for this reason.
- **A two-word cross-injected cell.** Still too separable: top-bit-only
  stripe patterns collide well above binomial expectation in a reduced-width
  screen, and the ladder family leaks. It costs the same two multiplies per
  16 bytes as the current cell, so there was no speed upside to trade.
- **A dynamic low-product pair cell.** The generalized ladder leaks at a
  stable per-trial rate where the current cell blocks the same family
  exactly, so the defense degrades from structural to carry-luck.
- **A multiply-free 8-to-4 fold butterfly.** Top-bit collisions run far above
  expectation, which is direct evidence that the fold multiplies suppress
  exactly the family the design defends against.
- **One multiply per 16 input bytes.** An ARX companion-lane prototype gained
  throughput but its strict-avalanche score fell to roughly 0.41 and it
  admitted an exact constructed 320-byte collision.

**Rejected on cost**, with no quality gap to justify them:

- **Distance-coded block injection.** The existing checkpoint costs one add
  per block and blocks the orbit family exactly; rotated checksums cost 7..11
  ops per block and would still need a GF(2) nullspace audit.
- **A high-half surrogate.** An extra 32x32 product with combines per word
  pair adds roughly a third to the bulk op count to buy margin that no screen
  or collision family shows lacking.
- **A halved 8-to-4 fold** (two multiplies, every upper lane still reaching
  two lower lanes). It passes every quality screen but adds a level to the
  fold's dependency chain, and the fold runs once per hash, so two saved
  multiplies never pay it back.
- **Six or seven bulk lanes.** Reduced instruction-level parallelism enough to
  lose to eight.
- **A separate x86 dispatcher.** Save/restore overhead made short inputs
  slower; the monolithic function already shrink-wraps well.
- **A configurable bulk threshold.** Changing it can move an unbounded number
  of stripes into the uncheckpointed four-lane loop, which is why it is an
  algorithm parameter.

## Screening method

Digest-changing candidates are screened at reduced width before any
full-width work. A harness reimplements the long-path skeleton — eight-lane
bulk over 8-word blocks, per-block checkpoint, 8-to-4 fold, tail, finalizer —
at 8 and 16 bits with pluggable cells and folds, and runs three gates:

1. an exhaustive-differential erasure scan through one cell application;
2. the rotation-orbit ladder family generalized to width *W*, following
   carry-matched per-stripe differences for a full orbit; and
3. end-to-end collision counting against binomial expectation over sparse,
   repeat-block, and top-bit-stripe key families.

The harness validates by construction: removing the checkpoint from the
current cell collides on every carry-matched ladder trial at both widths, and
restoring it suppresses the family exactly. A candidate that survives all
three still has to win at full width on both quality and speed.

## Open questions

- MSVC x64 performance. Conformance is in CI, but shared-runner timing is too
  noisy to gate on, so the compact dispatch and one-block bulk loop it
  compiles remain performance-untested.
- Whether GCC on AArch64 servers wants a vectorized bulk spelling via SVE,
  once such a machine is available.
- A stock-clang chained-latency regression on Zen 5 under the outlined
  128-bit shape. Routed around by the inline gate above; cause never
  identified.
- **Hash-specific circuit synthesis.** Extend unary-mixer searches such as
  [hash-prospector](https://github.com/skeeto/hash-prospector) to score small
  multiword circuits on real instruction counts, SAC and BIC, the orbit
  corpus, seed-erasure families, and reduced-width exact collisions.
  Avalanche alone is not an adequate objective.
- **Continuous algebraic regression.** Add bounded solver and constructed
  differential searches beside SMHasher3, targeting bit 63, rotation periods,
  lane reunions, modular carry control, and fold cancellation. Statistical
  tests stay the release gate; structural searches are what find the
  channels.

## Related designs

- [ChibiHash v2](https://github.com/N-R-K/ChibiHash) — the closest
  same-category baseline: portable scalar, ordinary multiply, passes
  SMHasher3. It motivated the ordinary-multiply constraint and several tail
  ideas. The orbit regression finds a deterministic collision family in its
  long recurrence too.
- [rapidhash v3](https://github.com/Nicoshev/rapidhash) — what a folded
  128-bit product buys. That primitive is outside this design's class, so its
  central mixing step cannot be borrowed fairly.
- [wyhash](https://github.com/wangyi-fudan/wyhash) — source of the
  overlapping-tail read pattern; its central mix also needs wide
  multiplication.
- [HalftimeHash](https://arxiv.org/abs/2104.08865) — a different route to
  long-input hashing without widening multiplication: encode blocks so an
  input difference affects several independently mixed positions. Its
  tree/SIMD construction is not a drop-in fit, but its explicit distance
  argument is useful guidance.
- [SMHasher3](https://gitlab.com/fwojcik/smhasher3) — the statistical gate.
  Its own documentation cautions that it is a broad black-box suite, not a
  proof or a total ordering; a constructed collision found after a clean run
  is a concrete example of that distinction.
