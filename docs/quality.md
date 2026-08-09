# Quality and verification

What is tested, where it runs, and how to reproduce it. The
claim-by-claim evidence register for the working paper is
[`paper/AUDIT.md`](../paper/AUDIT.md); the archived raw runs are under
[`paper/results/`](../paper/results/).

## SMHasher3

- Full default suite: **188/188 tests passed**, verification value
  `0x65F2AC15` (canonical little-endian reading; the byte-swapped
  value is `0x805DE5C0`). ChibiHash v2 also passes 188/188; v1 fails.
- hayahash128: **188/188 tests passed** with 128-bit-wide expectations,
  verification value `0x3F0411F4` (canonical little-endian reading;
  the byte-swapped value is `0x46140A64`). The complete run took
  839.5 seconds on an Apple M1 Pro.
- The `v0.5` digest (length absorbed in the finalizer rather than the
  premix, enabling the streaming API) passed the full suite on an
  Apple M1 Pro, with all dispatch shapes measured bit-identical
  locally. Release `v0.4.0` had additionally been verified on nine
  builds spanning four hosts, two architectures, and five compilers;
  that sweep has not been repeated for the new digest yet.
- The self-contained adapter lives in
  [`tests/smhasher3/`](../tests/smhasher3/) and mirrors the reference
  implementation in SMHasher3's upstream-ready form.
  [`smhasher3.md`](smhasher3.md) covers running, reproducing, and
  re-deriving the verification values after a digest change.

## Published test vectors

[`test_vectors/`](../test_vectors/) holds versioned known-answer digests
for the current digest series (`v0.5.0.txt` for the v0.5 streaming
digest). `make -C test_vectors check` recomputes every row from
`hayahash.h` and requires an exact match. Prefer these files over
in-tree language-port tables when implementing hayahash outside this
repository. Digest-breaking releases add a new vector file and record a
`DIGEST` entry in [`CHANGELOG.md`](../CHANGELOG.md).

## Local harness

`make -C tests run-quality` runs a strict avalanche criterion over
input and seed bits, plus exact-collision tests over 24 structured key
sets, including reproductions of the SMHasher3 keysets that broke
earlier iterations of the design. All clean. The same harness can run
the constructed rotation-orbit set against ChibiHash v2
(`./tests/quality v2`), where it finds 512 colliding pairs by design -
an expected-failure control, not a general quality ranking.

The target also runs `tests/hash128.c`: six fixed known-answer vectors,
all lengths through 512 under three seeds and five update patterns,
1,000 randomized cases through 20 KiB under three larger-input split
patterns, and non-mutating/continued digest checks. Every case requires
one-shot and streaming equality and `hayahash128.lo == hayahash64`.

## Cross-port conformance

The Rust, Go, Zig, Java, C#, Python, Swift, JavaScript, and MIPS64
assembly ports of both digest widths are bit-exact against the C reference:
each port's test suite checks the 64-bit SMHasher3 verification value,
shared 64-bit known-answer vectors, fixed 128-bit boundary vectors, and
the invariant `hayahash128.lo == hayahash64`.
(The JavaScript package checks both of its engines: the wasm build of
the reference header and the pure-JS fallback.)

Nightly differential conformance fuzzing generates one C-reference
corpus containing both output words, with random input bytes, random
64-bit hash seeds, exhaustive
lengths 0..384, and boundary-biased random lengths through the
128 KiB edge. Every language port consumes the identical corpus
(including both JavaScript engines); the MIPS assembly port is not in
the nightly matrix and relies on the shared known-answer vectors
instead. The logged PRNG seed or the failure artifact reproduces a run
exactly; the workflow can also be dispatched manually with a chosen
seed. See [`tests/differential/`](../tests/differential/) for local
replay commands.

## Endianness and ABI coverage

Endianness is tested in CI: the shared KAT is also produced on s390x
(big-endian, via `zig cc` + qemu-user) and must match the
little-endian reference. wasm32 covers the ILP32 case; MSVC x64 covers
the Windows ABI; the MIPS64 port runs under qemu-mips64el.

## Structural arguments

The absorb sequence is injective by construction (first-difference
induction), and all tail injections are bijective. These are
structural arguments about the absorb, not collision-resistance proofs
for the complete hash; [`design.md`](design.md) and the header notes
state their scope.
