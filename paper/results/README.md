# Evaluation records

These files hold the records used by the working paper.

- `apple-m1-chibihash.txt` contains three idle process replicates and the
  process-level medians transcribed into the paper.
- `apple-m1-provisional-background-load.txt` is an excluded run retained to
  show why process-level replication was added.
- `zen5-chibihash.txt` is the final-code Zen 5/GCC development run. It has
  only one process-level result and is therefore provisional.
- `quality.txt` is the current Hayahash local quality run.
- `quality-chibihash-v2.txt` is the expected-failure control run for the
  constructed rotation-orbit set.
- `conformance.txt` records language-port and native/WebAssembly checks.

The benchmark harness retains a median per cell but not its nine individual
sample values. A release-quality evaluation should record every sample,
operating-system and power-state details, exact competitor upstream revision,
and at least three process replicates on every host.

