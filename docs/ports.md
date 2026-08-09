# Repository layout and language ports

## Layout

- `hayahash.h` - reference implementation (C99, single header, public
  domain)
- `cli/` - `hayasum` file/stdin hashing utility (`make -C cli`)
- `Makefile`, `hayahash.pc.in`, `VERSION` - optional system install of the
  C header plus a `hayahash` pkg-config package (`make install`)
- `test_vectors/` - versioned known-answer digests for external
  implementers (`make -C test_vectors check`)
- `CHANGELOG.md` - release history; `DIGEST` marks digest-breaking changes
- `CONTRIBUTING.md` - how to change the reference, ports, and digests
- `rust/` - Rust port (`hayahash` crate, `no_std` compatible)
- `go/` - Go port (`github.com/thevilledev/hayahash/go` module)
- `zig/` - Zig port (`hayahash` module, Zig 0.16)
- `java/` - Java port (Maven module `io.github.thevilledev:hayahash`,
  Java 17+)
- `csharp/` - C# / .NET port (NuGet package `Hayahash`, .NET 8+)
- `python/` - Python port (PyPI package `hayahash`, CPython C
  extension over the reference header, CPython 3.9+)
- `swift/` - Swift port (SwiftPM package `Hayahash`, Swift 5.9+)
- `js/` - JavaScript/TypeScript port for npm (`hayahash` package): the
  reference header compiled to WebAssembly, plus a pure-JS fallback
- `mips/` - MIPS64 assembly port (`hayahash.S`, n64 ABI); tested under
  qemu-mips64el against the shared known-answer vectors
- `tests/` - C quality and benchmark harnesses; ChibiHash v1/v2
  reference sources are vendored there so the comparisons are
  self-contained
- `tests/smhasher3/` - SMHasher3 adapter and pinned build harness; the
  suite is cloned at test time, never vendored. See
  [`smhasher3.md`](smhasher3.md)
- `tests/differential/` - reproducible randomized corpus generator for
  nightly cross-port differential conformance against the C reference
- `tests/wasm/` - baseline-wasm32 shootout and wasm-vs-native
  bit-exactness check (zig cc + Node); run on demand in CI via the
  "Wasm bench" workflow, or locally with `make -C tests/wasm run-kat
  run-bench`. Competitor headers (rapidhash, xxHash) are fetched
  pinned to exact upstream commits at build time
- `docs/` - this documentation ([index](README.md))
- `paper/` - working paper, claim-by-claim evidence register
  ([`AUDIT.md`](../paper/AUDIT.md)), and archived evaluation records
- `scripts/` - release tooling (`bump-version.sh` sets the shared
  version in root `VERSION` and every port manifest)

Each port lives in its own top-level directory and is verified against
the reference implementation via the SMHasher3 verification value and
the shared known-answer vectors (see `rust/tests/kat.rs`,
`go/kat_test.go`, `zig/tests/kat.zig`, the Java `KatTest` under
`java/src/test`, the C# `KatTests` under `csharp/tests`,
`python/tests`, `swift/Tests`, `js/test/hayahash.test.mjs`, and
`make -C mips test`).
All ports share one version number, so a given version denotes the
same algorithm everywhere.

## Usage per language

C - copy `hayahash.h` into your project, or install it for pkg-config
consumers:

```c
#include "hayahash.h"

uint64_t h = hayahash64(buf, len, seed);
hayahash128_t h128 = hayahash128(buf, len, seed);
```

```sh
make install PREFIX=/usr/local
cc $(pkg-config --cflags hayahash) main.c -o main
```

`make check-install` stages into a temporary DESTDIR and verifies the
`.pc` file resolves. Embedding by copying the header remains fully
supported; the install target is for distro and system packaging.

Every language port exposes both widths. Each 128-bit result has `lo`
and `hi` words in that order, and `lo` is exactly hayahash64 for the
same input and seed. In C, the streaming `hayahash128_state`, init, and
update names are zero-cost aliases for the shared hayahash64 state.

Rust - the `hayahash` crate lives in [`rust/`](../rust/):

```rust
let h = hayahash::hayahash64(buf, seed);
let h128 = hayahash::hayahash128(buf, seed);
```

Go - the module lives in [`go/`](../go/):

```go
import hayahash "github.com/thevilledev/hayahash/go"

h := hayahash.Hash64(buf, seed)
h128 := hayahash.Hash128(buf, seed)
```

Zig - the package lives in [`zig/`](../zig/):

```zig
const hayahash = @import("hayahash");

const h = hayahash.hayahash64(buf, seed);
const h128 = hayahash.hayahash128(buf, seed);
```

Java - the Maven module lives in [`java/`](../java/):

```java
import io.github.thevilledev.hayahash.Hayahash;

long h = Hayahash.hash64(buf, seed);
Hayahash.Hash128 h128 = Hayahash.hash128(buf, seed);
```

C# / .NET - the NuGet package lives in [`csharp/`](../csharp/):

```csharp
using Hayahash;

ulong h = Hayahash.Hash64(buf, seed);
Digest128 h128 = Hayahash.Hash128(buf, seed);
```

Python - the PyPI package lives in [`python/`](../python/); a CPython C
extension wraps `hayahash.h` directly:

```python
from hayahash import hayahash128, hayahash64

h = hayahash64(buf, seed)
h128 = hayahash128(buf, seed)  # (lo, hi)
```

Swift - the SwiftPM package lives in [`swift/`](../swift/):

```swift
import Hayahash

let h = Hayahash.hash64(buf, seed: 0)
let h128 = Hayahash.hash128(buf, seed: 0)
```

JavaScript/TypeScript - the npm package lives in [`js/`](../js/); the
fast path is `hayahash.h` itself, compiled to a ~3 KB WebAssembly
module, with a pure-JS fallback:

```js
import { hayahash128, hayahash64 } from "hayahash";

const h = hayahash64(buf, seed); // unsigned 64-bit bigint
const h128 = hayahash128(buf, seed); // { lo, hi }
```

MIPS64 assembly - the port lives in [`mips/`](../mips/):

```c
#include "hayahash.h" /* mips/hayahash.h */

uint64_t h = hayahash64(buf, len, seed);
hayahash128_t h128 = hayahash128(buf, len, seed);
```

## Related project

[`haya32x64`](https://github.com/thevilledev/haya32x64) provides a 64-bit
digest using strictly 32-bit state and arithmetic while retaining the full
result of each 32x32 multiply. It targets 32-bit processors, pure JavaScript
without `BigInt`, CSP-constrained runtimes, and similar environments; its
JavaScript package is
[`haya32x64` on npm](https://www.npmjs.com/package/haya32x64).

hayahash and haya32x64 are different algorithms, not interchangeable
backends. Switching between them changes persisted hashes and must be treated
as a data migration. They have separate reference implementations,
known-answer vectors, compatibility guarantees, versions, and releases.
