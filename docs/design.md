# Design

The complete algorithm, stated compactly. The authoritative commentary is
the top of [`hayahash.h`](../hayahash.h), where every decision sits next to
the code it explains; the proofs are in the [working paper](../paper/).

## Notation

All values are unsigned 64-bit words. `+`, `*`, and `<<` discard bits above
bit 63; `>>` is logical; `rotl(x, r)` rotates left. `load64le(p)` reads eight
bytes least-significant-first, `load32le(p)` four, zero-extended. Loads use
`memcpy` plus a byte swap on big-endian hosts, so neither alignment nor host
byte order reaches the digest.

## Constants

```
K  = 0x9E3779B97F4A7C15   floor(2^64 / phi)      the one multiplier
M1 = 0x3C79AC492BA7B653   moremur                short-path second multiplier
M2 = 0x1C69B3F74AC4AE35   moremur
N1 = 0xFF51AFD7ED558CCD   MurmurHash3 finalizer  high word only
N2 = 0xC4CEB9FE1A85EC53   MurmurHash3 finalizer  high word only
```

All five are odd. One arithmetic property of `K` is load-bearing:
`K = 1 (mod 4)`, which is exactly the condition under which multiplication
by `K` fixes the top two bits of a difference (see *Cancellation channels*).

## Primitives

```
inj(w)  = w ^ rotl(w, 21) ^ rotl(w, 41)      bijective
inj2(w) = w ^ rotl(w, 11) ^ rotl(w, 50)      bijective

fmix(x):              fmix128(x):           lfmix(x):
    x ^= x >> 27          x ^= x >> 30          x ^= x >> 37
    x *= M1               x *= N1               x *= K
    x ^= x >> 33          x ^= x >> 31          x ^= x >> 32
    x *= M2               x *= N2               return x
    x ^= x >> 27          x ^= x >> 33
```

Any odd number of rotation terms is invertible over GF(2); any even number is
not. All three finalizers are bijections.

## Dispatch

Every path starts with

```
s      = seed ^ K
lenmix = n * K
```

The length enters nowhere else. It is absorbed in the finalizer, not premixed
into the state, so the state after *k* bytes is a function of `(seed, those k
bytes)` alone — which is what makes a streaming API that reproduces one-shot
digests possible.

| length | lanes | block |
|---|---|---|
| `n <= 16` | — | dedicated two-multiply path |
| `17 <= n <= 319` | 4 | 32 bytes |
| `n >= 320` | 8 | 64 bytes, then falls through to the 4-lane machinery |

## Short path, `n <= 16`

```
n >= 8:  a = load64le(B, 0)   b = load64le(B, n - 8)
n >= 4:  a = load32le(B, 0)   b = load32le(B, n - 4)
n >  0:  a = B[0]             b = (B[n/2] << 8) | (B[n-1] << 16)
n == 0:  a = 0                b = 0

x = (inj(a)  ^ s             ^ K)        * K
y = (inj2(b) ^ rotl(s, 23)   ^ (K >> 19)) * M1
u = rotl(x, 27) ^ y ^ lenmix

h64 = fmix(u)
```

The head and end reads overlap for some lengths; `lenmix` is what separates
those inputs. Each word is spread by a *different* injection before its
multiply, so a difference confined to the top bits gets low copies, and no
single key difference can erase the seed from both multiply terms at once.

## Absorb

Longer inputs are consumed as 64-bit *stripes* `w_0, w_1, ...`. With `wp` the
previous stripe (initially 0) and `r = 27`:

```
absorb(h, w):  h = (h ^ (w + rotl(wp, r))) * K ;  wp = w
```

`wp` chains across lanes, across blocks, and across the boundary between the
eight-lane and four-lane loops. Two properties follow. The stripe-to-absorbed
map is a bijection, so at the first stripe where two inputs differ their
absorbed values differ — no difference pattern cancels inside the absorb. And
every stripe's bits get a second, rotated life in the *next* absorb, combined
by addition rather than XOR, so cancelling both copies requires a carry
pattern rather than an algebraic identity.

## Lane initial values

All eight derive from `s` and shifted copies of `K`, so no large per-lane
literal is materialized (on AArch64 a 64-bit literal costs four
instructions) and full-state seeding is free.

```
h0 = s ^ K                      h4 = s + (K >> 27)
h1 = rotl(s, 17) + (K << 21)    h5 = rotl(s, 13) ^ (K <<  9)
h2 = rotl(s, 34) ^ (K >> 13)    h6 = rotl(s, 26) + (K >> 40)
h3 = rotl(s, 51) + (K << 42)    h7 = rotl(s, 39) ^ (K << 30)
```

The mid path uses `h0..h3`; the bulk path uses all eight.

## Bulk block, `n >= 320`

Each 64-byte block absorbs eight consecutive stripes into `h0..h7` in order,
then adds the raw eighth stripe into lane 0:

```
absorb(h0, w0) ... absorb(h7, w7)
h0 += w7                                 checkpoint; part of the digest
```

Nothing crosses lanes inside the loop. After the last complete block the
upper lanes fold into the lower:

```
h0 = (h0 ^ rotl(h4, 11)) * K      h2 = (h2 ^ rotl(h6, 31)) * K
h1 = (h1 ^ rotl(h5, 19)) * K      h3 = (h3 ^ rotl(h7, 47)) * K
```

If at least 32 bytes remain, one four-lane block runs; the `wp` chain
continues across the fold.

## Wall, tail, finalizer

```
h0 += rotl(wp, r)                        wall; no-op if no complete stripe

rho > 16:  h0 = (h0 + inj(load64le(B, p + 0))) * K
           h1 = (h1 + inj(load64le(B, p + 8))) * K
rho >  0:  h2 = (h2 + inj(load64le(B, n - 16))) * K
           h3 = (h3 + inj(load64le(B, n -  8))) * K

t0 = (h0 ^ rotl(h1, 13) ^ lenmix) * K
t1 = (h2 ^ rotl(h3, 33)) * K
z  = s ^ t0 ^ rotl(t1, 29)

h64 = lfmix(z)
```

`p` is the first unconsumed offset and `rho < 32` the remainder. The last two
reads always cover the final 16 bytes and may overlap bytes already absorbed;
there are no tail reads at `rho == 0`. Tail absorbs use `inj` rather than the
chained form because each feeds its own lane, where per-absorb bijectivity is
enough.

`lenmix` enters *inside* the `t0` multiply. XOR-ing it after the final
multiplies also separates lengths, but leaves the difference in a low-order
subgroup that survives to the output as modular structure across lengths.

## The 128-bit digest

Identical state walk, two words extracted at the end.

```
n <= 16:  lo = fmix(u)       hi = fmix128(x + rotl(u, 32))
n >= 17:  lo = lfmix(z)      hi = fmix128(rotl(s, 32) ^ (t1 + rotl(t0, 47)))
```

`hayahash128(...).lo == hayahash64(...)` for every input and seed, so
producing the second word costs a finalizer, not a second pass. The high word
combines the fold words with addition where the low word uses XOR, and uses a
different bijection, so one GF(2)-linear cancellation cannot erase a
difference from both.

On the short path `(u, x)` is a bijection of the pre-image pair `(x, y)` for
a fixed length, so the 128-bit digest is injective in the message for each
fixed length and seed — and, because `x` determines `s`, injective in the
seed for each fixed message. At exactly 16 bytes it is a permutation of the
128-bit space.

## Cancellation channels

Multiplication mod 2^64 is the only nonlinear operation available in this
primitive class, and it is weakest exactly where a difference wants to hide.
Three channels exist; each is closed by a specific constant.

**Top-window invariance.** For any odd `c`, `c * 2^63 = 2^63`. For
`c = 1 (mod 4)` — true of `K` — the whole four-element subgroup
`{0, 2^62, 2^63, 2^62 + 2^63}` is fixed pointwise. A difference planted there
survives every multiply unchanged and can be cancelled by an equal difference
introduced later in the same lane. The chained rotated copy is the answer:
the same difference also appears rotated, under addition, in the next
absorb.

**Ladder return distance.** A carry-matched cancellation leaves a residual
that walks `r` positions per stripe. Since `gcd(27, 64) = 1` it visits every
position with period 64. Starting in the top window, the least `d > 0` that
returns it there is `min{d > 0 : rd = 0, 1, or -1 (mod 64)}`: **3 for
`r = 21`, 19 for `r = 27`**. That factor of six in required carry conditions
is why the rotation is 27. Returning *in the same lane* of an `L`-lane loop
additionally needs `L | d`.

**Fold-rotation resonance.** `rotl(.,r')` inverts `rotl(.,r)` exactly when
`r + r' = 0 (mod 64)`. For `r = 27` that is 37, which would realign a stripe
difference in lane 2 with its own rotated copy in lane 3 at the `t1` fold and
permit exact XOR cancellation. The finalizer uses 33.

Three structural bounds compose with these. The four-lane loop never runs
more than `4 * floor(319/32) = 36` chained stripes, short of the rotation's
64-stripe orbit. The bulk checkpoint injects a raw, unmultiplied stripe into
lane 0 every 8 stripes, interrupting an orbit before it can return to its
starting lane. The wall absorbs the final stripe's dangling rotated copy, so
a ladder cannot be terminated by placing its last difference past the end of
the input.

Folds multiply (`(h ^ rotl(h', .)) * K`) rather than add: the absorb combines
additively, so an additive fold would put a difference and its copy in the
same algebra, where exact cancellation is a linear condition.

## Fixed algorithm parameters

The 320-byte bulk threshold, the absorb rotation 27, and the 64- and 32-byte
block sizes are algorithm parameters, not tuning knobs. An input of exactly
320 bytes takes the eight-lane path; moving the threshold changes digests and
can push the four-lane path past the 36-stripe bound above.

Dispatch *shape* — straight-line length tiers, loop unrolling, whether the
bulk path is outlined, whether a compiler vectorizes it — is chosen per
architecture and compiler and is specified to leave the digest unchanged.
[`implementation.md`](implementation.md) lists the shapes;
[`smhasher3.md`](smhasher3.md#covering-every-dispatch-shape) shows how one
conformance run covers all of them.

## Streaming

`hayahash64_state` holds the eight lanes, `wp`, the seed, the running total,
and a 448-byte buffer with a 128-byte retention floor. `update` appends;
`digest` may be called at any time, does not modify the state, and returns
either width. Bytes are consumed only in whole 64-byte blocks in one-shot
order from stream offset zero; the retention floor guarantees the buffered
suffix still contains the last 16 bytes the tail reads back; totals below 448
bytes stay buffered and dispatch to the one-shot function. Digests are
therefore identical to one-shot for every split of the same input, in both
widths.

## Status

hayahash is experimental: the algorithm, constants, and digest values may
change, so digests should not be persisted across versions yet. The checklist
for freezing them at 1.0 is in [`stability.md`](stability.md).
