# hayahash documentation

The top-level [`README.md`](../README.md) carries the pitch, the benchmark
tables, and the headline quality claims. Everything behind those claims
lives here:

- [`design.md`](design.md) - the four design ideas, and the structural
  collision classes that shaped them
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
