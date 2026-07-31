/*
 * hayahash64 - small, fast, portable 64-bit hash function.
 *
 * This is free and unencumbered software released into the public
 * domain. For more information, please refer to https://unlicense.org/
 */
package io.github.thevilledev.hayahash;

import java.lang.invoke.MethodHandles;
import java.lang.invoke.VarHandle;
import java.nio.ByteOrder;
import java.util.Objects;

/**
 * hayahash64, a small, fast, portable 64-bit hash function.
 *
 * <p>This is a bit-exact Java port of the reference C implementation
 * ({@code hayahash.h} at the repository root); see that header for the
 * full design notes. Output is identical for every input and seed on
 * every platform, and is independent of host endianness.
 *
 * <p>The reference's portability rules (no SIMD, no 128-bit multiply,
 * no hardware-specific instructions) map directly onto cheap JVM
 * primitives: Java {@code long} arithmetic wraps modulo 2<sup>64</sup>
 * exactly like C {@code uint64_t}, {@link Long#rotateLeft} and
 * {@code >>>} cover the rotations and logical shifts, and loads go
 * through little-endian {@link VarHandle} byte-array views, which
 * HotSpot compiles to single load instructions.
 *
 * <p>All methods are static, allocation-free, and thread-safe.
 */
public final class Hayahash {

    // Little-endian views over byte[]; plain get needs no alignment.
    private static final VarHandle LONG_LE =
            MethodHandles.byteArrayViewVarHandle(long[].class, ByteOrder.LITTLE_ENDIAN);
    private static final VarHandle INT_LE =
            MethodHandles.byteArrayViewVarHandle(int[].class, ByteOrder.LITTLE_ENDIAN);

    // Multiplier: 2^64 / golden ratio, odd.
    private static final long K = 0x9E3779B97F4A7C15L;

    // moremur finalizer constants (Pelle Evensen); M1 doubles as the
    // second multiplier of the short path.
    private static final long M1 = 0x3C79AC492BA7B653L;
    private static final long M2 = 0x1C69B3F74AC4AE35L;

    // Input length (in bytes) at or above which the 8-lane bulk loop
    // kicks in.
    private static final int BULK_MIN = 320;

    private Hayahash() {
    }

    /**
     * Hashes {@code key} with {@code seed}, returning a 64-bit digest.
     *
     * <p>Produces exactly the same value as the C reference
     * {@code hayahash64()} for all inputs, seeds, and host
     * endiannesses.
     *
     * @param key the bytes to hash
     * @param seed the seed
     * @return the 64-bit digest
     */
    public static long hash64(byte[] key, long seed) {
        return hashImpl(key, 0, key.length, seed);
    }

    /**
     * Hashes the {@code length} bytes of {@code key} starting at
     * {@code offset} with {@code seed}, returning a 64-bit digest.
     *
     * <p>Equivalent to hashing a copy of that range, without the copy;
     * useful when keys live inside larger buffers.
     *
     * @param key the array holding the bytes to hash
     * @param offset the index of the first byte to hash
     * @param length the number of bytes to hash
     * @param seed the seed
     * @return the 64-bit digest
     * @throws IndexOutOfBoundsException if {@code [offset, offset + length)}
     *     is not a valid range of {@code key}
     */
    public static long hash64(byte[] key, int offset, int length, long seed) {
        Objects.checkFromIndexSize(offset, length, key.length);
        return hashImpl(key, offset, length, seed);
    }

    private static long hashImpl(byte[] key, int off, int len, long seed) {
        // Seed & length premix; feeds every path so length extension and
        // overlapping tail reads can never collide across lengths.
        long s = seed ^ (long) len * K;
        if (len <= 16) {
            return hashShort(key, off, len, s);
        }
        return hashLong(key, off, len, s);
    }

    // Inputs of at most 16 bytes: two independent multiplies (one per
    // loaded word), then a strong finalizer; see the C header for why
    // each word gets its own injection.
    private static long hashShort(byte[] key, int off, int len, long s) {
        long a;
        long b;
        if (len >= 8) {
            a = load64(key, off);
            b = load64(key, off + len - 8);
        } else if (len >= 4) {
            a = load32(key, off);
            b = load32(key, off + len - 4);
        } else if (len > 0) {
            // 1..3 bytes: head, middle, tail (ChibiHash v2 trick).
            a = Byte.toUnsignedLong(key[off]);
            b = Byte.toUnsignedLong(key[off + (len >> 1)]) << 8
                    | Byte.toUnsignedLong(key[off + len - 1]) << 16;
        } else {
            a = 0;
            b = 0;
        }
        long x = (inj(a) ^ s ^ K) * K;
        long y = (inj2(b) ^ Long.rotateLeft(s, 23) ^ (K >>> 19)) * M1;
        return fmix(Long.rotateLeft(x, 27) ^ y);
    }

    // Inputs over 16 bytes: 8-lane bulk loop over 64-byte blocks, then
    // a 4-lane mid loop over 32-byte blocks, then overlapping tail
    // reads. Each lane absorbs t = w + rotl(wp, 27), where wp is the
    // previous stripe (chained across lanes, blocks, and the bulk/mid
    // boundary): h' = (h ^ t) * K.
    private static long hashLong(byte[] key, int off, int len, long s) {
        long h0 = s ^ K;
        long h1 = Long.rotateLeft(s, 17) + (K << 21);
        long h2 = Long.rotateLeft(s, 34) ^ (K >>> 13);
        long h3 = Long.rotateLeft(s, 51) + (K << 42);
        long w;
        long wp = 0;
        int p = off;
        int l = len;

        if (l >= BULK_MIN) {
            long h4 = s + (K >>> 27);
            long h5 = Long.rotateLeft(s, 13) ^ (K << 9);
            long h6 = Long.rotateLeft(s, 26) + (K >>> 40);
            long h7 = Long.rotateLeft(s, 39) ^ (K << 30);
            do {
                w = load64(key, p);
                h0 = (h0 ^ (w + Long.rotateLeft(wp, 27))) * K;
                wp = w;
                w = load64(key, p + 8);
                h1 = (h1 ^ (w + Long.rotateLeft(wp, 27))) * K;
                wp = w;
                w = load64(key, p + 16);
                h2 = (h2 ^ (w + Long.rotateLeft(wp, 27))) * K;
                wp = w;
                w = load64(key, p + 24);
                h3 = (h3 ^ (w + Long.rotateLeft(wp, 27))) * K;
                wp = w;
                w = load64(key, p + 32);
                h4 = (h4 ^ (w + Long.rotateLeft(wp, 27))) * K;
                wp = w;
                w = load64(key, p + 40);
                h5 = (h5 ^ (w + Long.rotateLeft(wp, 27))) * K;
                wp = w;
                w = load64(key, p + 48);
                h6 = (h6 ^ (w + Long.rotateLeft(wp, 27))) * K;
                wp = w;
                w = load64(key, p + 56);
                h7 = (h7 ^ (w + Long.rotateLeft(wp, 27))) * K;
                wp = w;
                // Checkpoint the raw-word chain once per block so a
                // 64-stripe rotation orbit cannot hide a difference.
                h0 += wp;
                p += 64;
                l -= 64;
            } while (l >= 64);
            // Fold the upper lanes in with xor + multiply: xor merges
            // of multiply spreads can only cancel by carry-pattern
            // luck, never exactly.
            h0 = (h0 ^ Long.rotateLeft(h4, 11)) * K;
            h1 = (h1 ^ Long.rotateLeft(h5, 19)) * K;
            h2 = (h2 ^ Long.rotateLeft(h6, 31)) * K;
            h3 = (h3 ^ Long.rotateLeft(h7, 47)) * K;
        }

        // Mid loop: same absorb as the bulk loop, 4 lanes over 32-byte
        // blocks; wp chains in from the bulk loop.
        for (; l >= 32; l -= 32, p += 32) {
            w = load64(key, p);
            h0 = (h0 ^ (w + Long.rotateLeft(wp, 27))) * K;
            wp = w;
            w = load64(key, p + 8);
            h1 = (h1 ^ (w + Long.rotateLeft(wp, 27))) * K;
            wp = w;
            w = load64(key, p + 16);
            h2 = (h2 ^ (w + Long.rotateLeft(wp, 27))) * K;
            wp = w;
            w = load64(key, p + 24);
            h3 = (h3 ^ (w + Long.rotateLeft(wp, 27))) * K;
            wp = w;
        }

        // Absorb the final loop stripe's dangling rotated copy so
        // difference ladders cannot march off the end of the input.
        h0 += Long.rotateLeft(wp, 27);

        // 0..31 bytes left.
        if (l > 16) {
            h0 = (h0 + injAt(key, p)) * K;
            h1 = (h1 + injAt(key, p + 8)) * K;
            p += 16;
            l -= 16;
        }
        // 0..16 bytes left; total length > 16, so reading the last 16
        // input bytes (overlapping already-hashed data) is always
        // valid. Length is already folded into every lane via s.
        if (l > 0) {
            h2 = (h2 + injAt(key, p + l - 16)) * K;
            h3 = (h3 + injAt(key, p + l - 8)) * K;
        }

        long t0 = (h0 ^ Long.rotateLeft(h1, 13)) * K;
        long t1 = (h2 ^ Long.rotateLeft(h3, 33)) * K;
        long x = s ^ t0 ^ Long.rotateLeft(t1, 29);
        // The long path has already mixed every byte through a
        // multiply; one final multiply is enough to avalanche lanes.
        x ^= x >>> 37;
        x *= K;
        return x ^ (x >>> 32);
    }

    // moremur finalizer (Pelle Evensen).
    private static long fmix(long x) {
        x ^= x >>> 27;
        x *= M1;
        x ^= x >>> 33;
        x *= M2;
        return x ^ (x >>> 27);
    }

    // inj and inj2 are bijective stripe injections (any odd number of
    // rotation terms is invertible over GF(2)). The short path uses a
    // second injection with different rotation amounts for its b word
    // so the two multiply terms can never be erased simultaneously by
    // one sparse difference.
    private static long inj(long w) {
        return w ^ Long.rotateLeft(w, 21) ^ Long.rotateLeft(w, 41);
    }

    private static long inj2(long w) {
        return w ^ Long.rotateLeft(w, 11) ^ Long.rotateLeft(w, 50);
    }

    private static long injAt(byte[] key, int i) {
        return inj(load64(key, i));
    }

    private static long load64(byte[] key, int i) {
        return (long) LONG_LE.get(key, i);
    }

    private static long load32(byte[] key, int i) {
        return Integer.toUnsignedLong((int) INT_LE.get(key, i));
    }
}
