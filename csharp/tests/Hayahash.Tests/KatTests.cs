/*
 * Conformance tests against the C reference implementation.
 *
 * The vector table is generated from hayahash.h; regenerate with the
 * same key-byte formula if the algorithm ever changes on purpose.
 */

using System.Buffers.Binary;
using System.Text;
using Xunit;

namespace Hayahash.Tests;

public class KatTests
{
    // Deterministic key material shared with the C generator:
    // byte(i) = (i*K + 0x2545F4914F6CDD1D) >> 56.
    private static byte ByteAt(ulong i)
        => (byte)((i * 0x9E3779B97F4A7C15UL + 0x2545F4914F6CDD1DUL) >> 56);

    private static byte[] KatBuffer()
    {
        byte[] buf = new byte[1024];
        for (int i = 0; i < buf.Length; i++)
        {
            buf[i] = ByteAt((ulong)i);
        }
        return buf;
    }

    // {input length, seed, expected digest} triples generated from the
    // C reference on a little-endian host. Lengths cover every dispatch
    // path: empty, 1..3 byte, 4..7 byte, 8..16 byte, tail-only,
    // mid-loop, and bulk-loop (>= 320) inputs, including all boundary
    // values.
    private static readonly (int Len, ulong Seed, ulong Digest)[] Vectors =
    [
        (0, 0x0000000000000000UL, 0x68AC507CF298CA3FUL),
        (0, 0x9E3779B97F4A7C15UL, 0xC4F85F43D5A9985EUL),
        (0, 0xDEADBEEFCAFEBABEUL, 0x7EDC9F1B603B7337UL),
        (1, 0x0000000000000000UL, 0x7EC9660A48395D15UL),
        (1, 0x9E3779B97F4A7C15UL, 0x4D49AADD61BED986UL),
        (1, 0xDEADBEEFCAFEBABEUL, 0x8E456FA77805E810UL),
        (2, 0x0000000000000000UL, 0x3AE1E83A68B10976UL),
        (2, 0x9E3779B97F4A7C15UL, 0xE27ACD9CD85250AEUL),
        (2, 0xDEADBEEFCAFEBABEUL, 0xB64D67091596299DUL),
        (3, 0x0000000000000000UL, 0x10E8B8FEA2D42E52UL),
        (3, 0x9E3779B97F4A7C15UL, 0x377E32D405528932UL),
        (3, 0xDEADBEEFCAFEBABEUL, 0xDCC0CB335DC1DE4BUL),
        (4, 0x0000000000000000UL, 0x3FF33333AEEA0226UL),
        (4, 0x9E3779B97F4A7C15UL, 0x7BB1267AF5779B6BUL),
        (4, 0xDEADBEEFCAFEBABEUL, 0x321409D41F3F0597UL),
        (5, 0x0000000000000000UL, 0x37EE1F8B5A98B84BUL),
        (5, 0x9E3779B97F4A7C15UL, 0xBEF801DDD997C630UL),
        (5, 0xDEADBEEFCAFEBABEUL, 0x169255793C689422UL),
        (6, 0x0000000000000000UL, 0x7C024E9BC939E745UL),
        (6, 0x9E3779B97F4A7C15UL, 0xDBE499DF16AF6C98UL),
        (6, 0xDEADBEEFCAFEBABEUL, 0x38A42A135D5BFBC6UL),
        (7, 0x0000000000000000UL, 0x8D33EEB37AEA4269UL),
        (7, 0x9E3779B97F4A7C15UL, 0x5E24209A2FD00B2CUL),
        (7, 0xDEADBEEFCAFEBABEUL, 0xF47AB25F56BDC3D7UL),
        (8, 0x0000000000000000UL, 0xA7E6D3110DA23914UL),
        (8, 0x9E3779B97F4A7C15UL, 0xCCB63E92CE9F688BUL),
        (8, 0xDEADBEEFCAFEBABEUL, 0x18665C87C237153DUL),
        (9, 0x0000000000000000UL, 0x09C8DFCA0C41DA5CUL),
        (9, 0x9E3779B97F4A7C15UL, 0xE75FFD2D1E883756UL),
        (9, 0xDEADBEEFCAFEBABEUL, 0xE5EE2BB71F19DE1BUL),
        (10, 0x0000000000000000UL, 0xE28B66FB1E4CB4EAUL),
        (10, 0x9E3779B97F4A7C15UL, 0x2A004FEA465884CEUL),
        (10, 0xDEADBEEFCAFEBABEUL, 0x0418428D16CA9A24UL),
        (11, 0x0000000000000000UL, 0x12778E6F25C1D32AUL),
        (11, 0x9E3779B97F4A7C15UL, 0x48188C4BED9A1E46UL),
        (11, 0xDEADBEEFCAFEBABEUL, 0xAD02CB6AE9D55E5BUL),
        (12, 0x0000000000000000UL, 0x48D755EBE2679385UL),
        (12, 0x9E3779B97F4A7C15UL, 0x011E9FD88E5940FEUL),
        (12, 0xDEADBEEFCAFEBABEUL, 0x8180264C7B1768A9UL),
        (13, 0x0000000000000000UL, 0x381A61980D756222UL),
        (13, 0x9E3779B97F4A7C15UL, 0x64B7FC904BBC58B3UL),
        (13, 0xDEADBEEFCAFEBABEUL, 0x64C9FE2B9160C2CAUL),
        (14, 0x0000000000000000UL, 0xC71A2DB50E6448EBUL),
        (14, 0x9E3779B97F4A7C15UL, 0x32011605ED340D8CUL),
        (14, 0xDEADBEEFCAFEBABEUL, 0x3A8F543C0F65C501UL),
        (15, 0x0000000000000000UL, 0x9A8920A57F119D6BUL),
        (15, 0x9E3779B97F4A7C15UL, 0x5572E81BAB3953FFUL),
        (15, 0xDEADBEEFCAFEBABEUL, 0x7E1EB5F7F4A597F0UL),
        (16, 0x0000000000000000UL, 0xE1AF813939BA1A9EUL),
        (16, 0x9E3779B97F4A7C15UL, 0x72EF22A0197AC7E6UL),
        (16, 0xDEADBEEFCAFEBABEUL, 0x15DDB774F1BECBF7UL),
        (17, 0x0000000000000000UL, 0xEB0531E9E3A3BEBEUL),
        (17, 0x9E3779B97F4A7C15UL, 0x0F6DFA98935233F7UL),
        (17, 0xDEADBEEFCAFEBABEUL, 0x3F070CC2B4422BA0UL),
        (20, 0x0000000000000000UL, 0xC311E14FF31FB2BFUL),
        (20, 0x9E3779B97F4A7C15UL, 0xAC8DC0FD5673D897UL),
        (20, 0xDEADBEEFCAFEBABEUL, 0x8B748E4515D7C27FUL),
        (24, 0x0000000000000000UL, 0x9A64D93E28CB5DA0UL),
        (24, 0x9E3779B97F4A7C15UL, 0xDD2D9A95B8088061UL),
        (24, 0xDEADBEEFCAFEBABEUL, 0xBE8112B0F103E6C5UL),
        (31, 0x0000000000000000UL, 0x95D2421945AEC7A1UL),
        (31, 0x9E3779B97F4A7C15UL, 0xCD63F6F92AE5BA34UL),
        (31, 0xDEADBEEFCAFEBABEUL, 0x1E915F729DA2021AUL),
        (32, 0x0000000000000000UL, 0xCBD35DAB7AD91CE4UL),
        (32, 0x9E3779B97F4A7C15UL, 0x4E5482C9BC55AC72UL),
        (32, 0xDEADBEEFCAFEBABEUL, 0xEFBFF5D3A7172762UL),
        (33, 0x0000000000000000UL, 0x134D1F8689BF729CUL),
        (33, 0x9E3779B97F4A7C15UL, 0x02F60A6383C9BEA7UL),
        (33, 0xDEADBEEFCAFEBABEUL, 0x51DE032C8DA94D2FUL),
        (47, 0x0000000000000000UL, 0x854A0E1FB80DC713UL),
        (47, 0x9E3779B97F4A7C15UL, 0x9C87C14BFAD5F65DUL),
        (47, 0xDEADBEEFCAFEBABEUL, 0x8DA40D3A16F8FBF1UL),
        (48, 0x0000000000000000UL, 0x6B6B8CAA3DDB2A68UL),
        (48, 0x9E3779B97F4A7C15UL, 0xFEFAE7ADD93696A6UL),
        (48, 0xDEADBEEFCAFEBABEUL, 0xF3DECF00052380B1UL),
        (63, 0x0000000000000000UL, 0x7FD21B276D3862D5UL),
        (63, 0x9E3779B97F4A7C15UL, 0xF8571E24784C85B0UL),
        (63, 0xDEADBEEFCAFEBABEUL, 0x68B11FACBCA125F5UL),
        (64, 0x0000000000000000UL, 0x8D2CE2017D1ECCEBUL),
        (64, 0x9E3779B97F4A7C15UL, 0x257D3EE25843F04BUL),
        (64, 0xDEADBEEFCAFEBABEUL, 0x71AA83B0D836F52DUL),
        (65, 0x0000000000000000UL, 0xA521C43309772CDEUL),
        (65, 0x9E3779B97F4A7C15UL, 0xFCD59327E5C4F6DDUL),
        (65, 0xDEADBEEFCAFEBABEUL, 0x2D7D45F44C1829D0UL),
        (96, 0x0000000000000000UL, 0x0E456A468AC7355BUL),
        (96, 0x9E3779B97F4A7C15UL, 0xE5F760FC0C083B17UL),
        (96, 0xDEADBEEFCAFEBABEUL, 0xD3B493D06042DC09UL),
        (127, 0x0000000000000000UL, 0x4907F10A034954D1UL),
        (127, 0x9E3779B97F4A7C15UL, 0x1D907A46A134AB8EUL),
        (127, 0xDEADBEEFCAFEBABEUL, 0x350DB49C244548A7UL),
        (128, 0x0000000000000000UL, 0xEECEEE2B8790729DUL),
        (128, 0x9E3779B97F4A7C15UL, 0x8DED815CA5788588UL),
        (128, 0xDEADBEEFCAFEBABEUL, 0xC9FF25BFDE22A5C7UL),
        (191, 0x0000000000000000UL, 0xB9E354ABAF76CDA3UL),
        (191, 0x9E3779B97F4A7C15UL, 0x8AB844BBF8DF6893UL),
        (191, 0xDEADBEEFCAFEBABEUL, 0x0A1934AE61772E91UL),
        (192, 0x0000000000000000UL, 0x0503FD18DB80FFFFUL),
        (192, 0x9E3779B97F4A7C15UL, 0xEDCACF5231FFEDF9UL),
        (192, 0xDEADBEEFCAFEBABEUL, 0xE6357993D5CAFBD4UL),
        (255, 0x0000000000000000UL, 0x1D0EE105FC8EE266UL),
        (255, 0x9E3779B97F4A7C15UL, 0x9EADEAF2612E6B65UL),
        (255, 0xDEADBEEFCAFEBABEUL, 0x674B0232E3BA8AFBUL),
        (319, 0x0000000000000000UL, 0x8F078F3394AC0EEBUL),
        (319, 0x9E3779B97F4A7C15UL, 0x0672714B8B89EAF4UL),
        (319, 0xDEADBEEFCAFEBABEUL, 0x66F3ECD10E74B602UL),
        (320, 0x0000000000000000UL, 0xF4BCF4FA135AABFEUL),
        (320, 0x9E3779B97F4A7C15UL, 0x6F86504F4C61F014UL),
        (320, 0xDEADBEEFCAFEBABEUL, 0xF0563F11BE6D85C7UL),
        (321, 0x0000000000000000UL, 0x68868A120FB9CEF6UL),
        (321, 0x9E3779B97F4A7C15UL, 0x58B97AFD4ADA0656UL),
        (321, 0xDEADBEEFCAFEBABEUL, 0xEDF253CD5A32819CUL),
        (383, 0x0000000000000000UL, 0x762CF976C6FFBA80UL),
        (383, 0x9E3779B97F4A7C15UL, 0x738E8B85886F0EAFUL),
        (383, 0xDEADBEEFCAFEBABEUL, 0x368281E467A93A0EUL),
        (512, 0x0000000000000000UL, 0xDFBF7FC9292FF7FFUL),
        (512, 0x9E3779B97F4A7C15UL, 0x153A2FC22ACBAEA6UL),
        (512, 0xDEADBEEFCAFEBABEUL, 0x2E9B7D29F46F8552UL),
        (1023, 0x0000000000000000UL, 0x2578244C81138967UL),
        (1023, 0x9E3779B97F4A7C15UL, 0xB2C3356B389D297DUL),
        (1023, 0xDEADBEEFCAFEBABEUL, 0x125EAA99FBAE5C4DUL),
        (1024, 0x0000000000000000UL, 0x951BE6CF3BC7CF43UL),
        (1024, 0x9E3779B97F4A7C15UL, 0x3460EEC64AA4799FUL),
        (1024, 0xDEADBEEFCAFEBABEUL, 0xC93D1F81FD51336AUL),
    ];

    [Fact]
    public void MatchesCReference()
    {
        byte[] buf = KatBuffer();
        foreach (var (len, seed, digest) in Vectors)
        {
            string at = $"len={len} seed=0x{seed:x16}";
            Assert.Equal(digest, Hayahash.Hash64(buf.AsSpan(0, len), seed));
            Assert.Equal(digest, Hayahash.Hash64(buf, 0, len, seed));
        }
    }

    // Reproduces SMHasher3's self-test: hash the key prefix of length i
    // with seed 256-i for i in 0..=255, concatenating the little-endian
    // digests (key byte i is set to i after round i), then hash that
    // buffer with seed 0. The low 32 bits must match the registered
    // verification value.
    [Fact]
    public void Smhasher3VerificationValue()
    {
        byte[] key = new byte[256];
        byte[] hashes = new byte[256 * 8];
        for (int i = 0; i < 256; i++)
        {
            ulong h = Hayahash.Hash64(key.AsSpan(0, i), (ulong)(256 - i));
            BinaryPrimitives.WriteUInt64LittleEndian(hashes.AsSpan(i * 8), h);
            key[i] = (byte)i;
        }
        ulong total = Hayahash.Hash64(hashes, 0);
        Assert.Equal(0x65F2AC15U, (uint)total);
    }

    [Fact]
    public void HelloWorldExample()
    {
        byte[] input = Encoding.UTF8.GetBytes("hello world");
        Assert.Equal(0x4524B96611BFC05AUL, Hayahash.Hash64(input, 0));
    }

    [Fact]
    public void RangeOverloadMatchesCopy()
    {
        byte[] buf = KatBuffer();
        int[] offsets = [0, 1, 7, 13, 64, 401];
        int[] lengths = [0, 1, 2, 3, 5, 8, 13, 16, 17, 31, 32, 33, 63, 64, 65, 127, 319, 320, 321, 512];
        ulong[] seeds = [0x0000000000000000UL, 0x9E3779B97F4A7C15UL, 0xDEADBEEFCAFEBABEUL];
        foreach (int off in offsets)
        {
            foreach (int len in lengths)
            {
                if (off + len > buf.Length)
                {
                    continue;
                }
                byte[] copy = buf.AsSpan(off, len).ToArray();
                foreach (ulong seed in seeds)
                {
                    Assert.Equal(
                        Hayahash.Hash64(copy, seed),
                        Hayahash.Hash64(buf, off, len, seed));
                }
            }
        }
    }

    [Fact]
    public void RangeOverloadChecksBounds()
    {
        byte[] buf = new byte[8];
        Assert.ThrowsAny<ArgumentException>(() => Hayahash.Hash64(buf, 1, 8, 0));
        Assert.ThrowsAny<ArgumentException>(() => Hayahash.Hash64(buf, -1, 4, 0));
        Assert.ThrowsAny<ArgumentException>(() => Hayahash.Hash64(buf, 0, -1, 0));
        Assert.ThrowsAny<ArgumentException>(() => Hayahash.Hash64(buf, 9, 0, 0));
        Assert.Throws<ArgumentNullException>(() => Hayahash.Hash64(null!, 0, 0, 0));
    }
}
