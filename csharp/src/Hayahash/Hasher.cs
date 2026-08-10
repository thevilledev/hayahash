// hayahash streaming state - incremental hashing.
//
// This is free and unencumbered software released into the public
// domain. For more information, please refer to https://unlicense.org/

using System;

namespace Hayahash;

/// <summary>
/// Incremental hayahash state.
/// </summary>
/// <remarks>
/// <para>
/// The digest equals <see cref="Hayahash.Hash64(ReadOnlySpan{byte}, ulong)"/> or
/// <see cref="Hayahash.Hash128(ReadOnlySpan{byte}, ulong)"/> over the concatenation of
/// everything passed to <see cref="Update(ReadOnlySpan{byte})"/>, for every split
/// of that input.
/// </para>
/// <para>
/// Digesting does not consume the state, so absorbing may continue afterwards.
/// Instances are not safe for concurrent use.
/// </para>
/// </remarks>
public sealed class Hasher
{
    /// <summary>
    /// Streaming buffer size. Totals below it stay buffered so short and mid inputs
    /// take the one-shot dispatch at digest time, exactly as the C reference does.
    /// </summary>
    private const int BufCap = 448;

    /// <summary>
    /// The floor the buffer is drained to. The digest-time mid/tail phase reaches
    /// back up to 16 bytes before the current pointer, so the buffer has to retain
    /// more than that.
    /// </summary>
    private const int Keep = 128;

    private readonly ulong[] _h = new ulong[8];
    private readonly byte[] _buf = new byte[BufCap];
    private ulong _wp;
    private ulong _seed;
    private ulong _total;
    private int _nbuf;
    private bool _bulk;

    /// <summary>Creates an empty state with the given seed.</summary>
    /// <param name="seed">The 64-bit seed.</param>
    public Hasher(ulong seed = 0)
    {
        Reset(seed);
    }

    /// <summary>The seed this state was created or last reset with.</summary>
    public ulong Seed => _seed;

    /// <summary>Number of bytes absorbed so far.</summary>
    public ulong Length => _total;

    /// <summary>Discards absorbed input and restarts with a new seed.</summary>
    /// <param name="seed">The 64-bit seed.</param>
    public void Reset(ulong seed)
    {
        const ulong k = Hayahash.K;
        ulong s = seed ^ k;
        _h[0] = s ^ k;
        _h[1] = Hayahash.Rotl(s, 17) + (k << 21);
        _h[2] = Hayahash.Rotl(s, 34) ^ (k >> 13);
        _h[3] = Hayahash.Rotl(s, 51) + (k << 42);
        _h[4] = s + (k >> 27);
        _h[5] = Hayahash.Rotl(s, 13) ^ (k << 9);
        _h[6] = Hayahash.Rotl(s, 26) + (k >> 40);
        _h[7] = Hayahash.Rotl(s, 39) ^ (k << 30);
        _wp = 0;
        _seed = seed;
        _total = 0;
        _nbuf = 0;
        _bulk = false;
    }

    /// <summary>Discards absorbed input, keeping the current seed.</summary>
    public void Reset() => Reset(_seed);

    /// <summary>Absorbs <paramref name="data"/>.</summary>
    /// <param name="data">The bytes to absorb.</param>
    public void Update(ReadOnlySpan<byte> data)
    {
        if (data.IsEmpty)
        {
            return;
        }
        _total += (ulong)data.Length;

        if (!_bulk)
        {
            // Undecided between the one-shot finish and the bulk path:
            // totals up to BufCap-1 stay buffered.
            if (_nbuf + data.Length < BufCap)
            {
                data.CopyTo(_buf.AsSpan(_nbuf));
                _nbuf += data.Length;
                return;
            }
            // Total is now >= 448 > BulkMin: commit to the bulk path.
            _bulk = true;
        }

        while (true)
        {
            // Buffer at its floor with plenty incoming: drain the floor, then
            // stream whole blocks straight from the caller's span, leaving a
            // [Keep, Keep+63]-byte remainder for the buffer.
            if (_nbuf == Keep && data.Length > BufCap)
            {
                int direct = (data.Length - Keep) & ~63;
                Blocks(_buf.AsSpan(0, Keep));
                Blocks(data[..direct]);
                data = data[direct..];
                _nbuf = 0;
            }
            int take = Math.Min(BufCap - _nbuf, data.Length);
            data[..take].CopyTo(_buf.AsSpan(_nbuf));
            _nbuf += take;
            data = data[take..];
            if (_nbuf < BufCap)
            {
                break;
            }
            // Buffer full: consume whole blocks down to the keep floor.
            int consume = (_nbuf - Keep) & ~63;
            Blocks(_buf.AsSpan(0, consume));
            _nbuf -= consume;
            _buf.AsSpan(consume, _nbuf).CopyTo(_buf.AsSpan(0));
        }
    }

    /// <summary>Absorbs a byte array.</summary>
    /// <param name="data">The bytes to absorb.</param>
    /// <exception cref="ArgumentNullException">If <paramref name="data"/> is null.</exception>
    public void Update(byte[] data)
    {
        ArgumentNullException.ThrowIfNull(data);
        Update(data.AsSpan());
    }

    /// <summary>Absorbs a range of a byte array.</summary>
    /// <param name="data">The backing array.</param>
    /// <param name="offset">The first index to absorb.</param>
    /// <param name="length">The number of bytes to absorb.</param>
    /// <exception cref="ArgumentNullException">If <paramref name="data"/> is null.</exception>
    /// <exception cref="ArgumentOutOfRangeException">If the range is outside the array.</exception>
    public void Update(byte[] data, int offset, int length)
    {
        ArgumentNullException.ThrowIfNull(data);
        if (offset < 0 || length < 0 || offset > data.Length - length)
        {
            throw new ArgumentOutOfRangeException(nameof(offset));
        }
        Update(data.AsSpan(offset, length));
    }

    /// <summary>Absorbs <paramref name="p"/>, whose length must be a multiple of 64.</summary>
    private void Blocks(ReadOnlySpan<byte> p)
    {
        const ulong k = Hayahash.K;
        ulong h0 = _h[0], h1 = _h[1], h2 = _h[2], h3 = _h[3];
        ulong h4 = _h[4], h5 = _h[5], h6 = _h[6], h7 = _h[7];
        ulong w = _wp;
        for (int off = 0; off < p.Length; off += 64)
        {
            ulong v;
            v = Hayahash.Load64(p, off); h0 = (h0 ^ (v + Hayahash.Rotl(w, 27))) * k; w = v;
            v = Hayahash.Load64(p, off + 8); h1 = (h1 ^ (v + Hayahash.Rotl(w, 27))) * k; w = v;
            v = Hayahash.Load64(p, off + 16); h2 = (h2 ^ (v + Hayahash.Rotl(w, 27))) * k; w = v;
            v = Hayahash.Load64(p, off + 24); h3 = (h3 ^ (v + Hayahash.Rotl(w, 27))) * k; w = v;
            v = Hayahash.Load64(p, off + 32); h4 = (h4 ^ (v + Hayahash.Rotl(w, 27))) * k; w = v;
            v = Hayahash.Load64(p, off + 40); h5 = (h5 ^ (v + Hayahash.Rotl(w, 27))) * k; w = v;
            v = Hayahash.Load64(p, off + 48); h6 = (h6 ^ (v + Hayahash.Rotl(w, 27))) * k; w = v;
            v = Hayahash.Load64(p, off + 56); h7 = (h7 ^ (v + Hayahash.Rotl(w, 27))) * k; w = v;
            // Checkpoint the raw-word chain once per block so a 64-stripe
            // rotation orbit cannot hide a difference until it returns to
            // the same lane.
            h0 += w;
        }
        _h[0] = h0; _h[1] = h1; _h[2] = h2; _h[3] = h3;
        _h[4] = h4; _h[5] = h5; _h[6] = h6; _h[7] = h7;
        _wp = w;
    }

    /// <summary>
    /// Returns the 64-bit digest of everything absorbed so far, without consuming
    /// the state.
    /// </summary>
    /// <returns>The 64-bit digest.</returns>
    public ulong Digest64()
    {
        if (!_bulk)
        {
            return Hayahash.Hash64(_buf.AsSpan(0, (int)_total), _seed);
        }
        (ulong t0, ulong t1, ulong s) = Tail();
        ulong x = s ^ t0 ^ Hayahash.Rotl(t1, 29);
        x ^= x >> 37;
        x *= Hayahash.K;
        return x ^ (x >> 32);
    }

    /// <summary>
    /// Returns both digest words, without consuming the state. The low word is
    /// exactly <see cref="Digest64"/>.
    /// </summary>
    /// <returns>The 128-bit digest.</returns>
    public Digest128 Digest128()
    {
        if (!_bulk)
        {
            return Hayahash.Hash128(_buf.AsSpan(0, (int)_total), _seed);
        }
        (ulong t0, ulong t1, ulong s) = Tail();
        ulong x = s ^ t0 ^ Hayahash.Rotl(t1, 29);
        x ^= x >> 37;
        x *= Hayahash.K;
        return new Digest128(
            x ^ (x >> 32),
            Hayahash.Fmix128(Hayahash.Rotl(s, 32) ^ (t1 + Hayahash.Rotl(t0, 47))));
    }

    /// <summary>
    /// Continues the long path over the buffered remainder: the leftover whole
    /// blocks, then the same fold, mid round, wall and tail as the one-shot. Reads
    /// the state without mutating it.
    /// </summary>
    private (ulong T0, ulong T1, ulong S) Tail()
    {
        const ulong k = Hayahash.K;
        ulong lenmix = _total * k;
        ulong s = _seed ^ k;
        ulong h0 = _h[0], h1 = _h[1], h2 = _h[2], h3 = _h[3];
        ulong h4 = _h[4], h5 = _h[5], h6 = _h[6], h7 = _h[7];
        ulong w = _wp;
        ReadOnlySpan<byte> buf = _buf.AsSpan(0, _nbuf);
        int p = 0;
        int l = _nbuf;

        while (l >= 64)
        {
            ulong v;
            v = Hayahash.Load64(buf, p); h0 = (h0 ^ (v + Hayahash.Rotl(w, 27))) * k; w = v;
            v = Hayahash.Load64(buf, p + 8); h1 = (h1 ^ (v + Hayahash.Rotl(w, 27))) * k; w = v;
            v = Hayahash.Load64(buf, p + 16); h2 = (h2 ^ (v + Hayahash.Rotl(w, 27))) * k; w = v;
            v = Hayahash.Load64(buf, p + 24); h3 = (h3 ^ (v + Hayahash.Rotl(w, 27))) * k; w = v;
            v = Hayahash.Load64(buf, p + 32); h4 = (h4 ^ (v + Hayahash.Rotl(w, 27))) * k; w = v;
            v = Hayahash.Load64(buf, p + 40); h5 = (h5 ^ (v + Hayahash.Rotl(w, 27))) * k; w = v;
            v = Hayahash.Load64(buf, p + 48); h6 = (h6 ^ (v + Hayahash.Rotl(w, 27))) * k; w = v;
            v = Hayahash.Load64(buf, p + 56); h7 = (h7 ^ (v + Hayahash.Rotl(w, 27))) * k; w = v;
            h0 += w;
            p += 64;
            l -= 64;
        }
        h0 = (h0 ^ Hayahash.Rotl(h4, 11)) * k;
        h1 = (h1 ^ Hayahash.Rotl(h5, 19)) * k;
        h2 = (h2 ^ Hayahash.Rotl(h6, 31)) * k;
        h3 = (h3 ^ Hayahash.Rotl(h7, 47)) * k;

        if (l >= 32)
        {
            ulong v;
            v = Hayahash.Load64(buf, p); h0 = (h0 ^ (v + Hayahash.Rotl(w, 27))) * k; w = v;
            v = Hayahash.Load64(buf, p + 8); h1 = (h1 ^ (v + Hayahash.Rotl(w, 27))) * k; w = v;
            v = Hayahash.Load64(buf, p + 16); h2 = (h2 ^ (v + Hayahash.Rotl(w, 27))) * k; w = v;
            v = Hayahash.Load64(buf, p + 24); h3 = (h3 ^ (v + Hayahash.Rotl(w, 27))) * k; w = v;
            p += 32;
            l -= 32;
        }

        h0 += Hayahash.Rotl(w, 27);
        if (l > 16)
        {
            h0 = (h0 + Hayahash.InjAt(buf, p)) * k;
            h1 = (h1 + Hayahash.InjAt(buf, p + 8)) * k;
        }
        // The last 16 bytes of the stream. Keep >= 128 guarantees this
        // reach-back stays inside the buffer even when l is small.
        if (l > 0)
        {
            h2 = (h2 + Hayahash.InjAt(buf, _nbuf - 16)) * k;
            h3 = (h3 + Hayahash.InjAt(buf, _nbuf - 8)) * k;
        }

        return ((h0 ^ Hayahash.Rotl(h1, 13) ^ lenmix) * k, (h2 ^ Hayahash.Rotl(h3, 33)) * k, s);
    }
}
