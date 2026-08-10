# Changelog

All notable changes to hayahash are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project uses [Semantic Versioning](https://semver.org/).

**Digest changes** (rows marked `DIGEST`) alter every output value for the same
input and seed. Do not mix digests across releases when hashes are persisted.
While hayahash is pre-1.0 and marked experimental, digests may still change;
see the experimental notice in the README and
[`docs/design.md`](docs/design.md#digest-stability).

Known-answer vectors for the current digest live under
[`test_vectors/`](test_vectors/).

## [Unreleased]

### Added

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
  One minute per target on pull requests; longer nightly runs in
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

- Streaming (`init` / `update` / `digest`) in the Rust, Go, Zig, Java,
  C#, Python and JavaScript ports. v0.5.0 moved the length into the
  finalizer specifically to make this possible, but until now only the C
  reference had it, so the streaming rows in `test_vectors/` were
  consumed by nothing. Digests are unchanged: each port produces exactly
  its own one-shot output over the concatenation of every update, for
  any split. Digesting does not consume the state. Spelled per language
  (`Digest` in Rust and Go, where it is also a `hash.Hash64`; `Hasher`
  elsewhere) - see [`docs/ports.md`](docs/ports.md#streaming). Swift
  remains one-shot only. The JavaScript `Hasher` runs on the pure-BigInt
  core, since the wasm module exports only the one-shot entry points.

### Fixed

- hayasum no longer exits `0` when a digest fails to reach stdout (full
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

### Changed

- Nightly differential conformance default corpus size raised from 4096
  to 32768 cases (same format and fixed 406-case length/edge prefix).
  Digests are unchanged; this only increases per-run sampling of
  random `(input, seed)` pairs across ports.
- `hayahash.h` withdraws its 13 `HAYAHASH*_INTERNAL_*` macros with
  `#undef` at the end of the header, so they no longer leak into the
  including translation unit. The `hayahash*_internal_*` functions and
  enum constants cannot be withdrawn this way and are unchanged.

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
  hosts (sixth optimization pass).

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

[Unreleased]: https://github.com/thevilledev/hayahash/compare/v0.5.0...HEAD
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
