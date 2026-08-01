/*
 * hayahash64 - small, fast, portable 64-bit hash function.
 *
 * This is free and unencumbered software released into the public
 * domain. For more information, please refer to https://unlicense.org/
 */

using System.Buffers.Binary;
using System.Runtime.CompilerServices;

namespace Hayahash;

/// <summary>
/// hayahash64, a small, fast, portable 64-bit hash function.
/// </summary>
/// <remarks>
/// <para>
/// This is a bit-exact .NET port of the reference C implementation
/// (<c>hayahash.h</c> at the repository root); see that header for the
/// full design notes. Output is identical for every input and seed on
/// every platform, and is independent of host endianness.
/// </para>
/// <para>
/// The reference's portability rules (no SIMD, no 128-bit multiply,
/// no hardware-specific instructions) map directly onto cheap CLR
/// primitives: <see cref="ulong"/> arithmetic wraps modulo 2^64 exactly
/// like C <c>uint64_t</c>, and loads go through little-endian
/// <see cref="BinaryPrimitives"/> readers.
/// </para>
/// <para>
/// All methods are static, allocation-free, and thread-safe.
/// </para>
/// </remarks>
public static class Hayahash
{
    // Multiplier: 2^64 / golden ratio, odd.
    private const ulong K = 0x9E3779B97F4A7C15UL;

    // moremur finalizer constants (Pelle Evensen); M1 doubles as the
    // second multiplier of the short path.
    private const ulong M1 = 0x3C79AC492BA7B653UL;
    private const ulong M2 = 0x1C69B3F74AC4AE35UL;

    // Input length (in bytes) at or above which the 8-lane bulk loop
    // kicks in.
    private const int BulkMin = 320;

    /// <summary>
    /// Hashes <paramref name="key"/> with <paramref name="seed"/>,
    /// returning a 64-bit digest.
    /// </summary>
    /// <param name="key">The bytes to hash.</param>
    /// <param name="seed">The seed.</param>
    /// <returns>The 64-bit digest.</returns>
    /// <remarks>
    /// Produces exactly the same value as the C reference
    /// <c>hayahash64()</c> for all inputs, seeds, and host
    /// endiannesses.
    /// </remarks>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static ulong Hash64(ReadOnlySpan<byte> key, ulong seed)
        => HashImpl(key, seed);

    /// <summary>
    /// Hashes the <paramref name="length"/> bytes of <paramref name="key"/>
    /// starting at <paramref name="offset"/> with <paramref name="seed"/>,
    /// returning a 64-bit digest.
    /// </summary>
    /// <param name="key">The array holding the bytes to hash.</param>
    /// <param name="offset">The index of the first byte to hash.</param>
    /// <param name="length">The number of bytes to hash.</param>
    /// <param name="seed">The seed.</param>
    /// <returns>The 64-bit digest.</returns>
    /// <exception cref="ArgumentOutOfRangeException">
    /// Thrown if <c>[offset, offset + length)</c> is not a valid range of
    /// <paramref name="key"/>.
    /// </exception>
    /// <remarks>
    /// Equivalent to hashing a copy of that range, without the copy;
    /// useful when keys live inside larger buffers.
    /// </remarks>
    public static ulong Hash64(byte[] key, int offset, int length, ulong seed)
    {
        ArgumentNullException.ThrowIfNull(key);
        return Hash64(key.AsSpan(offset, length), seed);
    }

    private static ulong HashImpl(ReadOnlySpan<byte> key, ulong seed)
    {
        // Seed & length premix; feeds every path so length extension and
        // overlapping tail reads can never collide across lengths.
        int len = key.Length;
        ulong s = seed ^ ((ulong)(uint)len * K);
        if (len <= 16)
        {
            return HashShort(key, s);
        }
        return HashLong(key, s);
    }

    // Inputs of at most 16 bytes: two independent multiplies (one per
    // loaded word), then a strong finalizer; see the C header for why
    // each word gets its own injection.
    private static ulong HashShort(ReadOnlySpan<byte> key, ulong s)
    {
        int len = key.Length;
        ulong a;
        ulong b;
        if (len >= 8)
        {
            a = Load64(key);
            b = Load64(key.Slice(len - 8));
        }
        else if (len >= 4)
        {
            a = Load32(key);
            b = Load32(key.Slice(len - 4));
        }
        else if (len > 0)
        {
            // 1..3 bytes: head, middle, tail (ChibiHash v2 trick).
            a = key[0];
            b = ((ulong)key[len >> 1] << 8) | ((ulong)key[len - 1] << 16);
        }
        else
        {
            a = 0;
            b = 0;
        }
        ulong x = (Inj(a) ^ s ^ K) * K;
        ulong y = (Inj2(b) ^ Rotl(s, 23) ^ (K >> 19)) * M1;
        return Fmix(Rotl(x, 27) ^ y);
    }

    // Inputs over 16 bytes: 8-lane bulk loop over 64-byte blocks, then
    // a 4-lane mid loop over 32-byte blocks, then overlapping tail
    // reads. Each lane absorbs t = w + rotl(wp, 27), where wp is the
    // previous stripe (chained across lanes, blocks, and the bulk/mid
    // boundary): h' = (h ^ t) * K.
    //
    // Indices stay absolute into key so the final overlapping 16-byte
    // tail can read already-consumed bytes (p + l - 16 may precede p).
    private static ulong HashLong(ReadOnlySpan<byte> key, ulong s)
    {
        ulong h0 = s ^ K;
        ulong h1 = Rotl(s, 17) + (K << 21);
        ulong h2 = Rotl(s, 34) ^ (K >> 13);
        ulong h3 = Rotl(s, 51) + (K << 42);
        ulong w;
        ulong wp = 0;
        int p = 0;
        int l = key.Length;

        if (l >= BulkMin)
        {
            ulong h4 = s + (K >> 27);
            ulong h5 = Rotl(s, 13) ^ (K << 9);
            ulong h6 = Rotl(s, 26) + (K >> 40);
            ulong h7 = Rotl(s, 39) ^ (K << 30);
            do
            {
                w = Load64(key, p);
                h0 = (h0 ^ (w + Rotl(wp, 27))) * K;
                wp = w;
                w = Load64(key, p + 8);
                h1 = (h1 ^ (w + Rotl(wp, 27))) * K;
                wp = w;
                w = Load64(key, p + 16);
                h2 = (h2 ^ (w + Rotl(wp, 27))) * K;
                wp = w;
                w = Load64(key, p + 24);
                h3 = (h3 ^ (w + Rotl(wp, 27))) * K;
                wp = w;
                w = Load64(key, p + 32);
                h4 = (h4 ^ (w + Rotl(wp, 27))) * K;
                wp = w;
                w = Load64(key, p + 40);
                h5 = (h5 ^ (w + Rotl(wp, 27))) * K;
                wp = w;
                w = Load64(key, p + 48);
                h6 = (h6 ^ (w + Rotl(wp, 27))) * K;
                wp = w;
                w = Load64(key, p + 56);
                h7 = (h7 ^ (w + Rotl(wp, 27))) * K;
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
            h0 = (h0 ^ Rotl(h4, 11)) * K;
            h1 = (h1 ^ Rotl(h5, 19)) * K;
            h2 = (h2 ^ Rotl(h6, 31)) * K;
            h3 = (h3 ^ Rotl(h7, 47)) * K;
        }

        // Mid loop: same absorb as the bulk loop, 4 lanes over 32-byte
        // blocks; wp chains in from the bulk loop.
        for (; l >= 32; l -= 32, p += 32)
        {
            w = Load64(key, p);
            h0 = (h0 ^ (w + Rotl(wp, 27))) * K;
            wp = w;
            w = Load64(key, p + 8);
            h1 = (h1 ^ (w + Rotl(wp, 27))) * K;
            wp = w;
            w = Load64(key, p + 16);
            h2 = (h2 ^ (w + Rotl(wp, 27))) * K;
            wp = w;
            w = Load64(key, p + 24);
            h3 = (h3 ^ (w + Rotl(wp, 27))) * K;
            wp = w;
        }

        // Absorb the final loop stripe's dangling rotated copy so
        // difference ladders cannot march off the end of the input.
        h0 += Rotl(wp, 27);

        // 0..31 bytes left.
        if (l > 16)
        {
            h0 = (h0 + InjAt(key, p)) * K;
            h1 = (h1 + InjAt(key, p + 8)) * K;
            p += 16;
            l -= 16;
        }
        // 0..16 bytes left; total length > 16, so reading the last 16
        // input bytes (overlapping already-hashed data) is always
        // valid. Length is already folded into every lane via s.
        if (l > 0)
        {
            h2 = (h2 + InjAt(key, p + l - 16)) * K;
            h3 = (h3 + InjAt(key, p + l - 8)) * K;
        }

        ulong t0 = (h0 ^ Rotl(h1, 13)) * K;
        ulong t1 = (h2 ^ Rotl(h3, 33)) * K;
        ulong x = s ^ t0 ^ Rotl(t1, 29);
        // The long path has already mixed every byte through a
        // multiply; one final multiply is enough to avalanche lanes.
        x ^= x >> 37;
        x *= K;
        return x ^ (x >> 32);
    }

    // moremur finalizer (Pelle Evensen).
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    private static ulong Fmix(ulong x)
    {
        x ^= x >> 27;
        x *= M1;
        x ^= x >> 33;
        x *= M2;
        return x ^ (x >> 27);
    }

    // inj and inj2 are bijective stripe injections (any odd number of
    // rotation terms is invertible over GF(2)). The short path uses a
    // second injection with different rotation amounts for its b word
    // so the two multiply terms can never be erased simultaneously by
    // one sparse difference.
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    private static ulong Inj(ulong w)
        => w ^ Rotl(w, 21) ^ Rotl(w, 41);

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    private static ulong Inj2(ulong w)
        => w ^ Rotl(w, 11) ^ Rotl(w, 50);

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    private static ulong InjAt(ReadOnlySpan<byte> key, int i)
        => Inj(Load64(key, i));

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    private static ulong Rotl(ulong x, int n)
        => (x << n) | (x >> (64 - n));

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    private static ulong Load64(ReadOnlySpan<byte> p)
        => BinaryPrimitives.ReadUInt64LittleEndian(p);

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    private static ulong Load64(ReadOnlySpan<byte> key, int i)
        => BinaryPrimitives.ReadUInt64LittleEndian(key.Slice(i));

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    private static ulong Load32(ReadOnlySpan<byte> p)
        => BinaryPrimitives.ReadUInt32LittleEndian(p);
}
