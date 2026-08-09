# Evaluation records

These files hold the records used by the working paper.

- `apple-m1-pro-smhasher3-128-shootout.txt` and
  `ryzen-ai9-smhasher3-128-shootout.txt` are the current `v0.5` 128-bit
  competitive sweep at pinned SMHasher3 commit `51d3cd1a`. Each preserves
  three round-robin process values, the host-wide overhead correction, and
  the median used in the README and website. The EPYC conformance record
  supplies the full-suite column without treating VM timings as comparable.
- `apple-m1-pro-smhasher3-128-refresh.txt` and
  `ryzen-ai9-smhasher3-128-refresh.txt` supersede only the hayahash128 row
  of those sweeps: three fresh process replicates per host at the same
  pinned commit after the sixth optimization pass's digest-identical
  dispatch change, corrected to each sweep record's overhead baseline.
  Competitor rows still come from the sweep records.
- `epyc9655-smhasher3-128-conformance.txt` records the corresponding full-suite
  summaries, exact raw-output checksums, and the source-backed instruction
  class audit for all nine 128-bit hashes.
- `apple-m1-pro-hayahash128.txt`, `ryzen-ai9-hayahash128.txt`, and
  `epyc9655-hayahash128.txt` are the 2026-08-09 dual-width harness runs
  behind the current README and website tables. They record the full
  hayahash64/hayahash128 and ChibiHash output plus source checksums. The
  M1 and Ryzen records were re-run the same day after the sixth
  optimization pass's digest-identical 128-bit dispatch change (the
  pre-pass runs remain in git history); the EPYC record predates that
  pass, and as a KVM guest only its within-host ratios are comparable.
- `apple-m1-pro-wasm-hayahash128.txt` is the matching baseline-wasm32
  shootout. Its timing loops run inside wasm under Node/V8; the build has
  neither SIMD nor a wide multiply.
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
- `epyc7b13-zen3-smhasher3.txt` and `epyc9655-turin-smhasher3.txt` are the
  conformance records for two further hosts, an EPYC 7B13 (Zen 3) and an EPYC
  9655 (Zen 5 Turin), both KVM guests. They are consolidated rather than full
  raw dumps: all nine `v0.4.0` conformance runs are byte-identical once timing
  lines are removed, so these carry provenance, the verbatim sanity and summary
  sections, and a checksum of the normalized output that matches the others.
  Between them the four hosts cover two architectures, five compilers, and
  every dispatch shape. The Zen 3 host is the only one with no AVX-512, so it
  reaches the unvectorized GCC shape without being asked.
- `epyc7b13-zen3-smhasher3-shootout.txt` and
  `epyc9655-turin-smhasher3-shootout.txt` hold those hosts' speed figures.
  Both are shared virtual machines with no frequency control, so the records
  state plainly that absolute rates are not comparable with the bare-metal
  hosts and only within-host ratios should be read. On that basis the EPYC
  9655 reproduces the bare-metal Zen 5 ratios, and the Zen 3 host shows what
  losing the vectorized bulk path costs.
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
