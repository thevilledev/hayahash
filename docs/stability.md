# Digest stability and 1.0 criteria

hayahash is experimental while its major version is `0`. The algorithm,
constants, and digest values may change between releases. Do not persist
hayahash digests across versions until 1.0 freezes them.

This document is the adoption contract for when digests become immutable.

## Current status

| Item | Status |
|---|---|
| Public API surface (64/128 one-shot) | Present in 0.5.x |
| Streaming API (init/update/digest) | C reference and every maintained port |
| SMHasher3 default suite (both widths) | 188/188 on recorded hosts |
| Cross-port bit-exactness | Required in CI + nightly differential |
| Digest freeze | **Not yet** — pre-1.0 |

The v0.5 development digest moved length absorption into the finalizer so
streaming digests can match one-shot digests. That change altered every
output relative to 0.4.x. Further pre-1.0 digest changes remain allowed
when quality or streaming/API goals require them.

## What "frozen" means

After 1.0.0:

1. For a given major version, `hayahash64` and `hayahash128` return the
   same digests for the same `(input, seed)` on every maintained port and
   dispatch shape.
2. Streaming `init` / `update` / `digest` remains one-shot-identical for
   every split of the same input.
3. The invariant `hayahash128.lo == hayahash64` remains true.
4. Digest-breaking changes require a new major version (2.0, …), not a
   minor or patch release.

Bug fixes that do not change digests, new language ports, packaging, and
documentation may ship in minor/patch releases of a frozen major line.

## Criteria to cut 1.0.0

All of the following must hold on a release candidate before the digests
are declared frozen:

1. **Quality**
   - hayahash64 and hayahash128 each pass all applicable SMHasher3 default
     tests (currently 188/188) on at least two CPU architectures and both
     major compiler families used in CI (Clang and GCC), covering every
     documented dispatch shape.
   - The local avalanche / structured-collision harness is clean.
2. **Conformance**
   - Every maintained language port is bit-exact against `hayahash.h` for
     the published known-answer vectors and the nightly differential corpus.
   - Big-endian (s390x), wasm32, MSVC x64, and the MIPS64 n64 port remain
     in the conformance set or have an explicit, documented exception.
3. **API freeze candidate**
   - One-shot and streaming APIs for both widths are considered final for
     1.x; any remaining breaking API rename happens before 1.0.
   - A versioned known-answer artifact for the frozen digest is published
     in-tree (see `test_vectors/` when present) or an equivalent release
     asset.
4. **Threat-model documentation**
   - [`SECURITY.md`](../SECURITY.md) remains accurate: non-cryptographic
     use only; seed secrecy guidance for hash-table HashDoS.
5. **Soak period**
   - At least one minor 0.x release after the last intentional digest
     change has shipped with no further digest-affecting fixes required.

## Out of scope for the 1.0 freeze

These may keep evolving after digests freeze:

- throughput-oriented dispatch (tiers, unrolling, auto-vectorization), as
  long as outputs stay bit-identical
- new language ports and packaging
- benchmark methodology and website tooling
- optional wider or keyed variants published under different names/APIs

## If a frozen digest must change

Only a new major version may change digests. The release notes must:

- mark the break explicitly
- publish new known-answer vectors
- re-run the SMHasher3 and conformance gates above

There is no silent digest fix on a frozen major line.
