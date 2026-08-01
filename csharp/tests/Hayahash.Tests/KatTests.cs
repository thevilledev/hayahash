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
        (0, 0x0000000000000000UL, 0xC4F85F43D5A9985EUL),
        (0, 0x9E3779B97F4A7C15UL, 0x68AC507CF298CA3FUL),
        (0, 0xDEADBEEFCAFEBABEUL, 0x4B9E2D31E2F3BE1AUL),
        (1, 0x0000000000000000UL, 0x01A6BDFC8C3D62DBUL),
        (1, 0x9E3779B97F4A7C15UL, 0x9D50F14E915849CEUL),
        (1, 0xDEADBEEFCAFEBABEUL, 0xCCCC62AC48D47C24UL),
        (2, 0x0000000000000000UL, 0xDADF7DA717BFE154UL),
        (2, 0x9E3779B97F4A7C15UL, 0xC89A133687D95091UL),
        (2, 0xDEADBEEFCAFEBABEUL, 0x30F0C4A46F6AD806UL),
        (3, 0x0000000000000000UL, 0xEA21F17856742557UL),
        (3, 0x9E3779B97F4A7C15UL, 0xE58B6749C35ABE5CUL),
        (3, 0xDEADBEEFCAFEBABEUL, 0x6D72E08B53427EE0UL),
        (4, 0x0000000000000000UL, 0x7C58CF3C18F9B496UL),
        (4, 0x9E3779B97F4A7C15UL, 0x99AAF1833279AA9DUL),
        (4, 0xDEADBEEFCAFEBABEUL, 0xA1B22F8158CFD63FUL),
        (5, 0x0000000000000000UL, 0x517724B9853C566CUL),
        (5, 0x9E3779B97F4A7C15UL, 0x250ACB4274262E38UL),
        (5, 0xDEADBEEFCAFEBABEUL, 0x31760EE78A0023EFUL),
        (6, 0x0000000000000000UL, 0xE1D10A4FBBE1DBC7UL),
        (6, 0x9E3779B97F4A7C15UL, 0xC93F66BF5463FFF0UL),
        (6, 0xDEADBEEFCAFEBABEUL, 0x8C20FD5F52DCE3CFUL),
        (7, 0x0000000000000000UL, 0x56A3989BDDD7CBE5UL),
        (7, 0x9E3779B97F4A7C15UL, 0xD7D254EE5D533404UL),
        (7, 0xDEADBEEFCAFEBABEUL, 0xC1D3AB0CC35936B9UL),
        (8, 0x0000000000000000UL, 0xDFD74858772B0E91UL),
        (8, 0x9E3779B97F4A7C15UL, 0x9BB51F7D28C94379UL),
        (8, 0xDEADBEEFCAFEBABEUL, 0x600552A3FF515FAEUL),
        (9, 0x0000000000000000UL, 0x0D246B6B3127404EUL),
        (9, 0x9E3779B97F4A7C15UL, 0x96A02B773AF52564UL),
        (9, 0xDEADBEEFCAFEBABEUL, 0x691BF2C6A9EEF2E0UL),
        (10, 0x0000000000000000UL, 0x940CED4143CCCB2BUL),
        (10, 0x9E3779B97F4A7C15UL, 0x6D653DB391FBCACCUL),
        (10, 0xDEADBEEFCAFEBABEUL, 0x24169BD3021086ACUL),
        (11, 0x0000000000000000UL, 0x00F4C1EB06392F40UL),
        (11, 0x9E3779B97F4A7C15UL, 0xA239C14889BA224AUL),
        (11, 0xDEADBEEFCAFEBABEUL, 0x29D070B34AF489CEUL),
        (12, 0x0000000000000000UL, 0x35985EE7C1DFC292UL),
        (12, 0x9E3779B97F4A7C15UL, 0xC65A041E835E8250UL),
        (12, 0xDEADBEEFCAFEBABEUL, 0x310DFF6EFCE0F6BDUL),
        (13, 0x0000000000000000UL, 0xFEA35C4B388EC02BUL),
        (13, 0x9E3779B97F4A7C15UL, 0xAA3CEFED2E20869EUL),
        (13, 0xDEADBEEFCAFEBABEUL, 0xF478BC0F8259144CUL),
        (14, 0x0000000000000000UL, 0x08524E7E5AA2CE93UL),
        (14, 0x9E3779B97F4A7C15UL, 0x5D380960C8653731UL),
        (14, 0xDEADBEEFCAFEBABEUL, 0x8887680638A97434UL),
        (15, 0x0000000000000000UL, 0xE82F8F2F83E24DC3UL),
        (15, 0x9E3779B97F4A7C15UL, 0xA55CC72365471962UL),
        (15, 0xDEADBEEFCAFEBABEUL, 0xAF5A8128D9513D53UL),
        (16, 0x0000000000000000UL, 0xC6C3B656C926EF2BUL),
        (16, 0x9E3779B97F4A7C15UL, 0x348B4B44E949A4A5UL),
        (16, 0xDEADBEEFCAFEBABEUL, 0x8B444516C3FCE921UL),
        (17, 0x0000000000000000UL, 0xA90B41F0B4D835FBUL),
        (17, 0x9E3779B97F4A7C15UL, 0xEB9CF0A2219A8A79UL),
        (17, 0xDEADBEEFCAFEBABEUL, 0x2B0BCB27D533F29DUL),
        (20, 0x0000000000000000UL, 0xD71BDC0ADCA8ADF8UL),
        (20, 0x9E3779B97F4A7C15UL, 0x1DED18E5D2C6641DUL),
        (20, 0xDEADBEEFCAFEBABEUL, 0x50E620AA5892FC5EUL),
        (24, 0x0000000000000000UL, 0x6982F8BDFE69F930UL),
        (24, 0x9E3779B97F4A7C15UL, 0xFB5432EF4D039215UL),
        (24, 0xDEADBEEFCAFEBABEUL, 0xB7A4906D498A9857UL),
        (31, 0x0000000000000000UL, 0xB01CADA4E3595061UL),
        (31, 0x9E3779B97F4A7C15UL, 0xBB07CE100EF1ACE7UL),
        (31, 0xDEADBEEFCAFEBABEUL, 0xD0B87A0842A08418UL),
        (32, 0x0000000000000000UL, 0x242FD3F914303C1DUL),
        (32, 0x9E3779B97F4A7C15UL, 0x343BA63D7E4FC2F6UL),
        (32, 0xDEADBEEFCAFEBABEUL, 0x66D7EF7DA4627549UL),
        (33, 0x0000000000000000UL, 0xE87DE7C9F18E3D9DUL),
        (33, 0x9E3779B97F4A7C15UL, 0x548C908A58A6CDF1UL),
        (33, 0xDEADBEEFCAFEBABEUL, 0xC115EA8FBA551490UL),
        (47, 0x0000000000000000UL, 0x696428DD947DF322UL),
        (47, 0x9E3779B97F4A7C15UL, 0x39BE535C74585AC5UL),
        (47, 0xDEADBEEFCAFEBABEUL, 0x52811A1D01562807UL),
        (48, 0x0000000000000000UL, 0x1059B4E24C8CFDF0UL),
        (48, 0x9E3779B97F4A7C15UL, 0x643BA76560D01C09UL),
        (48, 0xDEADBEEFCAFEBABEUL, 0xA537F4069657C426UL),
        (63, 0x0000000000000000UL, 0x4471E159CA7F1AA1UL),
        (63, 0x9E3779B97F4A7C15UL, 0xE979866C4F557390UL),
        (63, 0xDEADBEEFCAFEBABEUL, 0xB026D08EC12753F1UL),
        (64, 0x0000000000000000UL, 0x62BC7D33C15657E9UL),
        (64, 0x9E3779B97F4A7C15UL, 0x0078437E7D379478UL),
        (64, 0xDEADBEEFCAFEBABEUL, 0x765229403E95673BUL),
        (65, 0x0000000000000000UL, 0xA57F09711AE70C77UL),
        (65, 0x9E3779B97F4A7C15UL, 0xA5F941F9895FAF8DUL),
        (65, 0xDEADBEEFCAFEBABEUL, 0x74F19F9600A6C10FUL),
        (96, 0x0000000000000000UL, 0x2186FDC93E032C9CUL),
        (96, 0x9E3779B97F4A7C15UL, 0x4164E090EA0C5DF9UL),
        (96, 0xDEADBEEFCAFEBABEUL, 0x967410100E8C7C8AUL),
        (127, 0x0000000000000000UL, 0xDAA46CC2E67CF5C3UL),
        (127, 0x9E3779B97F4A7C15UL, 0x1D78B9BCE14CF66FUL),
        (127, 0xDEADBEEFCAFEBABEUL, 0x7CC9599ACC50F32BUL),
        (128, 0x0000000000000000UL, 0xD4C430490D0CE9D1UL),
        (128, 0x9E3779B97F4A7C15UL, 0xAE0594746A45D322UL),
        (128, 0xDEADBEEFCAFEBABEUL, 0xC777BD88BF800192UL),
        (191, 0x0000000000000000UL, 0xAB4EB4F6A214AB26UL),
        (191, 0x9E3779B97F4A7C15UL, 0x2F01EEFE05F61681UL),
        (191, 0xDEADBEEFCAFEBABEUL, 0x0AC11D2D060A6D39UL),
        (192, 0x0000000000000000UL, 0xBD4668FD0E37A0D8UL),
        (192, 0x9E3779B97F4A7C15UL, 0xB33BD21C20D7F1EEUL),
        (192, 0xDEADBEEFCAFEBABEUL, 0x2C05041A563EFC0AUL),
        (255, 0x0000000000000000UL, 0x3F35C5137D9DDD92UL),
        (255, 0x9E3779B97F4A7C15UL, 0xB3450BEEA6A88C0EUL),
        (255, 0xDEADBEEFCAFEBABEUL, 0x10EEAFB383803642UL),
        (319, 0x0000000000000000UL, 0xFB6F356631C62298UL),
        (319, 0x9E3779B97F4A7C15UL, 0x82A92D2F3C0D3FC2UL),
        (319, 0xDEADBEEFCAFEBABEUL, 0xC187B939C37F8EC7UL),
        (320, 0x0000000000000000UL, 0xAAB4DE0105C41715UL),
        (320, 0x9E3779B97F4A7C15UL, 0x24747E138240D684UL),
        (320, 0xDEADBEEFCAFEBABEUL, 0xD908512F166E3CD2UL),
        (321, 0x0000000000000000UL, 0xD58DE26140651F72UL),
        (321, 0x9E3779B97F4A7C15UL, 0xAE8B28A6EF04CC35UL),
        (321, 0xDEADBEEFCAFEBABEUL, 0xAA4EFD9BA8E0C810UL),
        (383, 0x0000000000000000UL, 0x6FBC1354616B9257UL),
        (383, 0x9E3779B97F4A7C15UL, 0x09A754CB4921EAB0UL),
        (383, 0xDEADBEEFCAFEBABEUL, 0xF1548B3D85DA380EUL),
        (512, 0x0000000000000000UL, 0x2968882331191FDBUL),
        (512, 0x9E3779B97F4A7C15UL, 0x143C166BDE64236DUL),
        (512, 0xDEADBEEFCAFEBABEUL, 0x5E7F7FF6C918FE7FUL),
        (1023, 0x0000000000000000UL, 0xDA47A412C97502BEUL),
        (1023, 0x9E3779B97F4A7C15UL, 0x003D27583FDEE215UL),
        (1023, 0xDEADBEEFCAFEBABEUL, 0x80F98032FDB103FDUL),
        (1024, 0x0000000000000000UL, 0xD3594D0A25CB043BUL),
        (1024, 0x9E3779B97F4A7C15UL, 0xED0E2941D2F3D593UL),
        (1024, 0xDEADBEEFCAFEBABEUL, 0xF00E1771AB6A3869UL),
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
        Assert.Equal(0xF3C4A9B4U, (uint)total);
    }

    [Fact]
    public void HelloWorldExample()
    {
        byte[] input = Encoding.UTF8.GetBytes("hello world");
        Assert.Equal(0xF2172C5BD68EC576UL, Hayahash.Hash64(input, 0));
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
