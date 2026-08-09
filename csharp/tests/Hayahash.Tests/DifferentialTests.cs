/* Nightly differential conformance against a randomized C-reference corpus. */

using System.Buffers.Binary;
using System.Text;
using Xunit;

namespace Hayahash.Tests;

public class DifferentialTests
{
    [Fact]
    public void RandomizedCReferenceCorpus()
    {
        string? path = Environment.GetEnvironmentVariable("HAYAHASH_CORPUS");
        if (string.IsNullOrEmpty(path))
        {
            Console.Error.WriteLine("HAYAHASH_CORPUS is unset; skipping nightly differential corpus");
            return;
        }

        byte[] corpus = File.ReadAllBytes(path);
        int cursor = 0;

        void RequireRemaining(int bytes)
        {
            if (bytes < 0 || corpus.Length - cursor < bytes)
            {
                Assert.Fail($"truncated differential corpus at byte {cursor}");
            }
        }

        RequireRemaining(8);
        Assert.Equal("HAYAFZ02", Encoding.ASCII.GetString(corpus, cursor, 8));
        cursor += 8;
        RequireRemaining(12);
        uint caseCount = BinaryPrimitives.ReadUInt32LittleEndian(corpus.AsSpan(cursor));
        cursor += 4;
        ulong prngSeed = BinaryPrimitives.ReadUInt64LittleEndian(corpus.AsSpan(cursor));
        cursor += 8;

        for (uint caseIndex = 0; caseIndex < caseCount; caseIndex++)
        {
            RequireRemaining(28);
            int length = BinaryPrimitives.ReadInt32LittleEndian(corpus.AsSpan(cursor));
            cursor += 4;
            ulong hashSeed = BinaryPrimitives.ReadUInt64LittleEndian(corpus.AsSpan(cursor));
            cursor += 8;
            ulong expectedLo = BinaryPrimitives.ReadUInt64LittleEndian(corpus.AsSpan(cursor));
            cursor += 8;
            ulong expectedHi = BinaryPrimitives.ReadUInt64LittleEndian(corpus.AsSpan(cursor));
            cursor += 8;
            RequireRemaining(length);
            int inputOffset = cursor;
            Digest128 actual = Hayahash.Hash128(corpus, inputOffset, length, hashSeed);
            cursor = inputOffset + length;
            Assert.True(
                expectedLo == actual.Lo && expectedHi == actual.Hi,
                $"case={caseIndex} len={length} hash_seed=0x{hashSeed:x16} corpus_prng_seed=0x{prngSeed:x16}");
            Assert.Equal(Hayahash.Hash64(corpus, inputOffset, length, hashSeed), actual.Lo);
        }

        Assert.Equal(0, corpus.Length - cursor);
        Console.Error.WriteLine(
            $"C# matched {caseCount} C-reference cases (corpus PRNG seed=0x{prngSeed:x16})");
    }
}
