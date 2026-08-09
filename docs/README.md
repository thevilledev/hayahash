# hayahash documentation

The top-level [`README.md`](../README.md) is the project overview and quick
start. Detailed evidence, measurements, and implementation notes live here:

- [`../CHANGELOG.md`](../CHANGELOG.md) - release history; digest-breaking
  changes are marked `DIGEST`
- [`../test_vectors/`](../test_vectors/) - versioned, language-agnostic
  known-answer digests for the current and historical digest series
- [`benchmarks.md`](benchmarks.md) - native and wasm measurements,
  ChibiHash comparisons, and the 64- and 128-bit SMHasher3 shootouts
- [`design.md`](design.md) - the four design ideas, and the structural
  collision classes and digest changes that shaped them
- [`quality.md`](quality.md) - what is tested and how: SMHasher3, the
  local harness, differential fuzzing, endianness and ABI coverage
- [`ports.md`](ports.md) - repository layout, per-language usage, and how
  every port is verified against the C reference
- [`smhasher3.md`](smhasher3.md) - how to run and reproduce the SMHasher3
  suite and the speed shootout, including the corrections the published
  numbers depend on
- [`optimization/`](optimization/) - the optimization log: five measured
  passes, the ideas that were rejected along the way, and the open
  experiment list

The authoritative commentary on the algorithm itself is in
[`hayahash.h`](../hayahash.h): every structural decision is documented
where it lives in the code. The working paper and its claim-by-claim
evidence register are under [`paper/`](../paper/).
