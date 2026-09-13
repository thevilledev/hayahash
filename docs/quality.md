# Quality and verification

**Both widths pass all 188 default SMHasher3 tests at the pinned revision.**
CI also checks that implementations agree across languages and platforms.
These are quality and compatibility checks, not cryptographic guarantees.

[Raw results](../paper/results/) and the [paper's evidence register](../paper/AUDIT.md)
record the supporting runs and their limitations.

## SMHasher3

| Output | Default tests | Verification (little-endian) | Byte-swapped |
|---|---:|---|---|
| hayahash64 | 188/188 | `0x65F2AC15` | `0x805DE5C0` |
| hayahash128 | 188/188 | `0x3F0411F4` | `0x46140A64` |

The 128-bit run uses 128-bit-wide expectations. Every compiled dispatch
shape must produce identical non-timing output. A full suite sweep across
multiple hosts and compilers at the current digest remains open work.

See the [SMHasher3 guide](smhasher3.md) for the adapter, build matrix, and
verification procedure.

## Published test vectors

Use [`test_vectors/`](../test_vectors/) when implementing hayahash outside
this repository. These versioned files contain expected digests from the C
reference. Check them with:

```sh
make -C test_vectors check
```

Digest-breaking releases add a new vector file and a `DIGEST` entry to the
[changelog](../CHANGELOG.md).

## Local harness

```sh
make -C tests run-quality
```

The harness checks input-bit and seed-bit avalanche behavior, plus exact
collisions across 24 structured key sets. These include regression cases
for the patterns described in the [design notes](design.md#cancellation-channels).
All pass. `./tests/quality v2` provides an expected-failure control against
ChibiHash v2's rotation-orbit case; it is not a general quality ranking.

The target also runs `tests/hash128.c`: fixed vectors, all lengths through
512 under three seeds and five update patterns, and 1,000 randomized cases
through 20 KiB. It checks one-shot/streaming equality, continued hashing
after taking a digest, and `hayahash128.lo == hayahash64`.

## Cross-port conformance

Every port checks shared known-answer vectors, the 64-bit SMHasher3
verification value, 128-bit boundary vectors, and the low-word invariant.
JavaScript checks both its wasm and pure-JS engines.

Pull requests run a 406-case C-reference differential corpus. Nightly runs
expand this to 32,768 cases: lengths 0–384, fixed boundary cases, and random
bytes and seeds at boundary-biased lengths through the 128 KiB edge. All
high-level language ports consume the same corpus. MIPS64 assembly uses
shared vectors and is outside the differential matrix.

The logged PRNG seed or failure artifact reproduces a run. See
[local replay commands](../tests/differential/).

## Endianness and ABI coverage

CI checks big-endian s390x, wasm32 (ILP32), MSVC x64, and MIPS64 n64
against the shared vectors. The s390x and MIPS64 jobs run under qemu.

## Structural arguments

The [design notes](design.md) and [working paper](../paper/) describe local
properties of the absorb chain, tail, finalizers, and short-input path.
These do not prove collision resistance for the complete hash.

See the [security policy](../SECURITY.md) and [1.0 freeze criteria](stability.md)
before adopting hayahash.
