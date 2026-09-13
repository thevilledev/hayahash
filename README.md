# hayahash

[![CI](https://img.shields.io/github/actions/workflow/status/thevilledev/hayahash/ci.yml?branch=main&logo=githubactions&logoColor=white&label=CI)](https://github.com/thevilledev/hayahash/actions/workflows/ci.yml)
[![license](https://img.shields.io/github/license/thevilledev/hayahash?logo=unlicense&logoColor=white&label=license)](LICENSE)

Fast, portable **64- and 128-bit hashing** for hash tables, caches, and
checksums. One C header, with matching results across ten language ports.

- **Portable:** ordinary 64-bit arithmetic; no SIMD, AES, or wide multiply
  required.
- **Consistent:** the same bytes and seed produce the same hash across
  languages and byte orders.
- **Streaming:** hash all at once or in chunks and get the same result.

> **Experimental and non-cryptographic.** Hash values may change between
> releases. Avoid uses that need stable hashes across versions. Do not use
> hayahash for security. See the [stability policy](docs/stability.md) and
> [security policy](SECURITY.md).

[Website](https://hayaha.sh/) ·
[Try it in your browser](https://hayaha.sh/playground.html#try) ·
[Documentation](docs/README.md)

## Usage

Copy [`hayahash.h`](hayahash.h) into your C or C++ project. No dependencies.

```c
#include "hayahash.h"

uint64_t h = hayahash64("hello", 5, 0);  // seed = 0
hayahash128_t h128 = hayahash128("hello", 5, 0);
// h128.lo == h; h128.hi is the second word
```

For input that arrives in chunks, use one state for both widths. Taking a
digest leaves the state available for more updates.

```c
hayahash64_state st;
hayahash64_init(&st, 0);
hayahash64_update(&st, "hel", 3);
hayahash64_update(&st, "lo", 2);
uint64_t h = hayahash64_digest(&st);  // same as the one-shot call
hayahash128_t h128 = hayahash128_digest(&st);
```

[Installation and language examples](docs/ports.md):
[C / C++](docs/ports.md#c--c), [Rust](docs/ports.md#rust),
[Go](docs/ports.md#go), [Zig](docs/ports.md#zig), [Java](docs/ports.md#java),
[C#](docs/ports.md#c--net), [Python](docs/ports.md#python),
[Swift](docs/ports.md#swift), [JS / TS](docs/ports.md#javascript--typescript),
[Haskell](docs/ports.md#haskell), and [MIPS64](docs/ports.md#mips64-assembly).
C also supports [pkg-config and CMake](docs/ports.md#c--c).

## Tools

[`hayasum`](cli/) hashes files or stdin:

```sh
make -C cli
./cli/hayasum -b 128 README.md
```

## Performance

hayahash is designed for environments such as WebAssembly where hardware
acceleration cannot be assumed. Native hashes using wide multiplication,
SIMD, or AES can be faster; measure your workload.

| host / compiler | 1 MiB, 64-bit (GB/s) | 1 MiB, 128-bit (GB/s) |
|---|---:|---:|
| Apple M1 Pro / Apple clang 21 | 30.73 | 30.76 |
| Ryzen AI 9 HX PRO 370 / GCC 16 | 61.47 | 61.31 |
| wasm32 on M1 Pro / Zig 0.16 | 23.55 | 23.34 |

Both widths have similar bulk throughput in these runs. Ryzen bulk uses
compiler auto-vectorization; the wasm build uses no SIMD.
[Comparisons, small-input timings, and methodology](docs/benchmarks.md).

## Quality

Both widths pass all **188 default tests** of the pinned SMHasher3 suite.
CI checks ports against the C reference, including streaming, byte order,
and platform compatibility. Passing these tests does not make a hash
cryptographically secure. [Test coverage and limitations](docs/quality.md).

## How it works

Short inputs use a dedicated path. Larger inputs are mixed across parallel
lanes, then combined into a digest. Both widths share the same pass over the
input; the 128-bit result adds a second output word.

Read the [algorithm](docs/design.md), explore the
[simulator](https://hayaha.sh/design.html#simulator), or see the
[full documentation index](docs/README.md) for proofs, implementation notes,
and contributor guides.

## License

Public domain under the [Unlicense](LICENSE). The separate SMHasher3 test
adapter is [GPL-3.0-or-later](docs/smhasher3.md#licensing) and is not included
in release artifacts.
