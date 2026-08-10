// Streaming conformance: every split of an input must produce the
// one-shot digest, and digesting must not consume the state.

using System;
using System.Collections.Generic;
using Hayahash;
using Xunit;

namespace Hayahash.Tests;

public class HasherTests
{
    private const ulong K = 0x9E3779B97F4A7C15UL;

    /// <summary>The shared portable input fill used by the KAT tables.</summary>
    private static byte[] PatternA(int n)
    {
        var b = new byte[n];
        for (int i = 0; i < n; i++)
        {
            b[i] = (byte)(((ulong)i * K + 0x2545F4914F6CDD1DUL) >> 56);
        }
        return b;
    }

    // Chunk sizes that straddle the 448-byte buffer, the 128-byte keep
    // floor and the 64-byte block.
    private static readonly int[] Splits = { 1, 7, 64, 127, 448, 449, int.MaxValue };

    private static void Feed(Hasher h, byte[] data, int chunk)
    {
        for (int i = 0; i < data.Length;)
        {
            int n = Math.Min(chunk, data.Length - i);
            h.Update(data.AsSpan(i, n));
            i += n;
        }
    }

    private static IEnumerable<int> Lengths()
    {
        for (int n = 0; n <= 640; n++)
        {
            yield return n;
        }
        foreach (int n in new[] { 895, 896, 897, 1023, 1024, 1025, 4096, 20000, 131073 })
        {
            yield return n;
        }
    }

    [Theory]
    [InlineData(0UL)]
    [InlineData(K)]
    [InlineData(0xDEADBEEFCAFEBABEUL)]
    public void StreamingMatchesOneShot(ulong seed)
    {
        foreach (int n in Lengths())
        {
            byte[] data = PatternA(n);
            ulong want64 = Hayahash.Hash64(data, seed);
            Digest128 want128 = Hayahash.Hash128(data, seed);
            foreach (int chunk in Splits)
            {
                var h = new Hasher(seed);
                Feed(h, data, chunk);
                Assert.Equal(want64, h.Digest64());
                Assert.Equal(want128, h.Digest128());
                Assert.Equal(want64, h.Digest128().Lo);
            }
        }
    }

    [Fact]
    public void DigestIsNonDestructive()
    {
        const int total = 2000;
        byte[] data = PatternA(total);
        foreach (int cut in new[] { 0, 1, 63, 64, 447, 448, 449, 1000, total })
        {
            var h = new Hasher(7);
            h.Update(data, 0, cut);
            ulong first = h.Digest64();
            Assert.Equal(first, h.Digest64());
            Assert.Equal(Hayahash.Hash64(data, 0, cut, 7), first);
            Assert.Equal(first, h.Digest128().Lo);
            h.Update(data, cut, total - cut);
            Assert.Equal(Hayahash.Hash64(data, 7), h.Digest64());
        }
    }

    [Fact]
    public void EmptyAndZeroLengthUpdates()
    {
        var h = new Hasher();
        Assert.Equal(Hayahash.Hash64(Array.Empty<byte>(), 0), h.Digest64());
        h.Update(Array.Empty<byte>());
        Assert.Equal(Hayahash.Hash64(Array.Empty<byte>(), 0), h.Digest64());
        Assert.Equal(0UL, h.Length);

        byte[] data = PatternA(500);
        h.Update(data, 0, 200);
        h.Update(Array.Empty<byte>());
        h.Update(data, 200, 300);
        Assert.Equal(Hayahash.Hash64(data, 0), h.Digest64());
        Assert.Equal(500UL, h.Length);
    }

    [Fact]
    public void ResetKeepsOrReplacesSeed()
    {
        byte[] data = PatternA(1000);
        var h = new Hasher(0xABCD);
        h.Update(data);
        h.Reset();
        Assert.Equal(0UL, h.Length);
        h.Update(data, 0, 10);
        Assert.Equal(Hayahash.Hash64(data, 0, 10, 0xABCD), h.Digest64());
        h.Reset(1);
        Assert.Equal(1UL, h.Seed);
        h.Update(data, 0, 10);
        Assert.Equal(Hayahash.Hash64(data, 0, 10, 1), h.Digest64());
    }

    [Fact]
    public void UpdateChecksBounds()
    {
        var h = new Hasher();
        var data = new byte[10];
        Assert.Throws<ArgumentNullException>(() => h.Update((byte[])null!));
        Assert.Throws<ArgumentOutOfRangeException>(() => h.Update(data, -1, 1));
        Assert.Throws<ArgumentOutOfRangeException>(() => h.Update(data, 0, 11));
        Assert.Throws<ArgumentOutOfRangeException>(() => h.Update(data, 5, 6));
    }

    // Pins the "streaming equality samples" section of
    // test_vectors/v0.5.0.txt, which until now no port consumed.
    [Theory]
    [InlineData(0, 0x68AC507CF298CA3FUL)]
    [InlineData(5, 0x37EE1F8B5A98B84BUL)]
    [InlineData(10, 0xE28B66FB1E4CB4EAUL)]
    [InlineData(15, 0x9A8920A57F119D6BUL)]
    [InlineData(20, 0xC311E14FF31FB2BFUL)]
    [InlineData(25, 0xC27FDE4AC86CCE54UL)]
    [InlineData(30, 0x16CC1E65CA2CB4F3UL)]
    [InlineData(35, 0x1C6522BDC246DA12UL)]
    [InlineData(40, 0xD110128D567CB9F8UL)]
    public void PublishedStreamingVectors(int length, ulong want)
    {
        var h = new Hasher(0);
        foreach (byte b in PatternA(length))
        {
            h.Update(new[] { b });
        }
        Assert.Equal(want, h.Digest64());
    }
}
