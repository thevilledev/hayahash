# Evaluation records

These files hold the records used by the working paper.

- `apple-m1-chibihash.txt` contains three idle process replicates and the
  process-level medians transcribed into the paper.
- `apple-m1-provisional-background-load.txt` is an excluded run retained to
  show why process-level replication was added.
- `zen5-chibihash.txt` is the final-code Zen 5/GCC development run. It has
  only one process-level result and is therefore provisional.
- `apple-m1-smhasher3.txt`, `zen5-smhasher3-gcc.txt`, and `zen5-smhasher3-clang.txt`
  are the three SMHasher3 runs for release `v0.3.0`, all at upstream commit
  `6ab43433` through the adapter in `tests/smhasher3`. They cover both compiled
  dispatch shapes: `HAYAHASH64_INTERNAL_TIERS` is 1 on the M1 and under GCC on
  x86-64, and 0 under Clang on x86-64. Every non-timing line of the three
  outputs is identical, so the shapes differ only in speed.
- `quality.txt` is the current Hayahash local quality run.
- `quality-chibihash-v2.txt` is the expected-failure control run for the
  constructed rotation-orbit set.
- `conformance.txt` records language-port and native/WebAssembly checks.

The benchmark harness retains a median per cell but not its nine individual
sample values. A release-quality evaluation should record every sample,
operating-system and power-state details, exact competitor upstream revision,
and at least three process replicates on every host.

