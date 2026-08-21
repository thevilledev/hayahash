# Evaluation records

Raw outputs backing the working paper, the benchmark page, and the claim
register in `../AUDIT.md`. Every record here was produced at the current
digest; records for superseded digests are not kept.

## Quality and conformance

- `quality.txt` - the local quality harness at this snapshot: strict
  avalanche over input and seed bits, exact-collision checks over 24
  structured key sets, and the streaming differential. The generator is
  fixed-seeded, so the numbers depend on the digest, not the host.
- `quality-chibihash-v2.txt` - the expected-failure control: the same
  harness run against vendored ChibiHash v2, where the constructed
  rotation-orbit set collides by design.
- `conformance.txt` - the C reference checks plus the Go, Rust, and Python
  suites, rerun while preparing the draft. The remaining ports rest on
  repository CI; the record lists that limit.
- `epyc9655-smhasher3-128-conformance.txt` - the full SMHasher3 default
  suite for hayahash128 at pinned upstream commit `51d3cd1a`, with
  raw-output checksums and a source-backed instruction-class audit of all
  nine 128-bit hashes measured. The host is a KVM guest; its timings back
  no claim.

## Speed

- `apple-m1-pro-hayahash128.txt`, `ryzen-ai9-hayahash128.txt`,
  `epyc9655-hayahash128.txt` - dual-width harness runs behind the tables in
  `docs/benchmarks.md`. Each records the full hayahash64 / hayahash128 and
  ChibiHash output plus source checksums. The EPYC host is a KVM guest, so
  only its within-host ratios are comparable.
- `apple-m1-pro-smhasher3-128-shootout.txt`,
  `ryzen-ai9-smhasher3-128-shootout.txt` - the 128-bit competitive sweep at
  pinned SMHasher3 commit `51d3cd1a`. Each preserves three round-robin
  process values, the host-wide overhead correction, and the median used in
  the published tables.
- `apple-m1-pro-smhasher3-128-refresh.txt`,
  `ryzen-ai9-smhasher3-128-refresh.txt` - three fresh process replicates of
  the hayahash128 row only, at the same pinned commit, corrected to each
  sweep record's overhead baseline. Competitor rows come from the sweep
  records.
- `apple-m1-pro-wasm-hayahash128.txt` - the baseline-wasm32 shootout. Timing
  loops run inside wasm under Node/V8; the build has neither SIMD nor a wide
  multiply.

## Method and known limits

Speed records state host, compiler, dispatch shape, and source checksums.
The benchmark harness retains a median per cell, not its nine individual
samples. A release-quality evaluation should record every sample, operating
system and power state, exact competitor upstream revision, and at least
three process replicates on every host.

The paper itself carries no benchmark tables. Its only performance statement
is the analytic cost model, derived from dependency graphs rather than from
these records.
