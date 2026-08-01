# Optimization log

This log records the optimization passes performed against the
development version of `hayahash64`. The scope is intentionally
narrow: a 64-bit result, portable scalar operations, ordinary
64x64-to-64-bit multiplication, no SIMD, and no dependency on the high
half of a 128-bit product.

The main lesson was that instruction-count improvements and structural
collision analysis have to proceed together. Several attractive, faster
cores passed ordinary avalanche tests but admitted algebraically
constructed collisions.

Each pass is a historical record, kept in the language and numbers of
its time; current status lives on this page. The header comments in
[`hayahash.h`](../../hayahash.h) are the authoritative description of
what is enabled today.

## The passes

1. [First pass - core changes retained](pass-1-core.md): compiler
   barriers around products, the XOR absorb form, the one-multiply
   long-path finalizer, an AArch64 17..31-byte tier, and the
   per-block raw-word checkpoint that closes a constructed 576-byte
   collision family. The algorithm ideas rejected in this phase are
   in [rejected-ideas.md](rejected-ideas.md).
2. [Second pass - dispatch and code layout](pass-2-dispatch.md):
   output-identical restructuring. Outlining the bulk path, a
   two-block bulk unroll, straight-line length tiers and a mid-loop
   unroll on AArch64. x86-64 decisions here were proxied through
   Rosetta 2 and several were later overturned on native silicon.
3. [Third pass - native x86-64 (Zen 5)](pass-3-zen5.md): re-measuring
   the Rosetta calls on a pinned Zen 5 core. The unroll transfers,
   the tiers become per-compiler (GCC yes, clang no), a lost
   `len >= 320` invariant is restated past the noinline boundary, and
   the product guard becomes clang-only.
4. [Fourth pass - transition band, stock toolchains, algorithm
   screens](pass-4-band-screens.md): cheaper bulk exit, the clang
   exclusions re-validated on stock clang 22 (for a different
   mechanism than first thought), a 192..255-byte tier decided by
   mixed-size workloads, MSVC x64 conformance in CI, and
   reduced-width screens that rejected five proposed bulk-cell and
   fold redesigns.
5. [Fifth pass - dispatch re-audit and GCC bulk
   vectorization](pass-5-vectorization.md): mixed-size measurement on
   both machines makes the tiers GCC-only everywhere, the product
   guards are re-audited into an anti-vectorization role, and a
   store-seeded spelling teaches GCC's SLP vectorizer the bulk loop -
   sustained Zen 5 bulk goes from 35 to 62 GB/s and the 320..512-byte
   transition band stops being a weakness.

## Where things stand

Dispatch state after the fifth pass, as compiled from the header (see
[`docs/smhasher3.md`](../smhasher3.md#covering-every-dispatch-shape)
for how to cover every shape in a conformance run):

- Straight-line length tiers (`HAYAHASH64_INTERNAL_TIERS`): AArch64 or
  x86-64, **and not clang**. Both measured clangs prefer the compact
  dispatch on mixed-size workloads.
- GCC bulk vectorization spelling (`HAYAHASH64_INTERNAL_VECGCC`):
  x86-64 GCC with AVX-512DQ only; token-identical to the plain
  spelling everywhere else.
- Compiler product guards: clang-only. GCC never needed them; on clang
  they now exist to keep the latency-critical path scalar, not to
  prevent the (gone) rotate-distribution transform.
- The two-block bulk unroll covers AArch64 and x86-64.

Resolved since the fifth pass was written: the SMHasher3 shootout was
rerun in full at `v0.4.0` (every cell re-measured, five replicates per
hash), refreshing the Zen 5 bulk column the fifth pass had left stale.

Still open:

- MSVC x64 performance measurement. Conformance is in CI, but
  shared-runner timing is too noisy to gate on; the compact dispatch
  and one-block bulk loop it compiles remain performance-untested.
- Whether GCC on AArch64 servers (Graviton class) wants the vectorized
  bulk spelling via SVE, once such a machine is available.
- Research items 9 and 10 below.

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

## The experiment list

The standing list the passes worked from. Items 1..8 are resolved;
their proposals and outcomes are documented in the passes named below.
Items 9 and 10 remain open as research infrastructure; the
reduced-width harness built in the fourth pass is their seed.

1. **Cheaper bulk entry and exit** - resolved. Exit bookkeeping and a
   pinned K landed in the [fourth pass](pass-4-band-screens.md); the
   [fifth pass](pass-5-vectorization.md)'s vectorized bulk spelling
   ended the 320..512-byte deficit outright.
2. **Stock-toolchain confirmation on x86-64** - resolved in the
   [fourth pass](pass-4-band-screens.md): both clang exclusions
   re-validated on distribution clang 22/glibc, MSVC x64 conformance
   added to CI (timing still open, see above).
3. **A 192..255-byte tier** - landed in the
   [fourth pass](pass-4-band-screens.md), decided by mixed-size
   workloads.
4. **A synthesized non-separable 8-to-4 fold** - rejected at the
   [fourth pass](pass-4-band-screens.md)'s reduced-width screen and
   benchmark gates.
5. **A non-separable two-word cell** - rejected at the screen: still
   too separable, and no multiply-density win to trade.
6. **Distance-coded block injection** - rejected by cost analysis; the
   one-add checkpoint already blocks the orbit family exactly.
7. **A high-half surrogate** - rejected by cost analysis; no screen
   shows the quality margin it would buy as lacking.
8. **A dynamic low-product pair cell** - rejected on quality (the
   generalized ladder leaks) and on op-count grounds.
9. **Hayahash-specific circuit synthesis.** Extend unary-mixer searches such as
   [hash-prospector](https://github.com/skeeto/hash-prospector) to score small
   multiword circuits on real AArch64/x86 instruction counts, SAC/BIC, the
   known orbit corpus, seed-erasure families, and reduced-width exact
   collisions. Avalanche alone is not an adequate objective.
10. **Continuous algebraic regression.** Add bounded solver and constructed
    differential searches beside SMHasher3. Statistical tests should remain the
    release gate, while structural searches specifically target bit 63, rotation
    periods, lane reunions, modular carry control, and fold cancellation.
