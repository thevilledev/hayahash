/*
 * hayahash64 and hayahash128 - small, fast, portable hash functions.
 *
 * This is free and unencumbered software released into the public
 * domain. For more information, please refer to https://unlicense.org/
 */

import Foundation

/// A 128-bit digest represented as two words.
public struct Hash128: Equatable, Sendable {
    /// Low output word; exactly `Hayahash.hash64` for the same input and seed.
    public let lo: UInt64
    /// Algebraically independent high output word.
    public let hi: UInt64

    public init(lo: UInt64, hi: UInt64) {
        self.lo = lo
        self.hi = hi
    }
}

/// hayahash64 and hayahash128, small, fast, portable hash functions.
///
/// This is a bit-exact Swift port of the reference C implementation
/// (`hayahash.h` at the repository root); see that header for the full
/// design notes. Output is identical for every input and seed on every
/// platform, and is independent of host endianness.
///
/// The reference's portability rules (no SIMD, no 128-bit multiply, no
/// hardware-specific instructions) map onto wrapping `UInt64` arithmetic
/// and explicit little-endian loads.
///
/// All methods are static, allocation-free, and thread-safe.
public enum Hayahash {
    // Multiplier: 2^64 / golden ratio, odd.
    private static let k: UInt64 = 0x9E3779B97F4A7C15

    // moremur finalizer constants (Pelle Evensen); m1 doubles as the
    // second multiplier of the short path.
    private static let m1: UInt64 = 0x3C79AC492BA7B653
    private static let m2: UInt64 = 0x1C69B3F74AC4AE35
    private static let n1: UInt64 = 0xFF51AFD7ED558CCD
    private static let n2: UInt64 = 0xC4CEB9FE1A85EC53

    // Input length (in bytes) at or above which the 8-lane bulk loop
    // kicks in.
    private static let bulkMin = 320

    /// Hashes `data` with `seed`, returning a 64-bit digest.
    ///
    /// Produces exactly the same value as the C reference `hayahash64()`
    /// for all inputs, seeds, and host endiannesses.
    public static func hash64(_ data: some ContiguousBytes, seed: UInt64 = 0) -> UInt64 {
        data.withUnsafeBytes { hashImpl($0, seed: seed) }
    }

    /// Hashes the `length` bytes of `data` starting at `offset` with
    /// `seed`, returning a 64-bit digest.
    ///
    /// Equivalent to hashing a copy of that range, without the copy.
    public static func hash64(
        _ data: [UInt8],
        offset: Int,
        length: Int,
        seed: UInt64 = 0
    ) -> UInt64 {
        precondition(
            offset >= 0 && length >= 0 && offset <= data.count
                && length <= data.count - offset
        )
        return data.withUnsafeBytes { buf in
            hashImpl(
                UnsafeRawBufferPointer(rebasing: buf[offset..<(offset + length)]),
                seed: seed
            )
        }
    }

    /// Hashes `data` once and returns both output words.
    public static func hash128(
        _ data: some ContiguousBytes,
        seed: UInt64 = 0
    ) -> Hash128 {
        data.withUnsafeBytes { hash128Impl($0, seed: seed) }
    }

    /// Hashes a byte-array range once and returns both output words.
    public static func hash128(
        _ data: [UInt8],
        offset: Int,
        length: Int,
        seed: UInt64 = 0
    ) -> Hash128 {
        precondition(
            offset >= 0 && length >= 0 && offset <= data.count
                && length <= data.count - offset
        )
        return data.withUnsafeBytes { buf in
            hash128Impl(
                UnsafeRawBufferPointer(rebasing: buf[offset..<(offset + length)]),
                seed: seed
            )
        }
    }

    private static func hashImpl(_ key: UnsafeRawBufferPointer, seed: UInt64) -> UInt64 {
        // The seed premixes into s; the length is absorbed in the
        // finalizer instead (through a multiply against state), so the
        // digest is a pure function of (seed, bytes-so-far) and a
        // streaming implementation can match it without knowing the
        // total length up front. len -> len*K is injective, which
        // keeps the overlapping tail reads collision-free across
        // lengths.
        let len = key.count
        let s = seed ^ k
        if len <= 16 {
            return hashShort(key, s)
        }
        return hashLong(key, s)
    }

    private static func hash128Impl(
        _ key: UnsafeRawBufferPointer,
        seed: UInt64
    ) -> Hash128 {
        let s = seed ^ k
        if key.count <= 16 {
            return hash128Short(key, s)
        }
        return hash128Long(key, s)
    }

    private static func hash128Short(
        _ key: UnsafeRawBufferPointer,
        _ s: UInt64
    ) -> Hash128 {
        let len = key.count
        let a: UInt64
        let b: UInt64
        if len >= 8 {
            a = load64(key, 0)
            b = load64(key, len - 8)
        } else if len >= 4 {
            a = load32(key, 0)
            b = load32(key, len - 4)
        } else if len > 0 {
            a = UInt64(key[0])
            b = (UInt64(key[len >> 1]) << 8) | (UInt64(key[len - 1]) << 16)
        } else {
            a = 0
            b = 0
        }
        let x = (inj(a) ^ s ^ k) &* k
        let y = (inj2(b) ^ rotl(s, 23) ^ (k >> 19)) &* m1
        let u = rotl(x, 27) ^ y ^ (UInt64(len) &* k)
        return Hash128(lo: fmix(u), hi: fmix128(x &+ rotl(u, 32)))
    }

    private static func hash128Long(
        _ key: UnsafeRawBufferPointer,
        _ s: UInt64
    ) -> Hash128 {
        var h0 = s ^ k
        var h1 = rotl(s, 17) &+ (k << 21)
        var h2 = rotl(s, 34) ^ (k >> 13)
        var h3 = rotl(s, 51) &+ (k << 42)
        var w: UInt64
        var wp: UInt64 = 0
        var p = 0
        var l = key.count

        if l >= bulkMin {
            var h4 = s &+ (k >> 27)
            var h5 = rotl(s, 13) ^ (k << 9)
            var h6 = rotl(s, 26) &+ (k >> 40)
            var h7 = rotl(s, 39) ^ (k << 30)
            repeat {
                w = load64(key, p)
                h0 = (h0 ^ (w &+ rotl(wp, 27))) &* k
                wp = w
                w = load64(key, p + 8)
                h1 = (h1 ^ (w &+ rotl(wp, 27))) &* k
                wp = w
                w = load64(key, p + 16)
                h2 = (h2 ^ (w &+ rotl(wp, 27))) &* k
                wp = w
                w = load64(key, p + 24)
                h3 = (h3 ^ (w &+ rotl(wp, 27))) &* k
                wp = w
                w = load64(key, p + 32)
                h4 = (h4 ^ (w &+ rotl(wp, 27))) &* k
                wp = w
                w = load64(key, p + 40)
                h5 = (h5 ^ (w &+ rotl(wp, 27))) &* k
                wp = w
                w = load64(key, p + 48)
                h6 = (h6 ^ (w &+ rotl(wp, 27))) &* k
                wp = w
                w = load64(key, p + 56)
                h7 = (h7 ^ (w &+ rotl(wp, 27))) &* k
                wp = w
                h0 &+= wp
                p += 64
                l -= 64
            } while l >= 64
            h0 = (h0 ^ rotl(h4, 11)) &* k
            h1 = (h1 ^ rotl(h5, 19)) &* k
            h2 = (h2 ^ rotl(h6, 31)) &* k
            h3 = (h3 ^ rotl(h7, 47)) &* k
        }

        while l >= 32 {
            w = load64(key, p)
            h0 = (h0 ^ (w &+ rotl(wp, 27))) &* k
            wp = w
            w = load64(key, p + 8)
            h1 = (h1 ^ (w &+ rotl(wp, 27))) &* k
            wp = w
            w = load64(key, p + 16)
            h2 = (h2 ^ (w &+ rotl(wp, 27))) &* k
            wp = w
            w = load64(key, p + 24)
            h3 = (h3 ^ (w &+ rotl(wp, 27))) &* k
            wp = w
            l -= 32
            p += 32
        }

        h0 &+= rotl(wp, 27)
        if l > 16 {
            h0 = (h0 &+ injAt(key, p)) &* k
            h1 = (h1 &+ injAt(key, p + 8)) &* k
            p += 16
            l -= 16
        }
        if l > 0 {
            h2 = (h2 &+ injAt(key, p + l - 16)) &* k
            h3 = (h3 &+ injAt(key, p + l - 8)) &* k
        }

        let t0 = (h0 ^ rotl(h1, 13) ^ (UInt64(key.count) &* k)) &* k
        let t1 = (h2 ^ rotl(h3, 33)) &* k
        var x = s ^ t0 ^ rotl(t1, 29)
        x ^= x >> 37
        x &*= k
        return Hash128(
            lo: x ^ (x >> 32),
            hi: fmix128(rotl(s, 32) ^ (t1 &+ rotl(t0, 47)))
        )
    }

    // Inputs of at most 16 bytes: two independent multiplies (one per
    // loaded word), then a strong finalizer; see the C header for why
    // each word gets its own injection.
    private static func hashShort(_ key: UnsafeRawBufferPointer, _ s: UInt64) -> UInt64 {
        let len = key.count
        let a: UInt64
        let b: UInt64
        if len >= 8 {
            a = load64(key, 0)
            b = load64(key, len - 8)
        } else if len >= 4 {
            a = load32(key, 0)
            b = load32(key, len - 4)
        } else if len > 0 {
            // 1..3 bytes: head, middle, tail (ChibiHash v2 trick).
            a = UInt64(key[0])
            b = (UInt64(key[len >> 1]) << 8) | (UInt64(key[len - 1]) << 16)
        } else {
            a = 0
            b = 0
        }
        let x = (inj(a) ^ s ^ k) &* k
        let y = (inj2(b) ^ rotl(s, 23) ^ (k >> 19)) &* m1
        return fmix(rotl(x, 27) ^ y ^ (UInt64(len) &* k))
    }

    // Inputs over 16 bytes: 8-lane bulk loop over 64-byte blocks, then
    // a 4-lane mid loop over 32-byte blocks, then overlapping tail
    // reads. Each lane absorbs t = w + rotl(wp, 27), where wp is the
    // previous stripe (chained across lanes, blocks, and the bulk/mid
    // boundary): h' = (h ^ t) * K.
    //
    // Indices stay absolute into key so the final overlapping 16-byte
    // tail can read already-consumed bytes (p + l - 16 may precede p).
    private static func hashLong(_ key: UnsafeRawBufferPointer, _ s: UInt64) -> UInt64 {
        var h0 = s ^ k
        var h1 = rotl(s, 17) &+ (k << 21)
        var h2 = rotl(s, 34) ^ (k >> 13)
        var h3 = rotl(s, 51) &+ (k << 42)
        var w: UInt64
        var wp: UInt64 = 0
        var p = 0
        var l = key.count

        if l >= bulkMin {
            var h4 = s &+ (k >> 27)
            var h5 = rotl(s, 13) ^ (k << 9)
            var h6 = rotl(s, 26) &+ (k >> 40)
            var h7 = rotl(s, 39) ^ (k << 30)
            repeat {
                w = load64(key, p)
                h0 = (h0 ^ (w &+ rotl(wp, 27))) &* k
                wp = w
                w = load64(key, p + 8)
                h1 = (h1 ^ (w &+ rotl(wp, 27))) &* k
                wp = w
                w = load64(key, p + 16)
                h2 = (h2 ^ (w &+ rotl(wp, 27))) &* k
                wp = w
                w = load64(key, p + 24)
                h3 = (h3 ^ (w &+ rotl(wp, 27))) &* k
                wp = w
                w = load64(key, p + 32)
                h4 = (h4 ^ (w &+ rotl(wp, 27))) &* k
                wp = w
                w = load64(key, p + 40)
                h5 = (h5 ^ (w &+ rotl(wp, 27))) &* k
                wp = w
                w = load64(key, p + 48)
                h6 = (h6 ^ (w &+ rotl(wp, 27))) &* k
                wp = w
                w = load64(key, p + 56)
                h7 = (h7 ^ (w &+ rotl(wp, 27))) &* k
                wp = w
                // Checkpoint the raw-word chain once per block so a
                // 64-stripe rotation orbit cannot hide a difference.
                h0 &+= wp
                p += 64
                l -= 64
            } while l >= 64
            // Fold the upper lanes in with xor + multiply: xor merges
            // of multiply spreads can only cancel by carry-pattern
            // luck, never exactly.
            h0 = (h0 ^ rotl(h4, 11)) &* k
            h1 = (h1 ^ rotl(h5, 19)) &* k
            h2 = (h2 ^ rotl(h6, 31)) &* k
            h3 = (h3 ^ rotl(h7, 47)) &* k
        }

        // Mid loop: same absorb as the bulk loop, 4 lanes over 32-byte
        // blocks; wp chains in from the bulk loop.
        while l >= 32 {
            w = load64(key, p)
            h0 = (h0 ^ (w &+ rotl(wp, 27))) &* k
            wp = w
            w = load64(key, p + 8)
            h1 = (h1 ^ (w &+ rotl(wp, 27))) &* k
            wp = w
            w = load64(key, p + 16)
            h2 = (h2 ^ (w &+ rotl(wp, 27))) &* k
            wp = w
            w = load64(key, p + 24)
            h3 = (h3 ^ (w &+ rotl(wp, 27))) &* k
            wp = w
            l -= 32
            p += 32
        }

        // Absorb the final loop stripe's dangling rotated copy so
        // difference ladders cannot march off the end of the input.
        h0 &+= rotl(wp, 27)

        // 0..31 bytes left.
        if l > 16 {
            h0 = (h0 &+ injAt(key, p)) &* k
            h1 = (h1 &+ injAt(key, p + 8)) &* k
            p += 16
            l -= 16
        }
        // 0..16 bytes left; total length > 16, so reading the last 16
        // input bytes (overlapping already-hashed data) is always
        // valid. Length is folded into t0 below, through a multiply.
        if l > 0 {
            h2 = (h2 &+ injAt(key, p + l - 16)) &* k
            h3 = (h3 &+ injAt(key, p + l - 8)) &* k
        }

        let t0 = (h0 ^ rotl(h1, 13) ^ (UInt64(key.count) &* k)) &* k
        let t1 = (h2 ^ rotl(h3, 33)) &* k
        var x = s ^ t0 ^ rotl(t1, 29)
        // The long path has already mixed every byte through a
        // multiply; one final multiply is enough to avalanche lanes.
        x ^= x >> 37
        x &*= k
        return x ^ (x >> 32)
    }

    // moremur finalizer (Pelle Evensen).
    @inline(__always)
    private static func fmix(_ x: UInt64) -> UInt64 {
        var x = x
        x ^= x >> 27
        x &*= m1
        x ^= x >> 33
        x &*= m2
        return x ^ (x >> 27)
    }

    @inline(__always)
    private static func fmix128(_ x: UInt64) -> UInt64 {
        var x = x
        x ^= x >> 30
        x &*= n1
        x ^= x >> 31
        x &*= n2
        return x ^ (x >> 33)
    }

    // inj and inj2 are bijective stripe injections (any odd number of
    // rotation terms is invertible over GF(2)). The short path uses a
    // second injection with different rotation amounts for its b word
    // so the two multiply terms can never be erased simultaneously by
    // one sparse difference.
    @inline(__always)
    private static func inj(_ w: UInt64) -> UInt64 {
        w ^ rotl(w, 21) ^ rotl(w, 41)
    }

    @inline(__always)
    private static func inj2(_ w: UInt64) -> UInt64 {
        w ^ rotl(w, 11) ^ rotl(w, 50)
    }

    @inline(__always)
    private static func injAt(_ key: UnsafeRawBufferPointer, _ i: Int) -> UInt64 {
        inj(load64(key, i))
    }

    @inline(__always)
    private static func rotl(_ x: UInt64, _ n: Int) -> UInt64 {
        (x << n) | (x >> (64 - n))
    }

    // Explicit little-endian loads so digests match on big-endian hosts.
    @inline(__always)
    private static func load64(_ key: UnsafeRawBufferPointer, _ i: Int) -> UInt64 {
        let b0 = UInt64(key[i])
        let b1 = UInt64(key[i &+ 1]) << 8
        let b2 = UInt64(key[i &+ 2]) << 16
        let b3 = UInt64(key[i &+ 3]) << 24
        let b4 = UInt64(key[i &+ 4]) << 32
        let b5 = UInt64(key[i &+ 5]) << 40
        let b6 = UInt64(key[i &+ 6]) << 48
        let b7 = UInt64(key[i &+ 7]) << 56
        return b0 | b1 | b2 | b3 | b4 | b5 | b6 | b7
    }

    @inline(__always)
    private static func load32(_ key: UnsafeRawBufferPointer, _ i: Int) -> UInt64 {
        let b0 = UInt64(key[i])
        let b1 = UInt64(key[i &+ 1]) << 8
        let b2 = UInt64(key[i &+ 2]) << 16
        let b3 = UInt64(key[i &+ 3]) << 24
        return b0 | b1 | b2 | b3
    }
}
