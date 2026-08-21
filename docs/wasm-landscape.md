# WASM hash landscape

A snapshot of which hash functions actually run in WebAssembly,
how the popular packages implement them, and where a
hyperoptimized follow-on project would have something to do.
Numbers are npm last-week downloads on 21 August 2026 unless
noted. They measure JS-ecosystem demand, not "how often the
algorithm runs as wasm": most of the largest packages are still
pure JavaScript.

hayahash's own wasm story is in [`js/README.md`](../js/README.md)
and the baseline-wasm32 shootout in [`benchmarks.md`](benchmarks.md).
This note is about the field around it.

## What "used in WASM" actually means

Three different markets get conflated.

1. **JS libraries that happen to use wasm for the inner loop.**
   The user writes JavaScript; a ~3–10 KB module does the arithmetic.
   This is `xxhash-wasm`, `hash-wasm`, and hayahash's npm package.
2. **Code compiled to wasm that hashes as part of a larger program.**
   A Rust/C/Go/Zig app targeting `wasm32` brings whatever hasher
   that language uses (`rustc-hash`, `ahash`, `twox-hash`, xxHash
   via C). There is no npm number for this; it is every wasm
   `HashMap` and every wasm build cache.
3. **Host APIs that are not wasm at all.** Browser
   `crypto.subtle.digest("SHA-256")` and Node `crypto` are native
   code. They win one-shot SHA-256 in the browser and do not
   expose an incremental API, which is why wasm SHA-256 still
   exists.

A fourth, quieter consumer is **tooling that embeds its own
wasm hash**: webpack ships an AssemblyScript XXH64, and that
module runs on every production webpack build.

## Demand, ranked

| package | impl | weekly downloads | algorithm |
|---|---|---:|---|
| `imurmurhash` | pure JS | 130,617,909 | MurmurHash3 (incremental, 32-bit) |
| `@noble/hashes` | pure JS (audited) | 62,207,329 | SHA-2/3, BLAKE, MD5, Argon2, … |
| `crc-32` | pure JS (table) | 38,032,142 | CRC-32 / CRC-32C |
| `sha.js` | pure JS | 17,827,031 | SHA-1 / SHA-2 |
| `crypto-js` | pure JS | 16,032,181 | MD5, SHA, HMAC, … |
| `md5` | pure JS | 14,359,266 | MD5 |
| `create-hash` | Node/`crypto` wrapper | 11,257,390 | SHA / MD5 |
| `spark-md5` | pure JS | 8,998,626 | incremental MD5 |
| **`xxhash-wasm`** | **hand-written WAT** | **6,594,666** | XXH32, XXH64 |
| `hash-sum` | pure JS | 4,130,179 | string/object hash |
| `js-sha256` | pure JS | 4,058,108 | SHA-256 |
| `xxhashjs` | pure JS (`cuint`) | 2,067,845 | XXH32, XXH64 |
| `murmurhash` | pure JS | 1,485,816 | MurmurHash |
| **`hash-wasm`** | **clang wasm32 from C** | **1,070,591** | kitchen sink, see below |
| `murmurhash3js` | pure JS | 663,487 | MurmurHash3 |
| `farmhash` | native addon | 214,459 | FarmHash |
| `blake3` (npm) | mixed | 12,992 | BLAKE3 |

The only dedicated wasm hash packages with real volume are
`xxhash-wasm` (~6.6 M) and `hash-wasm` (~1.1 M). Everything
above them is JS or a native Node addon. That gap is the
opportunity: the algorithms people already depend on are
still running in JS, or in wasm that was compiled rather
than written.

## The hashes that matter

### XXH64 / XXH32

The default non-cryptographic hash of the JS and frontend
tooling world.

**`xxhash-wasm`** ([jungomi/xxhash-wasm](https://github.com/jungomi/xxhash-wasm))
is a ~3 KB hand-written WAT module (`src/xxhash.wat`) plus a
JS loader that embeds the binary. It exports one-shot
`xxh32` / `xxh64` and streaming state structs laid out at
fixed memory offsets. It requires `i64`, bulk memory, and
`TextEncoder.encodeInto`. There is no XXH3 and no SIMD.
Instantiation is async (~1–2 ms) and then free.

Dependents sit around 250 direct / 1,700 transitive. The
package is the off-the-shelf answer for "hash this in the
browser or on a Worker without Node crypto."

**webpack** does not use that package. It vendors its own
AssemblyScript XXH64
([`assembly/hash/xxhash64.asm.ts`](https://github.com/webpack/webpack/blob/main/assembly/hash/xxhash64.asm.ts)),
ported from Stephan Brumme's `xxhash64.h` via hash-wasm's C
and then specialized: seed is always 0, `update` is only
called with a multiple of 32 bytes, `final` eats the
0–31-byte tail. That is the `[contenthash]` / `[hash]`
default. webpack 6 also uses XXH64 for
`cache.hashAlgorithm`.

**Turborepo** hashes cache keys with XXH64 seed 0 over
canonical Cap'n Proto bytes (`turborepo-hash`). That path
is native Rust, not wasm, but it is the same algorithm and
the same "fixed seed, fingerprint, hex" contract.

**`hash-wasm`** ships XXH32/64/3/128 as separate clang
wasm32 modules (4–8 KB gzipped). Same algorithm family,
compiled C rather than WAT.

hayahash's wasm32 shootout already has the speed picture
on baseline wasm (no SIMD, no wide mul), Zig 0.16 / Node
on an M1 Pro:

| hash | 8 B chained | 1 MiB |
|---|---:|---:|
| hayahash64 | 7.7 ns | 23.55 GB/s |
| XXH64 | 5.9 ns | 14.71 GB/s |
| XXH3-64 | 8.2 ns | 17.41 GB/s |

XXH64 still wins tiny keys. hayahash64 wins bulk. XXH3
on this target is not the native blowout it is on x86
SIMD, because its peak path wants a wide multiply.

### SHA-256 (and SHA-2 generally)

The default integrity hash. Content-addressable stores
(pnpm, npm, git-adjacent tooling), SRI, file checksums,
and every "hash this upload in the browser" demo.

Three implementations, in descending "is this wasm":

- **WebCrypto / Node `crypto`.** Native, one-shot only.
  `subtle.digest` will not take a chunked stream. That
  single API hole is why wasm SHA-256 has a job.
- **`@noble/hashes`.** Audited pure JS, ~2.7 KB gzipped
  for SHA-256, 62 M weekly downloads. The quality JS
  default. Slow next to wasm on bulk; fine on small
  keys; no wasm tax.
- **`hash-wasm` `sha256.wasm`.** clang `--target=wasm32
  -O3 -flto -nostdlib`, 7 KB gzipped, streaming
  `init` / `update` / `digest`. This is what people
  reach for when they need to hash a multi-hundred-MB
  file in a Worker without holding it all in RAM.

hash-wasm's Makefile comments out `-msimd128`. The
published SHA-256 module is scalar. Hand-written WAT
SHA-256 with `i8x16.swizzle` endian swap and a
degree-4 message schedule exists
([ChrisWhealy/wasm_sha256](https://github.com/ChrisWhealy/wasm_sha256),
~3.2 KB optimized) but is not an npm library anyone
depends on.

### MD5

Still everywhere for file identity (SparkMD5 +
FileReader chunking is the canonical browser pattern)
and for cache keys that predate webpack's XXH64 switch.
`spark-md5` is 9 M/week of carefully written JS, not
wasm. `hash-wasm`'s MD5 module is 4 KB and several
times faster on bulk. The remaining demand is
"incremental, tiny, no async init."

### CRC-32 / CRC-32C

`crc-32` (SheetJS) is 38 M/week of a JS table
implementation. ZIP, PNG, and protocol checksums.
`hash-wasm` has 3 KB wasm CRC32/CRC32C. Neither uses
SIMD or a carry-less multiply; wasm has no `pclmul`
equivalent yet. A 1–2 KB WAT CRC32 that beats the JS
table on anything past a few dozen bytes is a small,
closed problem.

### MurmurHash3

`imurmurhash` is 130 M/week because npm's own
object-hashing / integrity stack pulled it in years
ago. It is an incremental 32-bit Murmur in JS, used
on short keys. `murmurhash-wasm` exists (AssemblyScript,
~6 K/week) and loses on the workload that matters:
the JS↔wasm copy plus instance overhead dominates a
32-bit mix of a few words. This is a poor
hyperoptimization target unless the caller already
lives in wasm memory.

### BLAKE3

The hash a new project would pick if XXH were not
already the default. Parallel, tree-structured, 256-bit,
excellent on SIMD. On wasm the implementations are
fragmented and low-volume:

- `hash-wasm` BLAKE3: 9 KB, compiled C, no SIMD.
- `@fuzdev/blake3_wasm`: Rust `blake3` crate via
  wasm-pack, SIMD and no-SIMD builds (45 KB / 32 KB).
- `blake3-wasm-rs`: same idea, streaming `Hasher`.
- `as-blake`: AssemblyScript with a degree-4
  `v128` path above 4 KiB and a SWAR fallback.

None of these has `xxhash-wasm`-scale adoption. The
algorithm is the right long-term integrity hash for
wasm-native apps; the packaging is not.

### The portable scalar class (wyhash, rapidhash, ChibiHash, hayahash)

These show up in C/Rust more than on npm. On baseline
wasm32 they are defined by one constraint: **wasm has
`i64.mul` and no 64×64→128 multiply.** rapidhash and
wyhash are built around that wide product and fall
off a cliff when it is emulated (rapidhash v3 at
6.43 GB/s vs hayahash64 at 23.55 GB/s in the in-tree
shootout). ChibiHash is the same primitive class as
hayahash and loses on dependency placement.

This class is hayahash's existing claim. It is not an
untouched market; it is a market hayahash already
occupies, with XXH64 still owning the "tiny key,
already-the-default" slot.

### Rust-on-wasm in-process hashers

Any Rust program compiled to `wasm32-unknown-unknown`
gets `std::collections::HashMap` with a
**non-randomized** hasher (the rustc book is explicit).
Performance-conscious crates switch to:

- **`rustc-hash` / FxHasher** — polynomial + a
  wyhash-inspired slice compress. Fast on integers.
  No AES, so it does not fall over on wasm the way
  `ahash`'s AES path does.
- **`ahash`** — AES-based when the host has AES-NI;
  on wasm it cannot, and `getrandom` needs a `wasm_js`
  feature or the build breaks.
- **`twox-hash`** — XXH32/64/3. SIMD XXH3 wants
  `std` and does not map onto wasm SIMD automatically.

A wasm-aware `BuildHasher` that is bit-stable, seedable,
and does not pull `getrandom` is still a missing crate.
hayahash's Rust port is the obvious candidate once
streaming is wired into `HayaHasher` (see
[`roadmap.md`](roadmap.md) fix-now item 2).

## How the popular wasm hashes are built

Four implementation styles, in the order a new project
would actually choose among them.

### Hand-written WAT

`xxhash-wasm` is the existence proof. A 486-line
`xxhash.wat` with explicit primes, state overlays, and
no compiler. The binary is 3,105 bytes. The JS side
copies into exported `mem` (or `encodeInto`s a string
straight into it) and calls an exported function.

This is the ceiling for size and for "I know every
instruction." It is also the most expensive to write
and the easiest to get a tail path wrong. Nobody has
done this for XXH3, SHA-256-as-a-library, CRC32, or
hayahash.

### clang `--target=wasm32` from freestanding C

`hash-wasm` and hayahash's npm package. hash-wasm's
recipe is the more aggressive one:

```
clang -flto -O3 -nostdlib -fno-builtin -freestanding \
      -mexec-model=reactor --target=wasm32 \
      -Wl,--strip-all -Wl,--no-entry \
      -Wl,--compress-relocations -Wl,--export-dynamic
```

One `.c` file per algorithm, no libc, 128 KB of
linear memory, modules embedded as base64 so bundlers
do not have to learn `.wasm`. SIMD is available in
the Makefile and turned off. hayahash uses
`zig cc --target=wasm32-freestanding -O3` and a ~6 KB
module that also exports the streaming state.

This style wins "ship the reference," loses "I have
counted the loads." The compiler will not emit the
`i8x16.swizzle` endian trick or a degree-4 SHA
schedule unless the C is written to force it, and
hash-wasm's C is not.

### AssemblyScript

webpack's XXH64 and `murmurhash-wasm`. Typed TS that
lowers to wasm. Good enough for a specialized inner
loop (webpack's "update is a multiple of 32" contract
is the kind of thing AS makes easy). Worse than WAT
or tuned C for a general-purpose library, and the
toolchain is a moving target.

### Rust / wasm-bindgen

BLAKE3 ports. Correct, large (tens of KB), and
honest about SIMD vs no-SIMD as two packages. The
`Hasher.free()` / `using` story is the tax. Fine for
a 256-bit integrity hash; wrong for a 3 KB XXH64
replacement.

## What is left on the table

The field is not "no fast wasm hashes." It is "the
fast wasm hashes are either XXH64-in-WAT or
everything-in-compiled-C, and SIMD is almost unused."

Concrete gaps, in the order they look worth a new
repo rather than a hayahash commit.

### 1. Hand-written WAT XXH3

`xxhash-wasm` stops at classic XXH32/64. `hash-wasm`'s
XXH3 is clang output with SIMD commented out. Native
XXH3 is the hash people upgrade to; on wasm it is
currently the worse of the two worlds (emulated wide
mul, no SIMD secret path).

A 4–6 KB WAT XXH3 with a scalar `i64.mul` bulk loop
and an optional `v128` path would be the drop-in
successor to a 6.6 M/week package. Bit-exact against
Cyan4973/xxHash. The interesting work is the short
keys (where XXH64 still beats hayahash in the
in-tree shootout) and the 17–240-byte avalanche
path, not the 1 MiB loop.

This is the highest-leverage new project if the
goal is adoption. The algorithm is already the
default in the ecosystems that matter; the wasm
implementation is the part that is stale.

### 2. Streaming SHA-256 in WAT, with SIMD

WebCrypto cannot hash a file incrementally.
`hash-wasm` can, scalar, from C. A WAT module that
keeps the 32-byte state in locals, uses
`i8x16.swizzle` for the big-endian load, and runs a
degree-4 schedule on `i32x4` is a known technique
(Whealy's 3.2 KB binary) that has never been
packaged as `createSHA256()` with chunked
`update`.

The win is not "beat WebCrypto on a 32-byte
one-shot" — you will not. The win is "hash a 2 GB
blob in a Worker at native-ish GB/s, 4 KB of
wasm, no clang in the build." File identity in
the browser is still MD5 or a one-shot SHA-256
that OOMs. That is a product.

SHA-512 and SHA-3 are the same shape and less
demand; do SHA-256 first.

### 3. A 2 KB WAT CRC32 / CRC32C

38 M weekly downloads sit on a JS table. The
algorithm is a 32-bit recurrence. A slicing-by-8
or `v128` carry-less-emulated CRC32C that is
bit-exact with zlib / SSE4.2 `__crc32` is a
weekend of instruction selection and then a
very long time of test vectors. Small, useful,
easy to verify. Not a research hash; a
replacement for a table.

### 4. hayahash.wat

Not a new algorithm. The same digest, written
as WAT, with the eight-lane bulk loop in
registers and the overlapping tail spelled as
explicit `i64.load`s. The question is how much
`zig cc -O3` leaves on the table on V8,
SpiderMonkey, and Wasmtime.

If the answer is "a few percent," it stays a
branch. If the answer is "the 8-byte path drops
below XXH64," it is the thing that lets hayahash
take the default-tiny-key slot that XXH64 still
owns in [`benchmarks.md`](benchmarks.md). Either
way it is the only project on this list that
does not invent a new hash.

Optional second step, still bit-exact: an
`i64x2.mul` SIMD shape behind a feature bit.
wasm SIMD `i64x2.mul` is widely shipped. The C
header already has a Zen-only AVX-512 shape;
a wasm SIMD shape would be the same idea on a
target hayahash claims as primary.

### 5. A wasm-native `BuildHasher` crate

Rust-to-wasm programs currently pick FxHasher
(fast, low quality, not a published digest) or
ahash (AES-shaped, awkward on wasm). A
`no_std`, no-`getrandom`, seed-from-the-caller
`BuildHasher` over hayahash64 — once
`HayaHasher` stops buffering the world — is
the in-process half of the same portability
claim. This is a crate, not a new hash.

### 6. BLAKE3 degree-4 as a real package

`as-blake` already has the kernel. What it
does not have is the `xxhash-wasm` packaging
discipline: 9 KB, base64-embedded, sync
instantiate, streaming, SIMD and SWAR as
one module with a feature detect, bit-exact
against the official test vectors, Cloudflare
Workers `CompiledWasm` export. Integrity-hash
users who will not take XXH and cannot use
WebCrypto (keyed BLAKE3, XOF, derive-key)
are the audience. Lower volume than (1) or
(2); higher quality-of-implementation gap.

### 7. Things that look tempting and are not

- **Beating WebCrypto SHA-256 on a single
  `digest()` of a small buffer.** The host
  implementation is native and the JS↔wasm
  copy loses.
- **A wasm MurmurHash3 to replace
  `imurmurhash`.** The keys are short and
  already in JS heaps. The 130 M downloads
  are a trap.
- **Another wyhash/rapidhash port to wasm.**
  The wide-mul hole is why hayahash exists;
  a fourth port of the same primitive does
  not close it.
- **Password KDFs (Argon2, scrypt, bcrypt).**
  hash-wasm already ships them. The work is
  memory, not instruction selection.
- **AES-based hashes (Meow, ahash-on-AES).**
  wasm has no AES-NI. The proposed
  `wasm simd` / future crypto ops do not
  change that this year.

## Suggested order

If the goal is a new repo that can absorb the
same kind of work hayahash did (WAT or
freestanding C, KATs, a 3–8 KB module, a
JS loader that does not require a bundler
plugin):

1. **XXH3-in-WAT** — existing demand, stale
   implementations, bit-exact target.
2. **Streaming SHA-256-in-WAT+SIMD** — existing
   demand, an API hole WebCrypto will not
   fill, bit-exact target.
3. **hayahash.wat** — same digest, measures
   whether the compiler is the limit.
4. **CRC32-in-WAT** — small, closed, 38 M
   JS downloads as the baseline.

(1) and (2) are new projects. (3) is a
hayahash port. (4) is a palette cleanser
that still teaches the WAT toolchain.

The methodological carry-over from hayahash
is the same in all four: one authoritative
reference, versioned test vectors, a wasm
build that CI rebuilds and diffs, and a
primitive set that matches what every wasm
engine actually has — `i64.mul`, `rotl`,
loads, and, if you opt in, `v128`.
