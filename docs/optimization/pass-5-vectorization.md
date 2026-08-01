# Fifth pass: mixed-size dispatch re-audit, GCC bulk vectorization

Part of the [optimization log](README.md). This is the historical
record of the fifth pass.

This pass started from a doubt about the straight-line tiers ("is the
weird branching really faster than the regular way?") and re-measured
the dispatch question on both reference machines with the
[fourth pass](pass-4-band-screens.md)'s mixed-size A/B methodology,
then landed the largest single throughput win so far by teaching GCC
to auto-vectorize the bulk loop. Toolchains: Apple clang 21 on the M1
(unpinned; see the thermal note below), GCC 16.1 and stock clang
22.1.8 on the Zen 5 box pinned to a 5.16 GHz classic core. Every
change kept the exhaustive bit-exactness gate, run per experiment on
both machines.

## The tiers are a GCC shape; clang now gets compact everywhere

The [fourth pass](pass-4-band-screens.md) measured broad mixed-size
workloads only on Zen 5 clang. Running them on the M1 shows Apple
clang 21 has the same front-end problem: with the whole tier chain
live and per-hash branch targets unpredictable, the compact dispatch
beats the tiers by 2..8% independent and up to 10% chained on broad
mixes (1..319, 17..319, 128..319, 1..512, 64..512), is flat on 1..64
mixes and at fixed 17..31, and gives back 6..11% only at fixed
32..192-byte sizes plus about 9% on a single-band 192..255 mix. On
Zen 5 GCC the same comparison goes the other way at every point:
compact costs 3..13% at fixed sizes and 2..10% on every mixed band,
chained included. GCC shrink-wraps the chain and predicts it well;
both measured clangs pay for it in exactly the workloads a
general-purpose default has to assume. The tier gate is now
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

## Thermal discipline for M1 mixed numbers

The fanless M1 Air's mixed-size results move by tens of percent on
specific bands (1..64 swung from +13% to -16% between repeats of
byte-identical binaries) once the machine has been benching for
half an hour. Decisions in this pass used three cool-machine
replicates after a four-minute idle window, which brought 1..64 to
a stable +-2%. Fixed-size rows were far less sensitive. Treat any
future M1 mixed delta measured on a warm machine as noise until it
reproduces cold.

## Guards re-audited: obsolete purpose, new job, unchanged verdict

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

## Store-seeded SLP: GCC reaches clang's AVX-512 bulk rate

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
