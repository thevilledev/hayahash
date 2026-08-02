# hayahash

JavaScript/TypeScript package for
[hayahash64](https://github.com/thevilledev/hayahash), a small, fast,
portable 64-bit hash function that passes the full
[SMHasher3](https://gitlab.com/fwojcik/smhasher3) suite.

WebAssembly is the environment hayahash was made for. Its portability
rules - no SIMD, no 64x64-to-128-bit multiply, no per-architecture
code - are exactly the constraints wasm imposes on every hash: wasm
has a native `i64.mul` but no wide multiply, so the hashes that win
native shootouts (rapidhash, wyhash, ...) lose the very instruction
their speed comes from, while hayahash runs at full design speed.

This package therefore does not port the algorithm to JavaScript for
its fast path: it ships the untouched reference header (`hayahash.h`
at the repository root) compiled to a ~1.4 KB wasm32 module, embedded
as base64 and instantiated synchronously at import. Digests are
bit-exact with the C reference by construction. A pure-JS BigInt port
serves as the fallback engine where WebAssembly is unavailable, and as
an independent implementation the tests cross-check the wasm against.

## Usage

```js
import { hayahash64 } from "hayahash";

hayahash64("hello world");            // 17444460454596232566n
hayahash64("hello world", 42n);       // seeded
hayahash64(new Uint8Array([1, 2, 3])); // raw bytes
hayahash64("abc").toString(16);       // format as hex
```

- **Input:** `Uint8Array` (including `Buffer`), or a string, which is
  hashed as its UTF-8 encoding.
- **Seed:** optional `bigint` or integer `number`, default `0`.
  Negative values map to their two's-complement `uint64_t` pattern,
  matching a C caller.
- **Result:** the 64-bit digest as an unsigned `bigint` in
  `[0, 2^64)`. Identical on every platform, engine, and endianness.

### Exports

| export | description |
|---|---|
| `hayahash64(input, seed?)` | the hash; wasm engine when available, pure-JS otherwise |
| `hayahash64Pure(input, seed?)` | same digest, always computed by the pure-JS engine |
| `getEngine()` | `"wasm"` or `"js"`: which engine `hayahash64` currently uses |
| `setWasmModule(module)` | activate the wasm engine from a precompiled `WebAssembly.Module` |
| `HayahashInput` | `Uint8Array \| string` (type) |

## Engines

**wasm** (default): the reference C header compiled with
`zig cc --target=wasm32-freestanding -O3` (see [`wasm/build.sh`](wasm/build.sh)).
The module exports nothing but the hash, its memory, and `__heap_base`;
input bytes are copied into linear memory before each call. Inputs up
to ~32 KiB fit in the initial memory page; larger inputs grow the
instance's memory on demand (it is never shrunk). Inputs longer than
2^31 - 1 bytes exceed wasm32's `ptrdiff_t` and are routed to the pure
engine automatically.

**js** (fallback): a BigInt transliteration of the reference, used
when `WebAssembly` is missing or blocked (e.g. a Content Security
Policy without `wasm-unsafe-eval`). Bit-exact but roughly two orders
of magnitude slower - BigInt arithmetic allocates. It exists for
correctness everywhere, not speed.

Both engines are verified against the known-answer vectors shared by
all ports in this repository and reproduce SMHasher3's verification
value `0xF3C4A9B4` (see [`test/hayahash.test.mjs`](test/hayahash.test.mjs)).

## Edge runtimes (Cloudflare Workers)

Cloudflare Workers and other workerd-based runtimes forbid compiling
wasm at runtime ("Wasm code generation disallowed by embedder"), so
the embedded module cannot be used there and the package would fall
back to the slow pure-JS engine. They do allow *instantiating* a
module that was precompiled at deploy time, which is exactly what a
`.wasm` import gives you. The package ships the raw module for this:

```js
import wasmModule from "hayahash/hayahash.wasm";
import { hayahash64, setWasmModule } from "hayahash";

setWasmModule(wasmModule);
// hayahash64 now runs on the wasm engine.
```

Wrangler treats `.wasm` imports as `CompiledWasm` modules by default;
no configuration is needed. On runtimes without this restriction the
import-time initialization already handles everything and
`setWasmModule` is never required.

## Building from source

The compiled wasm (`wasm/hayahash.wasm`) and its embedded form
(`src/wasm-module.ts`) are committed, so the normal build only needs
node:

```sh
npm install
npm run build   # tsc -> dist/
npm test        # node --test against dist/
npm run bench   # wasm vs pure-JS throughput/latency
```

Rebuilding the wasm module itself (only needed if `hayahash.h`
changes) additionally requires [zig](https://ziglang.org/), used
purely as a hermetic C cross compiler:

```sh
npm run build:wasm   # zig cc + regenerate src/wasm-module.ts
```

## Status

Same as the parent repository: experimental prototype. The algorithm,
constants, and digest values may still change; do not use hayahash yet
anywhere hashes are persisted.

## License

Public domain, under the [Unlicense](LICENSE).
