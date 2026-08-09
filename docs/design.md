# Design

Four ideas carry the design. Each is explained in detail at the top of
[`hayahash.h`](../hayahash.h), which remains the authoritative
commentary; this page is the overview.

1. **A short bulk dependency chain.** The bulk loop runs 8 independent
   lanes over 64-byte blocks with nothing longer than `add -> mul`
   (~4 cycles) on the loop-carried path, and no cross-lane ALU work
   there. For scale, ChibiHash v2 carries a ~5-cycle
   `add -> mul -> xor` chain per 8-byte stripe across 4 lanes.
2. **A chained, injective absorb.** Each lane absorbs
   `t = w + rotl(w_prev, 27)`, where `w_prev` is the previous stripe.
   At the first stripe where two inputs differ, `w_prev` is still
   equal, so `t` differs: the absorb sequence is injective by
   induction. The rotated copy also plants every stripe bit at a low
   position in the next lane, where `+` and `rotl` commute with
   neither GF(2) nor mod-2^64 algebra. The rotation amount, the fold
   rotations, and a final "wall" absorb of the last stripe's dangling
   copy are all chosen against difference-ladder attacks; see the
   header notes. The rotation applies to an already-loaded register,
   off the loop-carried path, and is cheaper than a second load on
   wide cores.
3. **Derived lane constants; length in the finalizer.** The seed is
   premixed into one value `s`, and all lane IVs are derived from `s`
   plus shifted copies of the single multiplier constant. No big
   per-lane literals are materialized (on AArch64 a 64-bit literal
   costs 4 instructions), and full-state seeding comes for free. The
   length deliberately does not enter `s`: it is absorbed inside the
   finalizer's `t0` multiply (the short path's bijective fmix input),
   so the digest is a pure function of (seed, bytes-so-far) and the
   streaming `hayahash64_state` produces one-shot-identical digests
   without knowing the total length up front. `len -> len*K` is
   injective, which keeps the overlapping tail reads collision-free
   across lengths; absorbing it after the final multiplies instead
   leaves mod-2^64 low-bit structure across lengths of equal-state
   keys (SMHasher3's SeedZeroes differentials catch exactly that).
4. **Overlapping tail reads, two-multiply short path.** Tails read
   whole (overlapping) words from the end of the input, wyhash-style,
   so no byte-at-a-time loop exists for any length. Inputs of at most
   16 bytes take a dedicated path: both loaded words are spread with
   bijective 3-rotation injections (a different one per word, so the
   two multiply terms cannot be erased simultaneously) and passed
   through independent multiplies into a strong finalizer.

## The second output word

hayahash128 reuses the same state walk and preserves hayahash64 as its
low word. On the short path, if
`u = rotl(x, 27) ^ y ^ lenmix`, the pair `(u, x)` is a bijection of
the 128-bit pre-image `(x, y)` for a fixed length. Applying bijective
finalizers to `u` and `x + rotl(u, 32)` therefore preserves that
property. On longer inputs, the high word combines the existing fold
words with addition where the low word uses xor, then uses a distinct
bijective finalizer. Both one-shot outputs and both streaming outputs
come from the same absorb state.

## The collision classes that shaped it

Getting the details right was the hard part: SMHasher3 found five
distinct structural collision classes in earlier iterations of this
design:

- a GF(2) nullspace in a staggered-load absorb
- seed-copy erasure by aligned key bits - twice
- top-window carry-luck ladders, and
- a fold rotation resonating with the absorb rotation

Each fix is documented in the header where it lives. A deterministic
regression for the rotation-orbit family is kept in
[`tests/quality.c`](../tests/quality.c), and the
[optimization log](optimization/) records the ideas that were rejected
because they reopened one of these classes.

## Fixed algorithm parameters

The 320-byte bulk threshold is an algorithm parameter, not a tuning
knob: digests change with it, and keeping the four-lane mid path below
320 bytes bounds it to fewer than the absorb rotation's 64-stripe
orbit. Dispatch shape (straight-line length tiers, bulk-loop
unrolling, the vectorizable bulk spelling) is chosen per architecture
and compiler, but every shape is specified to produce identical
output; [`smhasher3.md`](smhasher3.md#covering-every-dispatch-shape)
lists the shapes and how to cover them all in a conformance run.

## Digest stability

hayahash remains experimental. Its algorithm, constants, and digest values
may change, so it should not yet be used where persisted hashes must remain
stable across versions. The checklist for freezing digests at 1.0 is in
[`stability.md`](stability.md).

The `v0.5` development digest exercised that freedom: the length term moved
from the lane-IV premix to the finalizer, changing every digest. The previous
spelling made a one-shot-identical streaming API impossible because every
lane IV depended on the total length before the first byte was read. Moving
the term costs no additional multiplies. A simpler spelling that xors
`len * K` after the final multiplies fails SMHasher3's SeedZeroes
differentials, so the length term is absorbed inside the `t0` multiply. The C
reference, all language ports, the SMHasher3 mirror, and the website simulator
were updated together when the digest changed.
