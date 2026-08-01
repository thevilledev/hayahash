# Second pass: dispatch and code layout

Part of the [optimization log](README.md). This is the historical
record of the second pass.

A follow-up pass took the two output-identical items from the standing
[experiment list](README.md#the-experiment-list) (more length tiers, a
two-block unroll) and landed them plus the restructuring they turned
out to require. Every change in this pass preserves the digest bit for
bit: each candidate was gated on an exhaustive comparison against the
previous commit (all lengths 0..8192 over five buffer patterns,
several seeds and alignments, plus large spot sizes, verified on both
AArch64 and x86-64) before any benchmark was read. Deltas below are
medians of interleaved A/B runs on the same Apple M1 generation
hardware as the [first pass](pass-1-core.md); x86-64 numbers in this
section are from Rosetta 2. The [third pass](pass-3-zen5.md)
re-measured them on native silicon and overturned several, so read
them as historical.

## Outline the long-input path into a non-inlined function

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

## Unroll the bulk loop two blocks deep

Inside the outlined function, processing two 64-byte blocks per
iteration (same block sequence, checkpoint after each block) gained
4..5% bulk throughput on the M1. On x86-64 the unroll's live state
exceeds the 15 usable registers on paper, and Rosetta measured a 7%
bulk loss, so this pass shipped it AArch64-only. Native Zen 5 later
reversed that call (see the [third pass](pass-3-zen5.md)); the unroll
now covers x86-64 as well. It remains a code shape choice per
architecture, not an algorithm change.

## Simplify the generic tail absorbs

The two tail branches used to consume 16 bytes (pointer and length
updates) before the final overlapping last-16 absorbs. Testing the
untouched post-loop remainder for both branches is equivalent
(`l > 16` implies `l - 16 > 0`), so the updates are gone and the
last-16 loads always address one loop-invariant end-of-input position.
This removed address arithmetic the allocator otherwise rematerialized
badly around the tail call, and gained 1..2.5% on 40..127-byte keys.

## Straight-line 32..63 and 64..127-byte tiers on AArch64

Spelled-out copies of one, two, or three mid rounds plus the generic
tail, nested under the existing 17..31-byte tier. The first absorb of
the first round folds in the known zero chain value, and the tail
conditions test the original length (for the 32..63 tier, `l > 48` and
`l > 32` replace the generic path's post-round `l > 16` and `l > 0`).
Measured +5..11% independent-hash throughput and +1..4% chained latency
across 32..127 bytes. The added dispatch compares cost 128..192-byte
keys about 1..1.7%, which the next item recovers.

## Unroll the AArch64 mid loop to 64-byte rounds

With the tiers returning everything below 128 bytes and the outlined
function taking everything at or above 320, the AArch64 mid loop only
serves 128..319-byte keys. Two rounds per iteration (with at most one
single round left over) halves its loop control: +4..5% for that band,
with the main function still free of callee-saved spills.

## A straight-line 128..191-byte tier on AArch64

A follow-up extended the tier chain by one band: four spelled-out
mid rounds, then the same optional-round-plus-tail shape as the
64..127 tier. Measured +6..7% independent-hash throughput on 128..159
bytes and +10..11% on 160..191 (the sizes that previously ran two
64-byte loop iterations), with chained latency up +0.2..3.2% across
the band. The extra dispatch compare costs 192..288-byte keys at most
0.5%, at the edge of the noise floor. 192..319 stays on the unrolled
mid loop: its loop control is a shrinking fraction of per-hash time,
so another tier would mostly add I-cache footprint.

## Second-pass ideas tested and rejected

- **Bulk dispatch after the tiers:** moving the 320-byte check below
  the tier chain saved one compare per tier but cost 3..6% on
  128..512-byte keys; the compare count for mid keys did not actually
  drop and both the mid and bulk layouts got worse.
- **Enabling the tiers on x86-64:** under Rosetta, 32..127-byte keys
  gained 3..7%, but 17..31-byte and 128..319-byte keys lost 1..3%.
  Mixed results from an emulation proxy did not justify committing.
  Superseded: the [third pass](pass-3-zen5.md) measured native silicon
  and landed the tiers for x86-64 GCC while keeping clang excluded.
- **Unrolling the x86-64 mid loop:** the same 64-byte rounds that the
  AArch64 mid loop runs gained 1..5% on 80..319-byte keys under
  Rosetta but cost up to 9% of 17..63-byte independent throughput.
  Superseded: on native silicon the shape wins under GCC when the
  tiers keep short keys out of the loop, and loses badly under any
  measured clang; the [third pass](pass-3-zen5.md) gates it
  accordingly.
- **An inline two-block unroll:** the first attempt kept the unrolled
  loop inline and lost up to half of small-key throughput to
  callee-saved register traffic; see the outlining note above for the
  mechanism. Rejecting the inline shape, not the unroll, was the
  lesson.
