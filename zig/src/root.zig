//! hayahash64 - small, fast, portable 64-bit hash function.
//!
//! Bit-exact Zig port of the reference C implementation (`hayahash.h`
//! at the repository root); see that header for the full design notes.
//! Output is identical for every input and seed on every platform, and
//! is independent of host endianness.
//!
//! This is free and unencumbered software released into the public
//! domain. For more information, please refer to <https://unlicense.org/>

const std = @import("std");

/// Multiplier: 2^64 / golden ratio, odd.
const K: u64 = 0x9E3779B97F4A7C15;
/// moremur finalizer constants (Pelle Evensen); M1 doubles as the
/// second multiplier of the short path.
const M1: u64 = 0x3C79AC492BA7B653;
const M2: u64 = 0x1C69B3F74AC4AE35;

/// Input length (in bytes) at or above which the 8-lane bulk loop
/// kicks in.
const bulk_min: usize = 320;

inline fn load64le(p: []const u8, i: usize) u64 {
    return std.mem.readInt(u64, p[i..][0..8], .little);
}

inline fn load32le(p: []const u8, i: usize) u64 {
    return std.mem.readInt(u32, p[i..][0..4], .little);
}

inline fn rotl(x: u64, comptime n: u6) u64 {
    return std.math.rotl(u64, x, n);
}

/// moremur finalizer (Pelle Evensen).
inline fn fmix(v: u64) u64 {
    var x = v;
    x ^= x >> 27;
    x *%= M1;
    x ^= x >> 33;
    x *%= M2;
    return x ^ (x >> 27);
}

/// Bijective stripe injections (any odd number of rotation terms is
/// invertible over GF(2)). The short path uses a second injection with
/// different rotation amounts for its `b` word so the two multiply
/// terms can never be erased simultaneously by one sparse difference.
inline fn inj(w: u64) u64 {
    return w ^ rotl(w, 21) ^ rotl(w, 41);
}

inline fn inj2(w: u64) u64 {
    return w ^ rotl(w, 11) ^ rotl(w, 50);
}

inline fn injp(p: []const u8, i: usize) u64 {
    return inj(load64le(p, i));
}

/// Absorb one 8-byte stripe: h = (h + (w + rotl(wp, 27))) * K, then
/// remember the stripe so the next lane injects its rotated copy.
inline fn stripe(h: *u64, wp: *u64, p: []const u8, i: usize) void {
    const w = load64le(p, i);
    h.* = (h.* +% (w +% rotl(wp.*, 27))) *% K;
    wp.* = w;
}

/// Hashes `key` with `seed`, returning a 64-bit digest.
///
/// Produces exactly the same value as the C reference `hayahash64()`
/// for all inputs, seeds, and host endiannesses.
pub fn hayahash64(key: []const u8, seed: u64) u64 {
    const len = key.len;
    // Seed & length premix; feeds every path so length extension and
    // overlapping tail reads can never collide across lengths.
    const s = seed ^ (@as(u64, len) *% K);

    if (len <= 16) {
        var a: u64 = 0;
        var b: u64 = 0;
        if (len >= 8) {
            a = load64le(key, 0);
            b = load64le(key, len - 8);
        } else if (len >= 4) {
            a = load32le(key, 0);
            b = load32le(key, len - 4);
        } else if (len > 0) {
            // 1..3 bytes: head, middle, tail (ChibiHash v2 trick).
            a = key[0];
            b = (@as(u64, key[len >> 1]) << 8) | (@as(u64, key[len - 1]) << 16);
        }
        // Two independent multiplies, then a strong finalizer; see the
        // C header for why each word gets its own injection.
        const x = (inj(a) ^ s ^ K) *% K;
        const y = (inj2(b) ^ rotl(s, 23) ^ (K >> 19)) *% M1;
        return fmix(rotl(x, 27) ^ y);
    }

    var h0 = s ^ K;
    var h1 = rotl(s, 17) +% (K << 21);
    var h2 = rotl(s, 34) ^ (K >> 13);
    var h3 = rotl(s, 51) +% (K << 42);
    var wp: u64 = 0;
    var off: usize = 0;
    var l = len;

    if (l >= bulk_min) {
        var h4 = s +% (K >> 27);
        var h5 = rotl(s, 13) ^ (K << 9);
        var h6 = rotl(s, 26) +% (K >> 40);
        var h7 = rotl(s, 39) ^ (K << 30);
        while (true) {
            stripe(&h0, &wp, key, off);
            stripe(&h1, &wp, key, off + 8);
            stripe(&h2, &wp, key, off + 16);
            stripe(&h3, &wp, key, off + 24);
            stripe(&h4, &wp, key, off + 32);
            stripe(&h5, &wp, key, off + 40);
            stripe(&h6, &wp, key, off + 48);
            stripe(&h7, &wp, key, off + 56);
            off += 64;
            l -= 64;
            if (l < 64) break;
        }
        // Fold the upper lanes in with xor + multiply: xor merges of
        // multiply spreads can only cancel by carry-pattern luck.
        h0 = (h0 ^ rotl(h4, 11)) *% K;
        h1 = (h1 ^ rotl(h5, 19)) *% K;
        h2 = (h2 ^ rotl(h6, 31)) *% K;
        h3 = (h3 ^ rotl(h7, 47)) *% K;
    }

    // Mid loop: same absorb as the bulk loop, 4 lanes over 32-byte
    // blocks; wp chains in from the bulk loop.
    while (l >= 32) {
        stripe(&h0, &wp, key, off);
        stripe(&h1, &wp, key, off + 8);
        stripe(&h2, &wp, key, off + 16);
        stripe(&h3, &wp, key, off + 24);
        off += 32;
        l -= 32;
    }

    // Absorb the final loop stripe's dangling rotated copy so
    // difference ladders cannot march off the end of the input.
    h0 +%= rotl(wp, 27);

    // 0..31 bytes left.
    if (l > 16) {
        h0 = (h0 +% injp(key, off)) *% K;
        h1 = (h1 +% injp(key, off + 8)) *% K;
        l -= 16;
    }
    // 0..16 bytes left; total length > 16, so reading the last 16
    // input bytes (overlapping already-hashed data) is always valid.
    // Length is already folded into every lane via `s`.
    if (l > 0) {
        h2 = (h2 +% injp(key, len - 16)) *% K;
        h3 = (h3 +% injp(key, len - 8)) *% K;
    }

    const t0 = (h0 ^ rotl(h1, 13)) *% K;
    const t1 = (h2 ^ rotl(h3, 33)) *% K;
    return fmix(s ^ t0 ^ rotl(t1, 29));
}

test hayahash64 {
    try std.testing.expectEqual(0xC4F85F43D5A9985E, hayahash64("", 0));
    try std.testing.expectEqual(0xF2172C5BD68EC576, hayahash64("hello world", 0));
}
