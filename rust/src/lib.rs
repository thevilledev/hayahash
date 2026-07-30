//! hayahash64 - small, fast, portable 64-bit hash function.
//!
//! Bit-exact Rust port of the reference C implementation (`hayahash.h`
//! at the repository root); see that header for the full design notes.
//! Output is identical for every input and seed on every platform, and
//! is independent of host endianness.
//!
//! ```
//! let h = hayahash::hayahash64(b"hello world", 0);
//! assert_eq!(h, hayahash::hayahash64(b"hello world", 0));
//! ```
//!
//! This is free and unencumbered software released into the public
//! domain. For more information, please refer to <https://unlicense.org/>

#![no_std]
#![forbid(unsafe_code)]
#![warn(missing_docs)]

/// Multiplier: 2^64 / golden ratio, odd.
const K: u64 = 0x9E37_79B9_7F4A_7C15;
/// moremur finalizer constants (Pelle Evensen); M1 doubles as the
/// second multiplier of the short path.
const M1: u64 = 0x3C79_AC49_2BA7_B653;
const M2: u64 = 0x1C69_B3F7_4AC4_AE35;

/// Input length (in bytes) at or above which the 8-lane bulk loop
/// kicks in.
const BULK_MIN: usize = 320;

#[inline(always)]
fn load64le(p: &[u8], i: usize) -> u64 {
    u64::from_le_bytes(p[i..i + 8].try_into().unwrap())
}

#[inline(always)]
fn load32le(p: &[u8], i: usize) -> u64 {
    u64::from(u32::from_le_bytes(p[i..i + 4].try_into().unwrap()))
}

/// moremur finalizer (Pelle Evensen).
#[inline(always)]
fn fmix(mut x: u64) -> u64 {
    x ^= x >> 27;
    x = x.wrapping_mul(M1);
    x ^= x >> 33;
    x = x.wrapping_mul(M2);
    x ^ (x >> 27)
}

/// Bijective stripe injections (any odd number of rotation terms is
/// invertible over GF(2)). The short path uses a second injection with
/// different rotation amounts for its `b` word so the two multiply
/// terms can never be erased simultaneously by one sparse difference.
#[inline(always)]
fn inj(w: u64) -> u64 {
    w ^ w.rotate_left(21) ^ w.rotate_left(41)
}

#[inline(always)]
fn inj2(w: u64) -> u64 {
    w ^ w.rotate_left(11) ^ w.rotate_left(50)
}

#[inline(always)]
fn injp(p: &[u8], i: usize) -> u64 {
    inj(load64le(p, i))
}

/// Hashes `key` with `seed`, returning a 64-bit digest.
///
/// Produces exactly the same value as the C reference `hayahash64()`
/// for all inputs, seeds, and host endiannesses.
///
/// ```
/// assert_eq!(hayahash::hayahash64(b"", 0), 0xC4F8_5F43_D5A9_985E);
/// ```
#[must_use]
pub fn hayahash64(key: &[u8], seed: u64) -> u64 {
    let len = key.len();
    // Seed & length premix; feeds every path so length extension and
    // overlapping tail reads can never collide across lengths.
    let s = seed ^ (len as u64).wrapping_mul(K);

    if len <= 16 {
        let (a, b);
        if len >= 8 {
            a = load64le(key, 0);
            b = load64le(key, len - 8);
        } else if len >= 4 {
            a = load32le(key, 0);
            b = load32le(key, len - 4);
        } else if len > 0 {
            // 1..3 bytes: head, middle, tail (ChibiHash v2 trick).
            a = u64::from(key[0]);
            b = (u64::from(key[len >> 1]) << 8) | (u64::from(key[len - 1]) << 16);
        } else {
            a = 0;
            b = 0;
        }
        // Two independent multiplies, then a strong finalizer; see the
        // C header for why each word gets its own injection.
        let x = (inj(a) ^ s ^ K).wrapping_mul(K);
        let y = (inj2(b) ^ s.rotate_left(23) ^ (K >> 19)).wrapping_mul(M1);
        return fmix(x.rotate_left(27) ^ y);
    }

    let mut h0 = s ^ K;
    let mut h1 = s.rotate_left(17).wrapping_add(K << 21);
    let mut h2 = s.rotate_left(34) ^ (K >> 13);
    let mut h3 = s.rotate_left(51).wrapping_add(K << 42);
    let mut wp = 0u64;
    let mut off = 0usize;
    let mut l = len;

    // Absorb one 8-byte stripe: h = (h + (w + rotl(wp, 27))) * K, then
    // remember the stripe so the next lane injects its rotated copy.
    macro_rules! stripe {
        ($h:ident, $at:expr) => {{
            let w = load64le(key, $at);
            $h = $h
                .wrapping_add(w.wrapping_add(wp.rotate_left(27)))
                .wrapping_mul(K);
            wp = w;
        }};
    }

    if l >= BULK_MIN {
        let mut h4 = s.wrapping_add(K >> 27);
        let mut h5 = s.rotate_left(13) ^ (K << 9);
        let mut h6 = s.rotate_left(26).wrapping_add(K >> 40);
        let mut h7 = s.rotate_left(39) ^ (K << 30);
        loop {
            stripe!(h0, off);
            stripe!(h1, off + 8);
            stripe!(h2, off + 16);
            stripe!(h3, off + 24);
            stripe!(h4, off + 32);
            stripe!(h5, off + 40);
            stripe!(h6, off + 48);
            stripe!(h7, off + 56);
            off += 64;
            l -= 64;
            if l < 64 {
                break;
            }
        }
        // Fold the upper lanes in with xor + multiply: xor merges of
        // multiply spreads can only cancel by carry-pattern luck.
        h0 = (h0 ^ h4.rotate_left(11)).wrapping_mul(K);
        h1 = (h1 ^ h5.rotate_left(19)).wrapping_mul(K);
        h2 = (h2 ^ h6.rotate_left(31)).wrapping_mul(K);
        h3 = (h3 ^ h7.rotate_left(47)).wrapping_mul(K);
    }

    // Mid loop: same absorb as the bulk loop, 4 lanes over 32-byte
    // blocks; wp chains in from the bulk loop.
    while l >= 32 {
        stripe!(h0, off);
        stripe!(h1, off + 8);
        stripe!(h2, off + 16);
        stripe!(h3, off + 24);
        off += 32;
        l -= 32;
    }

    // Absorb the final loop stripe's dangling rotated copy so
    // difference ladders cannot march off the end of the input.
    h0 = h0.wrapping_add(wp.rotate_left(27));

    // 0..31 bytes left.
    if l > 16 {
        h0 = h0.wrapping_add(injp(key, off)).wrapping_mul(K);
        h1 = h1.wrapping_add(injp(key, off + 8)).wrapping_mul(K);
        l -= 16;
    }
    // 0..16 bytes left; total length > 16, so reading the last 16
    // input bytes (overlapping already-hashed data) is always valid.
    // Length is already folded into every lane via `s`.
    if l > 0 {
        h2 = h2.wrapping_add(injp(key, len - 16)).wrapping_mul(K);
        h3 = h3.wrapping_add(injp(key, len - 8)).wrapping_mul(K);
    }

    let t0 = (h0 ^ h1.rotate_left(13)).wrapping_mul(K);
    let t1 = (h2 ^ h3.rotate_left(33)).wrapping_mul(K);
    fmix(s ^ t0 ^ t1.rotate_left(29))
}
