# Language ports

Choose a language below for installation and examples. All implementations
at the same version produce the same hash for the same bytes and seed.
The 128-bit result contains `lo` and `hi` words; `lo` equals the 64-bit hash.

**Experimental:** hash values can change between releases. Pin your version
and read the [stability policy](stability.md) before storing hashes.

[C / C++](#c--c) · [Rust](#rust) · [Go](#go) · [Zig](#zig) ·
[Java](#java) · [C#](#c--net) · [Python](#python) · [Swift](#swift) ·
[JS / TS](#javascript--typescript) · [Haskell](#haskell) ·
[MIPS64](#mips64-assembly) · [Streaming](#streaming)

## Usage per language

The snippets use `buf` for input bytes and `seed` for a 64-bit seed.

### C / C++

Copy [`hayahash.h`](../hayahash.h) into your project. No dependencies.

```c
#include "hayahash.h"

uint64_t h = hayahash64(buf, len, seed);
hayahash128_t h128 = hayahash128(buf, len, seed);
```

```sh
make install PREFIX=/usr/local
cc $(pkg-config --cflags hayahash) main.c -o main
```

For CMake, install the package config and link the exported target:

```sh
cmake -S . -B build && cmake --install build --prefix /usr/local
```

```cmake
find_package(hayahash 0.5 REQUIRED)
target_link_libraries(app PRIVATE hayahash::hayahash)
```

CMake also installs a package config under `lib/cmake/hayahash`. Version
matching uses `SameMinorVersion` while pre-1.0 digests can change between
minor releases. See [stability](stability.md).

### Rust

[`hayahash`](../rust/) supports `no_std`. Install with `cargo add hayahash`.

```rust
let h = hayahash::hayahash64(buf, seed);
let h128 = hayahash::hayahash128(buf, seed);
```

### Go

Install with `go get github.com/thevilledev/hayahash/go`. [Source](../go/).

```go
import hayahash "github.com/thevilledev/hayahash/go"

h := hayahash.Hash64(buf, seed)
h128 := hayahash.Hash128(buf, seed)
```

### Zig

Requires Zig 0.16. [Package and build setup](../zig/).

```zig
const hayahash = @import("hayahash");

const h = hayahash.hayahash64(buf, seed);
const h128 = hayahash.hayahash128(buf, seed);
```

### Java

Requires Java 17+. Maven: `io.github.thevilledev:hayahash`. [Source](../java/).

```java
import io.github.thevilledev.hayahash.Hayahash;

long h = Hayahash.hash64(buf, seed);
Hayahash.Hash128 h128 = Hayahash.hash128(buf, seed);
```

### C# / .NET

Requires .NET 8+. Install with `dotnet add package Hayahash`.
[Package details](../csharp/).

```csharp
using Hayahash;

ulong h = Hayahash.Hash64(buf, seed);
Digest128 h128 = Hayahash.Hash128(buf, seed);
```

### Python

Requires CPython 3.9+. Install with `pip install hayahash`.
[C extension and wheels](../python/).

```python
from hayahash import hayahash128, hayahash64

h = hayahash64(buf, seed)
h128 = hayahash128(buf, seed)  # (lo, hi)
```

### Swift

Requires Swift 5.9+. Use the [SwiftPM package](../swift/) as a local
dependency or extract the Swift release archive.

```swift
import Hayahash

let h = Hayahash.hash64(buf, seed: 0)
let h128 = Hayahash.hash128(buf, seed: 0)
```

### JavaScript / TypeScript

Install with `npm install hayahash`. Uses WebAssembly with a pure-JS
fallback. [Package details](../js/).

```js
import { hayahash128, hayahash64 } from "hayahash";

const h = hayahash64(buf, seed); // unsigned 64-bit bigint
const h128 = hayahash128(buf, seed); // { lo, hi }
```

### Haskell

Requires GHC 8.10+. The [Cabal package](../haskell/) hashes strict
`ByteString` values.

```haskell
import Data.Hash.Hayahash

h = hayahash64 buf seed
h128 = hayahash128 buf seed
```

Use it as a local package, from the Haskell release archive, or pin the
repository and `subdir: haskell` in a `source-repository-package` stanza.

### MIPS64 assembly

Copy [`mips/hayahash.S`](../mips/hayahash.S) and its header into your build.
Uses the n64 ABI. [Build instructions](../mips/).

```c
#include "hayahash.h" /* mips/hayahash.h */

uint64_t h = hayahash64(buf, len, seed);
hayahash128_t h128 = hayahash128(buf, len, seed);
```

## Streaming

Use streaming when input arrives in chunks. Any split produces the same
digest as hashing the complete input. Taking a digest leaves the state
available for more updates.

| language | type | absorb | finish |
|---|---|---|---|
| C | `hayahash64_state` | `hayahash64_update` | `hayahash64_digest` / `hayahash128_digest` |
| Rust | `hayahash::Digest` | `update` | `finish64` / `finish128` |
| Go | `hayahash.Digest` (a `hash.Hash64`) | `Write` | `Sum64` / `Sum128` |
| Zig | `hayahash.Hasher` | `update` | `digest64` / `digest128` |
| Java | `Hasher` | `update` | `digest64` / `digest128` |
| C# | `Hasher` | `Update` | `Digest64` / `Digest128` |
| Python | `hayahash.Hasher` | `update` | `digest64` / `digest128` |
| Swift | `Hayahash.Hasher` | `update` | `digest64` / `digest128` |
| JS/TS | `Hasher` | `update` | `digest64` / `digest128` |
| Haskell | `Hasher` | `update` | `digest64` / `digest128` |

```c
hayahash64_state st;
hayahash64_init(&st, 0);
hayahash64_update(&st, "hel", 3);
hayahash64_update(&st, "lo", 2);
uint64_t h = hayahash64_digest(&st);
hayahash128_t h128 = hayahash128_digest(&st);
```

Swift's hasher is a value type: copying it forks its state. Python provides
an explicit `copy()` method. The MIPS64 assembly port has one-shot APIs only.

## Layout

| Path | Purpose |
|---|---|
| `hayahash.h` | Authoritative C reference |
| `rust/`, `go/`, `zig/`, `java/`, `csharp/`, `python/`, `swift/`, `js/`, `haskell/`, `mips/` | Language ports and their tests |
| `cli/` | `hayasum` file and stdin utility |
| `Makefile`, `CMakeLists.txt`, `hayahash.pc.in`, `VERSION` | C packaging and version |
| `test_vectors/` | Versioned known-answer digests |
| `tests/` | Quality and benchmark harnesses |
| `tests/smhasher3/` | Pinned SMHasher3 adapter and harness |
| `tests/differential/` | Cross-port conformance corpus |
| `tests/wasm/` | WebAssembly benchmarks and native-equivalence checks |
| `docs/`, `paper/` | Guides, specification, proofs, and raw results |
| `scripts/` | Build and release tooling |

See [Contributing](../CONTRIBUTING.md) for port sync rules and checks, and
[the changelog](../CHANGELOG.md) for release history.

## Related project

[`haya32x64`](https://github.com/thevilledev/haya32x64) produces a 64-bit hash
using 32-bit arithmetic. It targets 32-bit processors and JavaScript without
`BigInt`. It is a separate algorithm: switching changes hashes and requires
a data migration.
