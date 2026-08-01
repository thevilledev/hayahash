# Repository layout and language ports

## Layout

- `hayahash.h` - reference implementation (C99, single header, public
  domain)
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
  version in every port manifest)

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

C - copy `hayahash.h` into your project:

```c
#include "hayahash.h"

uint64_t h = hayahash64(buf, len, seed);
```

Rust - the `hayahash` crate lives in [`rust/`](../rust/):

```rust
let h = hayahash::hayahash64(buf, seed);
```

Go - the module lives in [`go/`](../go/):

```go
import hayahash "github.com/thevilledev/hayahash/go"

h := hayahash.Hash64(buf, seed)
```

Zig - the package lives in [`zig/`](../zig/):

```zig
const hayahash = @import("hayahash");

const h = hayahash.hayahash64(buf, seed);
```

Java - the Maven module lives in [`java/`](../java/):

```java
import io.github.thevilledev.hayahash.Hayahash;

long h = Hayahash.hash64(buf, seed);
```

C# / .NET - the NuGet package lives in [`csharp/`](../csharp/):

```csharp
using Hayahash;

ulong h = Hayahash.Hash64(buf, seed);
```

Python - the PyPI package lives in [`python/`](../python/); a CPython C
extension wraps `hayahash.h` directly:

```python
from hayahash import hayahash64

h = hayahash64(buf, seed)
```

Swift - the SwiftPM package lives in [`swift/`](../swift/):

```swift
import Hayahash

let h = Hayahash.hash64(buf, seed: 0)
```

JavaScript/TypeScript - the npm package lives in [`js/`](../js/); the
fast path is `hayahash.h` itself, compiled to a ~1.5 KB WebAssembly
module, with a pure-JS fallback:

```js
import { hayahash64 } from "hayahash";

const h = hayahash64(buf, seed); // unsigned 64-bit bigint
```

MIPS64 assembly - the port lives in [`mips/`](../mips/):

```c
#include "hayahash.h" /* mips/hayahash.h */

uint64_t h = hayahash64(buf, len, seed);
```
