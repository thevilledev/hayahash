# hayahash

[![CI](https://img.shields.io/github/actions/workflow/status/thevilledev/hayahash/ci.yml?branch=main&logo=githubactions&logoColor=white&label=CI)](https://github.com/thevilledev/hayahash/actions/workflows/ci.yml)
[![license](https://img.shields.io/github/license/thevilledev/hayahash?logo=unlicense&logoColor=white&label=license)](LICENSE)

hayahash is a family of fast, non-cryptographic 64- and 128-bit hash
functions for platforms with ordinary 64-bit scalar arithmetic. It passes
the full [SMHasher3](https://gitlab.com/fwojcik/smhasher3) suite without
requiring SIMD, AES instructions, or a 64x64-to-128-bit multiply.

The reference implementation is the single C header
[`hayahash.h`](hayahash.h). Bit-exact ports are available for Rust, Go, Zig,
Java, C#, Python, Swift, JavaScript/TypeScript, and MIPS64 assembly. Every
128-bit API returns ordered `lo` and `hi` words, with
`hayahash128.lo == hayahash64` for the same input and seed.

> **Experimental:** the algorithm, constants, and digest values may still
> change. Do not use hayahash yet where hashes must remain stable across
> versions. It is not a cryptographic hash or message authentication code.
> See [`docs/stability.md`](docs/stability.md) for the 1.0 freeze criteria
> and [`SECURITY.md`](SECURITY.md) for the threat model.

*Haya* (速) is Japanese for "fast."

## Why hayahash

- **Portable scalar core:** ordinary 64-bit operations, endian-independent
  output, no undefined behavior, and no architecture-specific intrinsics.
- **Two output widths:** hayahash64 and hayahash128 share the same state walk;
  selecting 128 bits adds a second finalization path rather than a second pass
  over the input.
- **Streaming:** one-shot and incremental APIs produce identical digests for
  every split of the same input.
- **Broad language support:** every maintained port exposes both widths and is
  checked against the C reference.
- **Quality-gated development:** both widths pass all 188 applicable SMHasher3
  test groups, with additional structured-collision and differential tests.

The design is aimed at wasm, JVM, .NET, portable C, and other environments
where wide multiplication or hardware acceleration cannot be assumed. It is
not a universal speed claim: hashes that target native wide multiply, SIMD,
or AES instructions can be faster on hardware that provides them. See the
[benchmark record](docs/benchmarks.md) for competitive results and methodology.

## Performance

Representative measurements of the public 64- and 128-bit APIs:

| host / compiler | 8 B chained, 64 / 128 (ns) | 1 MiB, 64 / 128 (GB/s) |
|---|---:|---:|
| Apple M1 Pro / Apple clang 21 | 7.88 / 9.25 | 30.73 / 30.76 |
| Ryzen AI 9 HX PRO 370 / GCC 16 | 4.29 / 4.90 | 61.47 / 61.31 |
| EPYC 9655 KVM guest / GCC 13 | 4.92 / 5.66 | 54.15 / 53.99 |
| wasm32 on M1 Pro / Zig 0.16 | 7.7 / 10.6 | 23.55 / 23.34 |

The M1 and Ryzen native runs are bare metal. The EPYC guest has no frequency
control, so its within-host ratio is more meaningful than its absolute rate.
The 128-bit dispatch change recorded in the
[optimization log](docs/optimization/) brought 128-bit bulk to parity with
the 64-bit rate on all three machines. The wasm build uses no SIMD or wide
multiply. These are point measurements, not a claim that one hash is fastest
on every workload or machine.

Detailed size sweeps, ChibiHash comparisons, SMHasher3 shootouts, caveats, and
reproduction notes are in [`docs/benchmarks.md`](docs/benchmarks.md).

## Usage

C - copy [`hayahash.h`](hayahash.h) into your project, or install the
header and pkg-config file with `make install`:

```c
#include "hayahash.h"

uint64_t h = hayahash64(buf, len, seed);
hayahash128_t h128 = hayahash128(buf, len, seed);
// h128.lo == h
```

```sh
make install PREFIX=/usr/local
cc $(pkg-config --cflags hayahash) main.c -o main
```

Streaming uses one shared state for both output widths. Calling a digest
function does not modify the state:

```c
hayahash64_state st;
hayahash64_init(&st, seed);
hayahash64_update(&st, part1, n1);
hayahash64_update(&st, part2, n2);
uint64_t h = hayahash64_digest(&st);
hayahash128_t h128 = hayahash128_digest(&st);
```

| language | package | 64-bit call | 128-bit call |
|---|---|---|---|
| [Rust](rust/) | [`hayahash`](https://crates.io/crates/hayahash) (`no_std`) | `hayahash::hayahash64(buf, seed)` | `hayahash::hayahash128(buf, seed)` |
| [Go](go/) | [`github.com/thevilledev/hayahash/go`](https://pkg.go.dev/github.com/thevilledev/hayahash/go) | `hayahash.Hash64(buf, seed)` | `hayahash.Hash128(buf, seed)` |
| [Zig](zig/) | `hayahash` module (Zig 0.16) | `hayahash.hayahash64(buf, seed)` | `hayahash.hayahash128(buf, seed)` |
| [Java](java/) | [`io.github.thevilledev:hayahash`](https://central.sonatype.com/artifact/io.github.thevilledev/hayahash) (17+) | `Hayahash.hash64(buf, seed)` | `Hayahash.hash128(buf, seed)` |
| [C#](csharp/) | [`Hayahash`](https://www.nuget.org/packages/Hayahash) (.NET 8+) | `Hayahash.Hash64(buf, seed)` | `Hayahash.Hash128(buf, seed)` |
| [Python](python/) | [`hayahash`](https://pypi.org/project/hayahash/) (3.9+) | `hayahash64(buf, seed)` | `hayahash128(buf, seed)` |
| [Swift](swift/) | `Hayahash` SwiftPM package (5.9+) | `Hayahash.hash64(buf, seed: 0)` | `Hayahash.hash128(buf, seed: 0)` |
| [JS/TS](js/) | [`hayahash`](https://www.npmjs.com/package/hayahash) (wasm + pure JS) | `hayahash64(buf, seed)` | `hayahash128(buf, seed)` |
| [MIPS64](mips/) | `hayahash.S` (n64 ABI) | `hayahash64(buf, len, seed)` | `hayahash128(buf, len, seed)` |

Installation details, complete examples, and the repository layout are in
[`docs/ports.md`](docs/ports.md).

## Quality

hayahash64 and hayahash128 each pass all 188 applicable SMHasher3 test
groups. CI also checks structured collision sets, one-shot/streaming equality,
cross-language differential conformance, big-endian output, wasm32, MSVC x64,
and the MIPS64 n64 ABI. The exact tests, verification values, and limitations
are documented in [`docs/quality.md`](docs/quality.md).

## Documentation

- [Contributing](CONTRIBUTING.md) - port sync rules, local gates, SMHasher3 triggers
- [Changelog](CHANGELOG.md) - release history; `DIGEST` marks digest-breaking changes
- [Test vectors](test_vectors/) - versioned known-answer digests for external implementers
- [Stability / 1.0 criteria](docs/stability.md) - when digests freeze
- [Security policy](SECURITY.md) - threat model and private reporting
- [Benchmarks](docs/benchmarks.md) - comparative results and methodology
- [Design](docs/design.md) - the algorithm and structural decisions
- [Quality](docs/quality.md) - test coverage and conformance evidence
- [Ports](docs/ports.md) - installation, language examples, and layout
- [SMHasher3](docs/smhasher3.md) - reproducing the suite and speed shootouts
- [Website deployment](docs/deployment.md) - Pages and Cloudflare cache setup
- [Optimization log](docs/optimization/) - measured changes and rejected ideas

## License

Public domain under the [Unlicense](LICENSE). The separately licensed
SMHasher3 test harness is described in
[`docs/smhasher3.md`](docs/smhasher3.md#licensing).
