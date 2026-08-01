# Algorithm ideas tested and rejected

Part of the [optimization log](README.md). These are the
algorithm-level candidates rejected while the core design settled,
mostly during the [first pass](pass-1-core.md) era; later passes carry
their own inline rejection records
([second](pass-2-dispatch.md#second-pass-ideas-tested-and-rejected),
[fourth](pass-4-band-screens.md#reduced-width-screens-for-the-algorithm-candidates),
[fifth](pass-5-vectorization.md#the-tiers-are-a-gcc-shape-clang-now-gets-compact-everywhere)).

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
- **A late state-only "wall":** because it did not directly observe raw input,
  the offending differential could be translated to compensate for it.
