# Roadmap

Where hayahash stands against established hash libraries, what is missing,
and the order in which to close it.

The comparison set is the repositories a prospective user would weigh
hayahash against: [xxHash](https://github.com/Cyan4973/xxHash) and
[BLAKE3](https://github.com/BLAKE3-team/BLAKE3) at the mature end,
[wyhash](https://github.com/wangyi-fudan/wyhash),
[komihash](https://github.com/avaneev/komihash),
[rapidhash](https://github.com/Nicoshev/rapidhash) and
[ChibiHash](https://github.com/N-R-K/ChibiHash) at the single-header end.

Speed is not the gap. Neither is the depth of in-tree testing. What
separates hayahash from the mature end of that set is reach, verification
by someone other than its author, and ports that only do half of what the
reference does.

## Where hayahash already stands

Several things here are ahead of the single-header field and, in a few
cases, ahead of xxHash and BLAKE3:

- [`test_vectors/`](../test_vectors/) is a versioned, language-agnostic
  conformance artifact with a documented format, a shared input formula,
  and an add-a-file-never-edit policy. Most hash projects publish
  per-language tables or nothing.
- One SemVer across ten implementations, with a release-time job that
  re-derives every manifest and refuses to publish on disagreement.
- Trusted publishing (OIDC, no long-lived tokens) on npm, crates.io,
  PyPI and NuGet.
- [`stability.md`](stability.md) is an explicit adoption contract: what
  "frozen" means, five numbered criteria to cut 1.0, and what stays out
  of scope.
- [`../paper/AUDIT.md`](../paper/AUDIT.md) is a claim-by-claim register
  that distinguishes `reproduced` / `archived` / `code-derived` /
  `reported` / `open`, and says plainly when the evidence for a claim
  belongs to a superseded digest.
- Every GitHub Action is SHA-pinned, with a `zizmor` job linting the
  workflows themselves.
- The `js` job rebuilds the committed wasm from `hayahash.h` and
  `git diff --exit-code`s it, so a stale binary cannot stay green.
- The `hayasum` fuzz targets assert invariants rather than only crashes,
  and ship a libFuzzer-free replay driver so every CI compiler runs the
  corpus.

The gap list below should be read against that baseline.

## Gaps

### Reach: C and C++ consumers

This is the largest structural gap and the one that most directly bounds
adoption.

| Missing | Comparison |
|---|---|
| `CMakeLists.txt`, `meson.build` | xxHash and BLAKE3 ship both CMake and a `find_package()` config export |
| vcpkg port, Conan recipe | both follow from CMake; neither exists today |
| `extern "C"` / `__cplusplus` guard | xxHash, BLAKE3 and wyhash all guard |
| Any C++ compile in CI | verified: no `g++`, `clang++` or `.cpp` build outside the SMHasher3 adapter |
| Compiled library artifact | header-only with no `HAYAHASH_IMPLEMENTATION`-style option, so no `.a`, no `.so`, no soname, no ABI |
| Runtime dispatch | XXH3 has `xxh_x86dispatch.h`; BLAKE3 dispatches across SSE2/SSE4.1/AVX2/AVX-512/NEON |

Two smaller items in the same bucket. 18 `hayahash*_internal_*` symbols
(15 helpers plus three enum constants) and 13 `HAYAHASH*_INTERNAL_*`
macros leak into the consumer's namespace, and the header contains zero
`#undef`. And the one-shot entry points take `ptrdiff_t`:

```c
hayahash64(const void *keyIn, ptrdiff_t len, uint64_t seed)
```

xxHash, BLAKE3 and wyhash all take `size_t`. The signed length is
defensible and documented, but it is a surprise at every call site and a
friction point for binding generators.

The runtime-dispatch gap has a concrete cost worth stating: the AVX-512DQ
path behind the fastest recorded bulk rate is gated on
`__znver4__ || __znver5__` at compile time. A distro or vcpkg build for
generic `x86-64` never selects it, so the number a packaged hayahash
delivers is not the number in [`benchmarks.md`](benchmarks.md).

Distribution has two more holes. [`../cli/`](../cli/) has never shipped in
a release and is packaged nowhere - you must clone and run `make -C cli`,
where `xxhsum` and `b3sum` are in Debian, Homebrew, Alpine and nixpkgs.
And the Swift and Zig packages sit in monorepo subdirectories, so neither
is consumable by Git URL; both require a local path dependency or a
release tarball.

### Port parity

Streaming exists in C only.

`hayahash64_init` / `_update` / `_digest` and their 128-bit counterparts
are in [`hayahash.h`](../hayahash.h), but **zero of the nine ports expose
them**. This is the sharpest gap in the repository, because v0.5.0 was a
digest-breaking change made specifically to enable streaming - every
output value changed to buy an API that nine of ten implementations do
not have. The nine streaming rows in
[`test_vectors/v0.5.0.txt`](../test_vectors/v0.5.0.txt) are consumed by
nothing but the C generator's own self-check. `js/wasm/shim.c` says so
outright: the shim "exports only the one-shot function ... until a
streaming export exists."

Idiomatic integration is Rust-only. `rust/src/hasher.rs` provides
`HayaHasher` (`Hasher` + `BuildHasher`) and the `HayaHashMap` /
`HayaHashSet` aliases. Nothing equivalent exists elsewhere:

| Language | Expected integration | Present |
|---|---|---|
| Go | `hash.Hash64`, `io.Writer` | no |
| Java | `java.util.zip.Checksum` | no |
| C# | `NonCryptographicHashAlgorithm` | no |
| Python | `hashlib`-style `update`/`digest` | no |
| Zig | `std.hash`-style `init`/`update`/`final` | no |
| Swift | `Hashable` / `HashFunction` bridge | no |
| JS | incremental API | no |

Most of these fall out of implementing streaming, which is why they are
one workstream rather than eight.

There are also no language-native benchmarks anywhere. Every number in
[`benchmarks.md`](benchmarks.md) is C or wasm, so a Go, Java or .NET user
has no way to measure hayahash in the runtime they will actually deploy
it in.

### CI that proves the documented claims

Three findings here are more serious than their size suggests.

**The cross-port differential gate runs zero cases on pull requests.**
Every port's differential test skips silently when `HAYAHASH_CORPUS` is
unset, and `ci.yml` never sets it - the variable appears only in
`differential.yml`. So the flagship conformance mechanism catches a
divergence introduced in a PR up to 24 hours later, in a nightly job that
`continue-on-error`s each step rather than failing the build.

**No AArch64 and no macOS runner exists in `ci.yml`.** Every job runs on
x86-64 Linux or Windows. The header has eight `__aarch64__` sites that
compile on the maintainer's M1 on every local build and on a pull request
never. The only automated ARM and macOS execution of the C reference is
`cibuildwheel` on `ubuntu-24.04-arm` and `macos-14` in `release.yml`,
which runs at tag time only. This also blocks
[`stability.md`](stability.md)'s own 1.0 criterion of two architectures.

**One of the two documented dispatch shapes never compiles in CI.** The C
jobs pass `ARCH=` empty, which kills the Makefile's `-march=native`, so
`__AVX512DQ__` is never defined and `HAYAHASH64_INTERNAL_VECGCC` is never
selected. Shape equivalence rests entirely on manual runs.

Beyond those: big-endian coverage is 42 lines (three variants, 14
lengths, one seed), so the published vector file, the streaming samples,
the 24 collision sets and the differential corpus never run big-endian.
There is no coverage instrumentation in any language, no property-based
testing in any of the nine, no MSan, TSan, valgrind or GCC sanitizer
build, and no native 32-bit target.

### External validation

Every quality claim hayahash makes is currently produced by hayahash.

The hash is not registered upstream in
[SMHasher3](https://gitlab.com/fwojcik/smhasher3). The adapter in
[`../tests/smhasher3/`](../tests/smhasher3/) is upstream-shaped and
pinned, but unsubmitted. The competitors hayahash benchmarks against -
wyhash, XXH3, komihash, rapidhash - are all upstream-registered, which is
exactly why anyone can reproduce their pass/fail numbers without
trusting the author's tree. Until hayahash is upstream, 188/188 is a
self-run result against a self-maintained adapter, and SMHasher3 never
runs in CI.

Evidence for the shipping digest is also thinner than for the one it
replaced. v0.5 has one archived full-suite record: 128-bit, one KVM host,
one compiler. hayahash64 at v0.5 is `reported` with no raw dump. The
nine-build, four-host, five-compiler sweep belongs to the superseded v0.4
digest. [`quality.md`](quality.md) and
[`../paper/AUDIT.md`](../paper/AUDIT.md) both say this plainly - the
honesty is not the problem, the coverage is.

The hash core itself is never fuzzed; all four targets are `hayasum` CLI
surface. `make -C paper check` is not wired into any workflow, so the
snapshot register can drift from the header between manual runs. There is
no SBOM, no build provenance attestation, no CodeQL and no OpenSSF
Scorecard. And the digest is unfrozen, having moved twice in eleven days
(v0.3.0 and v0.5.0), so the soak clause in
[`stability.md`](stability.md) has not started.

## Fix now

Three items are wrong today rather than merely absent, and are small
enough not to wait for a phase:

1. **The PR differential no-op.** Set `HAYAHASH_CORPUS` in `ci.yml` with
   a small corpus and fail rather than `continue-on-error`.
2. **`rust/src/hasher.rs:32` documents a constraint that v0.5.0
   removed.** It still reads "hayahash64 premixes the total input length
   into its state before absorbing any bytes, so it cannot hash a stream
   incrementally." `HayaHasher` therefore buffers the entire input into a
   `Vec` on the strength of a premise the algorithm no longer has: a
   1 GiB stream allocates 1 GiB.
3. **Streaming is advertised without its caveat.** The
   [`README`](../README.md) feature list and the `stability.md` status
   table both present streaming as part of the public API surface without
   noting it is C-only.

## Phases

### Phase 1 - C and C++ reach

Add `CMakeLists.txt` exporting an installable interface target and a
`find_package(hayahash)` config, consistent with the existing
`make install` and [`../hayahash.pc.in`](../hayahash.pc.in). Add
`extern "C"` and `__cplusplus` guards, `#undef` the internal macros, and
add a C++ compile-and-KAT job to `ci.yml` so the guard is actually
exercised. Then a vcpkg port and a Conan recipe, `hayasum` binaries as
release assets, and a Homebrew formula.

*Done when* a C++ project can consume hayahash via `find_package` or
`vcpkg install`, and CI proves the header compiles clean under a C++
compiler.

### Phase 2 - Port parity

Implement streaming in every port against the streaming rows already
sitting in [`test_vectors/`](../test_vectors/), then the idiomatic
wrappers that fall out of it, then language-native benchmarks. This
retires fix-now item 2 as a side effect and makes v0.5.0's digest break
worth what it cost.

*Done when* every maintained port passes the streaming vectors and the
README's streaming claim needs no caveat.

**Status:** streaming has landed in Rust, Go, Zig, Java, C#, Python and
JavaScript, on the wasm engine where one is available. Swift is the one
port still one-shot only. Language-native benchmarks are still open.

### Phase 3 - Make CI prove the claims

Set `HAYAHASH_CORPUS` on pull requests and fail on divergence. Add
`ubuntu-24.04-arm` and `macos-14` jobs. Force both dispatch shapes rather
than accepting whatever `ARCH=` yields. Widen the s390x job past 42
lines. Wire `make -C paper check`.

*Done when* the two-architecture and every-dispatch-shape criteria in
[`stability.md`](stability.md) are satisfied by automation rather than by
the maintainer's laptop.

### Phase 4 - External validation, then the freeze

Submit the adapter to SMHasher3 upstream. Re-run the multi-host,
multi-compiler sweep for the v0.5 digest so `AUDIT.md` can move the
64-bit full-suite row from `reported` to `archived`. Then execute the
five 1.0 criteria in [`stability.md`](stability.md), soak period
included.

*Done when* a third party can reproduce 188/188 without cloning this
repository, and digests are frozen.

### Phase 5 - Assurance depth and supply chain

Fuzz the hash core over arbitrary `(length, alignment)` rather than only
the CLI. Add coverage in at least C and Rust. Add property-based tests
where the language has a good library. Add release provenance
attestations, CodeQL and a Scorecard badge. Write a standalone
implementable specification so a third party can port hayahash without
reading 1945 lines of C.

## On the ordering

Phase 1 is a reach bet, and it is worth being explicit that it is not the
largest gap by either other measure.

Phase 2 is the largest **functional** gap: nine ports missing the feature
that a digest break was spent to enable. Phase 4 is the largest
**credibility** gap: no claim in this repository has yet been checked by
anyone outside it, and no amount of additional in-tree testing changes
that.

The case for Phase 1 first is that reach compounds. CMake is the
precondition for vcpkg, Conan and distro packaging, and those are how a
hash function acquires the users whose bug reports and independent
benchmarks constitute real external validation. The case against is that
it widens the top of a funnel whose next step still says "do not use
hayahash yet where hashes must remain stable across versions."

Both readings are defensible. This document records the choice rather
than pretending there was only one.
