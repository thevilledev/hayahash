# Changelog

All notable changes to hayahash are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project uses [Semantic Versioning](https://semver.org/).

**Digest changes** (rows marked `DIGEST`) alter every output value for the same
input and seed. Do not mix digests across releases when hashes are persisted.
While hayahash is pre-1.0 and marked experimental, digests may still change;
see the experimental notice in the README and
[`docs/stability.md`](docs/stability.md).

Known-answer vectors for the current digest live under
[`test_vectors/`](test_vectors/).

## [Unreleased]

### Fixed

- CMake consumer test follows the root `VERSION` instead of requesting
  0.5 after a minor-version bump. CMake usage examples now request 0.6.
- JS `PureHasher.update` (the pure fallback behind `Hasher`) no longer
  produces a wrong digest for a single chunk of 2 GiB plus 128 bytes or
  more. The direct-block count was rounded with int32 bitwise ops, which
  wrapped negative.
- `hayasum --seed` / `-s` rejects a doubled hex prefix such as `0x0x10`.
  `strtoull` in base 16 skips its own optional `0x`, so the strict parse
  accepted the second prefix and hashed with seed 16.
- Java `Hasher.update(byte[], int, int)` no longer throws
  `ArrayIndexOutOfBoundsException` when a chunk close to
  `Integer.MAX_VALUE` bytes follows buffered input. The buffering check
  `nbuf + n < BUF_CAP` overflowed `int` and took the buffered branch.
- Swift `Hasher.update` no longer traps on 32-bit platforms (watchOS
  `arm64_32` / `armv7k`) when a chunk close to `Int.max` bytes follows
  buffered input. The buffering check `nbuf + remaining < bufCap`
  overflowed `Int`.
- Go `Digest.Write` no longer corrupts its state on 32-bit targets
  (`386`, `arm`) when a slice close to 2 GiB follows buffered input. The
  buffering check `d.nbuf+n < bufCap` overflowed `int`, truncated the
  copy, and later panicked.
- C# `Hasher.Update` no longer throws when a span close to `int.MaxValue`
  bytes follows buffered input. The buffering check
  `_nbuf + data.Length < BufCap` wrapped negative and tried to copy the
  whole span into the 448-byte buffer.

## [0.6.0] - 2026-09-13

### Added

- Bit-exact pure Haskell port (`haskell/`) over strict `ByteString`, with
  64- and 128-bit one-shot hashing and an immutable, bounded-memory
  incremental `Hasher`. Its Cabal test suite pins known-answer and streaming
  vectors, the SMHasher3 verification value, and the shared randomized
  C-reference differential corpus.
- Root `Makefile` install path for the C header plus a `hayahash`
  pkg-config package (`VERSION`, `hayahash.pc.in`); `scripts/bump-version.sh`
  and the release version guard keep `VERSION` in lockstep with port
  manifests.
- `hayasum`, a CLI that hashes files or stdin with the C reference
  ([`cli/`](cli/)). Short and long options with attached or separate
  values, `--` to end them, `-V` reporting the root `VERSION`, and exit
  status `0` / `1` / `2` for success, I/O error, and usage error.
- `make -C cli check`: a POSIX-shell functional harness
  ([`cli/tests/run.sh`](cli/tests/run.sh)) over option parsing, exit
  statuses, diagnostics, escaping, and read/write errors, plus a
  differential of hayasum's chunked reader against a one-shot oracle at
  every size where its buffering changes. Runs on both compilers and
  under ASan/UBSan in CI.
- Coverage-guided fuzzing of hayasum's argv parsing, byte reader, and
  output escaping ([`cli/fuzz/`](cli/fuzz/)), with a committed seed
  corpus, invariant assertions rather than crash-only checks, and a
  libFuzzer-free replay driver so every CI compiler runs the corpus.
  30 seconds per target on pull requests; longer nightly runs in
  `fuzz.yml`.
- C++ conformance job: the existing KAT, published-vector, and
  128-bit/streaming harnesses are rebuilt with g++ and clang++ at C++11
  and C++17. `hayahash.h` is a C header that C++ projects include, and
  nothing compiled it as C++ before, so a valid-C-but-not-valid-C++
  construct could land unnoticed. Digests are unchanged and identical to
  the C build on every combination.
- `extern "C"` guard in `hayahash.h`. Every declaration is
  `static inline`, so this changes no symbol and fixes no link error; it
  gives the declarations C language linkage and puts the guard where it
  has to be before any future non-static build mode.
- CMake package for the C reference (`CMakeLists.txt`,
  `cmake/hayahashConfig.cmake.in`). `cmake --install` writes the same
  header and pkg-config file to the same paths as `make install`, and
  adds an exported `hayahash::hayahash` interface target so consumers
  can `find_package(hayahash 0.5)`. Version comes from the root
  `VERSION` file, so `scripts/bump-version.sh` needs no new target. CI
  stages both install paths and diffs them, then builds a consumer
  (`tests/cmake/`) that resolves the package rather than the source
  tree. Package-version compatibility is `SameMinorVersion` while
  pre-1.0 digests can change between minors.
- The JavaScript `Hasher` now runs on the wasm engine, not just the
  pure-BigInt core: `hayahash.h`'s streaming state is exported from the
  wasm module and the state round-trips through linear memory on each
  call, so the JS object stays the sole owner and the public API needs
  no `dispose()` or finalizer. Measured on Node 24 over 8 MiB, wasm
  streaming is 4.5x the pure core at 64-byte chunks and 126x at 64 KiB.
  The wasm module grows from ~3 KB to ~6 KB. `PureHasher` is exported
  for the same reason `hayahash64Pure` is.
- Streaming (`init` / `update` / `digest`) in the Rust, Go, Zig, Java,
  C#, Python, Swift and JavaScript ports. v0.5.0 moved the length into
  the finalizer specifically to make this possible, but until now only
  the C reference had it, so the streaming rows in `test_vectors/` were
  consumed by nothing. Digests are unchanged: each port produces exactly
  its own one-shot output over the concatenation of every update, for
  any split. Digesting does not consume the state. Spelled per language
  (`Digest` in Rust and Go, where it is also a `hash.Hash64`;
  `Hayahash.Hasher` in Swift, nested so it cannot collide with the
  standard library's `Hasher`; `Hasher` elsewhere) - see
  [`docs/ports.md`](docs/ports.md#streaming).

### Fixed

- Swift CI under setup-swift v3: the action verifies the Swiftly
  tarball without importing the swift.org signing key
  (swift-actions/setup-swift#798). Both Swift jobs import
  `https://www.swift.org/keys/all-keys.asc` first.
- hayasum no longer exits `0` when a digest fails to reach stdout (full)
  disk, closed descriptor): the write is checked and reported.
- hayasum rejects seeds that would silently mean something else — a
  leading `-` wrapping past 2^64-1, a leading `+`, surrounding
  whitespace, or a leading zero read as octal.
- hayasum escapes backslash, newline, and carriage return in file names
  so one input cannot produce more than one output line, and sanitizes
  argv echoed back in diagnostics.
- hayasum reads stdin in binary mode on Windows; text mode changed the
  digest of piped bytes at CRLF and stopped at Ctrl-Z.
- hayasum reports the underlying `errno` for a failed read (a directory,
  for instance) instead of a bare "read error".
- Cross-port differential conformance now runs on pull requests. Every
  port's differential test skips when `HAYAHASH_CORPUS` is unset, and
  `ci.yml` never set it, so the gate silently checked zero cases on every
  pull request and a divergence surfaced only in the next nightly run.
  Each port job now builds a 406-case corpus (the generator's
  deterministic prefix: lengths 0-384 plus the 21 fixed large edges,
  820 KiB, about a second) seeded from the commit SHA, and fails rather
  than continuing on a mismatch.
- The browser playground loads again. `scripts/build-playground.sh` copied
  a hand-listed subset of `js/dist/` into the deployed bundle, so the
  streaming `Hasher` arriving in the npm package left `vendor/index.js`
  importing a `vendor/stream.js` that never shipped. One missing module
  takes down the whole graph, and the page could only report that the
  import failed — "Importing a module script failed" in Safari, "Failed to
  fetch dynamically imported module" in Chrome — without naming the file.
  The bundle now carries every module tsc emits, and
  `scripts/version-playground.mjs` resolves every import and every
  `import.meta.url` fetch in the staged set before publishing it, so a gap
  fails the deploy instead of the page.

### Changed

- Shortened the website and introductory docs around quick starts,
  portability, streaming, and stability. Language examples are easier to
  find; detailed algorithms and benchmark evidence remain in the reference
  docs. No digest or API changes.

- Nightly differential conformance default corpus size raised from 4096
  to 32768 cases (same format and fixed 406-case length/edge prefix).
  Digests are unchanged; this only increases per-run sampling of
  random `(input, seed)` pairs across ports.
- `hayahash.h` withdraws its 13 `HAYAHASH*_INTERNAL_*` macros with
  `#undef` at the end of the header, so they no longer leak into the
  including translation unit. The `hayahash*_internal_*` functions and
  enum constants cannot be withdrawn this way and are unchanged.
- Documentation, website, and paper refactored around the algorithm.
  `docs/design.md` is now the complete algorithm - constants, dispatch,
  absorb, tail, finalizers, both widths - followed by the three
  cancellation channels and the constants that close them. The paper is
  specification-and-proof first: new lemmas for top-window invariance
  under odd multiplication, ladder return distance under the absorb
  rotation (`D(21) = 3` against `D(27) = 19`), fold-rotation resonance,
  and short-path injectivity in the seed, plus a dependency-graph cost
  model that replaces its benchmark section. The six per-pass optimization
  logs are consolidated into `docs/implementation.md` (compiled shapes,
  screened-and-rejected algorithm ideas, open questions). Superseded-digest
  evaluation records and the `v0.4.0` competitive tables are removed rather
  than carried forward; no digest, API, or test changes.

## [0.5.0] - 2026-08-09

### Changed

- **DIGEST:** Length is absorbed in the finalizer instead of the lane-IV
  premix, enabling a one-shot-identical streaming API. Every prior digest
  value changes. SMHasher3 verification values are now `0x65F2AC15`
  (hayahash64) and `0x3F0411F4` (hayahash128).
- Added portable `hayahash128` and streaming `init` / `update` / `digest`
  APIs that share state across both widths (`hayahash128.lo == hayahash64`).
- Bit-exact hayahash128 support in every maintained language port.
- hayahash128 bulk path brought to hayahash64 throughput parity on measured
  hosts.

### Migration

- Recompute any stored hashes produced with v0.4.x or earlier.
- Prefer the streaming API when input arrives in chunks; digests match
  one-shot for every split.

## [0.4.6] - 2026-08-08

### Added

- Browser playground and step-by-step simulator on the project website.
- Self-contained SMHasher3 adapter mirror for upstream-ready integration.

### Fixed

- Unlicense detection metadata for GitHub's license classifier.

### Changed

- Documentation audit and README restructuring; optimization notes moved
  under `docs/optimization/`.

## [0.4.5] - 2026-08-01

### Changed

- Release packaging and CI maintenance (lockstep with 0.4.x port manifests).

## [0.4.4] - 2026-08-01

### Fixed

- Python extension build: silence MSVC warnings from CPython headers.

## [0.4.3] - 2026-08-01

### Added

- manylinux Python wheels via cibuildwheel in the release workflow.

## [0.4.2] - 2026-08-01

### Changed

- Release packaging maintenance.

## [0.4.1] - 2026-08-01

### Added

- Bit-exact Swift port (SwiftPM).

### Changed

- Clang bulk lanes kept scalar off Zen 4/5; GCC bulk vectorization gated to
  Zen 4/5 targets.

## [0.4.0] - 2026-08-01

### Added

- Bit-exact C# / .NET and MIPS64 assembly ports.
- Nightly differential conformance fuzzing across language ports.
- SMHasher3 harness with archived host runs; s390x big-endian KAT in CI.
- Fifth optimization pass: GCC bulk vectorization and per-compiler dispatch.

### Note

- Digest values for the public one-shot API were unchanged from the late
  0.3.x series through 0.4.6; only the v0.5.0 finalizer change is a
  `DIGEST` break in this changelog.

## [0.3.0] - 2026-07-31

### Added

- Working paper and claim-by-claim audit register under `paper/`.
- MSVC x64 conformance job; additional AArch64 / x86-64 length tiers and
  bulk-path optimizations.

### Fixed

- **DIGEST (relative to 0.2.x):** Full rotation-orbit collisions broken;
  portable input handling hardened. Recompute hashes if upgrading from
  0.2.x.

## [0.2.1] - 2026-07-30

### Added

- Package badges and Dependabot coverage for npm.
- CI actions pinned to commit SHAs.

## [0.2.0] - 2026-07-30

### Added

- Tag-driven multi-registry release workflow and `scripts/bump-version.sh`.
- Registry publishing preparation for Rust, Zig, Java, and npm.

## [0.1.0] - 2026-07-30

### Added

- Initial public release: C reference header `hayahash.h` and bit-exact
  ports (Rust, Go, Zig, Java, Python, JavaScript/WebAssembly).
- Local quality harness and benchmark tooling under `tests/`.

[Unreleased]: https://github.com/thevilledev/hayahash/compare/v0.6.0...HEAD
[0.6.0]: https://github.com/thevilledev/hayahash/compare/v0.5.0...v0.6.0
[0.5.0]: https://github.com/thevilledev/hayahash/compare/v0.4.6...v0.5.0
[0.4.6]: https://github.com/thevilledev/hayahash/compare/v0.4.5...v0.4.6
[0.4.5]: https://github.com/thevilledev/hayahash/compare/v0.4.4...v0.4.5
[0.4.4]: https://github.com/thevilledev/hayahash/compare/v0.4.3...v0.4.4
[0.4.3]: https://github.com/thevilledev/hayahash/compare/v0.4.2...v0.4.3
[0.4.2]: https://github.com/thevilledev/hayahash/compare/v0.4.1...v0.4.2
[0.4.1]: https://github.com/thevilledev/hayahash/compare/v0.4.0...v0.4.1
[0.4.0]: https://github.com/thevilledev/hayahash/compare/v0.3.0...v0.4.0
[0.3.0]: https://github.com/thevilledev/hayahash/compare/v0.2.1...v0.3.0
[0.2.1]: https://github.com/thevilledev/hayahash/compare/v0.2.0...v0.2.1
[0.2.0]: https://github.com/thevilledev/hayahash/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/thevilledev/hayahash/releases/tag/v0.1.0
