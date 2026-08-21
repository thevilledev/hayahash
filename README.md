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

## How it works

The primitive set is exactly `+`, `^`, `<<`, `>>`, `rotl`, and
`* mod 2^64` on 64-bit words. Four decisions follow from that:

1. **Eight lanes, nothing but `xor -> mul` carried.** The bulk loop absorbs
   64-byte blocks across eight independent lanes with no cross-lane work on
   the loop-carried path, so the loop is bound by multiplier issue rather
   than by dependency latency.
2. **A chained, injective absorb.** Each lane absorbs
   `t = w + rotl(w_prev, 27)`. The stripe-to-absorbed map is a bijection, so
   at the first stripe where two inputs differ their absorbed values differ.
   The rotated copy also gives every stripe a second life at a low position
   of the next absorb, under addition rather than XOR, so cancelling both
   copies needs a carry pattern rather than an algebraic identity.
3. **Seed-derived lane constants; length in the finalizer.** All eight lane
   IVs come from one premixed seed word plus shifted copies of the single
   multiplier, so no large per-lane literal is materialized. The length is
   absorbed in the finalizer, which makes the state a pure function of
   `(seed, bytes so far)` — and that is what lets the streaming API
   reproduce one-shot digests exactly.
4. **Overlapping tail reads, two-multiply short path.** Tails read whole
   words from the end of the input, wyhash-style, so no length uses a
   byte-at-a-time loop. Inputs of at most 16 bytes take a dedicated path
   whose 128-bit output is injective in both the message and the seed.

Removing the wide product is not only a throughput cost: it opens specific
algebraic channels, because multiplication mod 2^64 is then the only
nonlinear operation and is the identity on a four-element subgroup at the
top of the word. [`docs/design.md`](docs/design.md) states the complete
algorithm and the constants that close those channels; the
[working paper](paper/) proves the structural properties.

This targets wasm, the JVM, .NET, portable C, and anywhere else wide
multiplication or hardware acceleration cannot be assumed. It is not a
universal speed claim: hashes built on a native wide multiply, SIMD, or AES
are faster on hardware that provides them.

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
128-bit bulk is at parity with 64-bit on all three machines. The wasm build
uses no SIMD or wide multiply. These are point measurements, not a claim that
one hash is fastest on every workload or machine.

Size sweeps, the ChibiHash comparison, the 128-bit SMHasher3 shootout,
caveats, and reproduction notes are in
[`docs/benchmarks.md`](docs/benchmarks.md).

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

CMake installs the same header and pkg-config file to the same paths,
and adds a package config for `find_package`:

```sh
cmake -S . -B build && cmake --install build --prefix /usr/local
```

```cmake
find_package(hayahash 0.5 REQUIRED)
target_link_libraries(app PRIVATE hayahash::hayahash)
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

The table lists the one-shot entry points. Every port also provides the
incremental API shown above, spelled the way that language spells it -
`Digest` in Go (a `hash.Hash64`) and Rust, `Hayahash.Hasher` in Swift,
`Hasher` elsewhere. See [`docs/ports.md`](docs/ports.md#streaming).

Installation details, complete examples, and the repository layout are in
[`docs/ports.md`](docs/ports.md).

## Quality

hayahash64 and hayahash128 each pass all 188 applicable SMHasher3 test
groups, with canonical verification values `0x65F2AC15` and `0x3F0411F4`.
CI also checks structured collision sets, one-shot/streaming equality,
cross-language differential conformance, big-endian output, wasm32, MSVC x64,
and the MIPS64 n64 ABI. Every compiled dispatch shape must produce identical
output. The exact tests, verification values, and limitations are documented
in [`docs/quality.md`](docs/quality.md).

## Tools

[`cli/hayasum`](cli/) hashes files or stdin with the C reference:

```sh
make -C cli
./cli/hayasum -b 128 README.md
```

Its own tests and fuzz targets live under [`cli/`](cli/):
`make -C cli check` runs the functional harness and replays the fuzz
corpus, `make -C cli fuzz-run` fuzzes argv parsing, the reader, and the
output escaper.

## Documentation

- [Design](docs/design.md) - the complete algorithm: constants, dispatch,
  absorb, tail, finalizers, and the cancellation channels the constants close
- [Paper](paper/) - exact specification, proofs of the structural properties,
  and the claim-by-claim evidence register
- [Implementation](docs/implementation.md) - how the header compiles per
  target, and the digest-changing ideas that were screened and rejected
- [Quality](docs/quality.md) - test coverage and conformance evidence
- [Benchmarks](docs/benchmarks.md) - measurements and methodology
- [SMHasher3](docs/smhasher3.md) - reproducing the suite and speed shootouts
- [Test vectors](test_vectors/) - versioned known-answer digests for external
  implementers
- [Ports](docs/ports.md) - installation, language examples, and layout
- [Stability / 1.0 criteria](docs/stability.md) - when digests freeze
- [Security policy](SECURITY.md) - threat model and private reporting
- [Contributing](CONTRIBUTING.md) - port sync rules, local gates, SMHasher3 triggers
- [Changelog](CHANGELOG.md) - release history; `DIGEST` marks digest-breaking changes
- [Roadmap](docs/roadmap.md) - gaps against established hash repositories
  and the order for closing them
- [Website deployment](docs/deployment.md) - Pages and Cloudflare cache setup

## License

Public domain under the [Unlicense](LICENSE). The separately licensed
SMHasher3 test harness is described in
[`docs/smhasher3.md`](docs/smhasher3.md#licensing).
