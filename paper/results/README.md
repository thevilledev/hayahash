# Evaluation records

These files hold the records used by the working paper.

- `apple-m1-chibihash.txt` contains three idle process replicates and the
  process-level medians transcribed into the paper.
- `apple-m1-provisional-background-load.txt` is an excluded run retained to
  show why process-level replication was added.
- `zen5-chibihash.txt` is the final-code Zen 5/GCC development run. It has
  only one process-level result and is therefore provisional.
- `apple-m1-smhasher3.txt`, `zen5-smhasher3-gcc.txt`,
  `zen5-smhasher3-gcc-novec.txt`, and `zen5-smhasher3-clang.txt` are the four
  SMHasher3 conformance runs for release `v0.4.0`, all at upstream commit
  `6ab43433` through the adapter in `tests/smhasher3`. Between them they cover
  every dispatch shape the header compiles - `TIERS` 1 and 0, and the new
  `VECGCC` bulk vectorization on and off - across two architectures. Every
  non-timing line of all four is identical, so the shapes differ only in speed.
  Note that the M1 compiles the compact shape as of `v0.4.0`; it compiled the
  wide one at `v0.3.0`.
- `apple-m1-smhasher3-shootout.txt` and `zen5-smhasher3-shootout.txt` are the
  competitor speed comparisons behind the table in `README.md`. They record
  the overhead-calibration correction their numbers depend on, why the M1
  build needs the Apple Silicon patch before any SIMD hash can be compared
  fairly there, and the full-suite pass/fail for every hash measured. The Zen 5
  record also measures hayahash across all three dispatch shapes, because its
  bulk rate there depends on whether the compiler vectorizes the bulk loop.
  The paper itself makes no competitor-speed claim from them.
- `quality.txt` is the current Hayahash local quality run.
- `quality-chibihash-v2.txt` is the expected-failure control run for the
  constructed rotation-orbit set.
- `conformance.txt` records language-port and native/WebAssembly checks.

The benchmark harness retains a median per cell but not its nine individual
sample values. A release-quality evaluation should record every sample,
operating-system and power-state details, exact competitor upstream revision,
and at least three process replicates on every host.

