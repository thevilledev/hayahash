# First pass: core changes retained

Part of the [optimization log](README.md). This is the historical
record of the first pass, run against the development version of
`hayahash64` on an Apple M1; the algorithm ideas rejected in the same
phase are in [rejected-ideas.md](rejected-ideas.md).

## Preserve one multiply across rotate

Clang can turn `rotl(x * C, n)` into two independent multiplies, shortening
the dependency chain at the cost of an extra multiply and another materialized
constant. An empty compiler barrier, following the technique used by
[xxHash](https://github.com/Cyan4973/xxHash/blob/dev/xxhash.h), keeps the
already-computed product opaque. The same barrier helps schedule the two XOR
halves of the tail injections and keeps the common multiplier in a register on
AArch64. These barriers do not change the digest.

(The [third pass](pass-3-zen5.md) later made the barrier clang-only,
and the [fifth pass](pass-5-vectorization.md) found the transform it
guards against is gone from current clangs while the barrier still
pays for itself another way.)

## XOR the absorb into lane state

The long-path recurrence is now:

```text
t = w + rotl(previous_word, 27)
h = (h XOR t) * K
```

LLVM could reassociate the former outer addition in ways that lengthened the
critical path. The XOR form emitted a shorter recurrence on the measured
AArch64 target and retained the deliberately non-linear addition inside the
chained word transform.

## Use a one-multiply long-path avalanche

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

## Specialize 17–31 bytes on AArch64

A straight-line spelling of the existing four overlapping loads is profitable
on AArch64. It is nested below the existing long-input branch, so it adds no
test to larger keys, is bit-identical to the generic tail, and avoids harming
x86-64 shrink-wrapping.

## Add a per-block raw-word checkpoint

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
