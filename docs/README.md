# hayahash documentation

The top-level [`README.md`](../README.md) is the project overview and quick
start. Details live here.

**The algorithm**

- [`design.md`](design.md) - the complete algorithm: constants, dispatch,
  absorb, tail, finalizers, both output widths, and the cancellation
  channels the constants close
- [`implementation.md`](implementation.md) - how the header compiles per
  target, and the digest-changing ideas that were screened and rejected
- [`../paper/`](../paper/) - the working paper: exact specification, proofs
  of the structural properties, and the claim-by-claim evidence register

The authoritative commentary on the algorithm is
[`hayahash.h`](../hayahash.h) itself: every structural decision is
documented where it lives in the code.

**Evidence**

- [`quality.md`](quality.md) - what is tested and how: SMHasher3, the local
  harness, differential fuzzing, endianness and ABI coverage
- [`smhasher3.md`](smhasher3.md) - running and reproducing the suite and the
  speed shootout, including the corrections the published numbers depend on
- [`benchmarks.md`](benchmarks.md) - native and wasm measurements, ChibiHash
  comparisons, and the 128-bit SMHasher3 shootout
- [`../test_vectors/`](../test_vectors/) - versioned, language-agnostic
  known-answer digests

**Using it**

- [`ports.md`](ports.md) - repository layout, per-language usage, and how
  every port is verified against the C reference
- [`stability.md`](stability.md) - experimental status and the 1.0
  digest-freeze criteria
- [`../SECURITY.md`](../SECURITY.md) - threat model and vulnerability
  reporting
- [`../CHANGELOG.md`](../CHANGELOG.md) - release history; digest-breaking
  changes are marked `DIGEST`

**Project**

- [`roadmap.md`](roadmap.md) - how hayahash compares to established hash
  repositories, what is missing, and the order for closing it
- [`deployment.md`](deployment.md) - website Pages and Cloudflare setup
