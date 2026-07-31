/*
 * Conformance tests against the C reference implementation.
 *
 * The vector table is generated from hayahash.h; regenerate with the
 * same key-byte formula if the algorithm ever changes on purpose.
 */
package io.github.thevilledev.hayahash;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import org.junit.jupiter.api.Test;

class KatTest {

    // Deterministic key material shared with the C generator:
    // byte(i) = (i*K + 0x2545F4914F6CDD1D) >> 56.
    private static byte byteAt(long i) {
        return (byte) ((i * 0x9E3779B97F4A7C15L + 0x2545F4914F6CDD1DL) >>> 56);
    }

    private static byte[] katBuffer() {
        byte[] buf = new byte[1024];
        for (int i = 0; i < buf.length; i++) {
            buf[i] = byteAt(i);
        }
        return buf;
    }

    // {input length, seed, expected digest} triples generated from the
    // C reference on a little-endian host. Lengths cover every dispatch
    // path: empty, 1..3 byte, 4..7 byte, 8..16 byte, tail-only,
    // mid-loop, and bulk-loop (>= 320) inputs, including all boundary
    // values.
    private static final long[][] VECTORS = {
        {0, 0x0000000000000000L, 0xC4F85F43D5A9985EL},
        {0, 0x9E3779B97F4A7C15L, 0x68AC507CF298CA3FL},
        {0, 0xDEADBEEFCAFEBABEL, 0x4B9E2D31E2F3BE1AL},
        {1, 0x0000000000000000L, 0x01A6BDFC8C3D62DBL},
        {1, 0x9E3779B97F4A7C15L, 0x9D50F14E915849CEL},
        {1, 0xDEADBEEFCAFEBABEL, 0xCCCC62AC48D47C24L},
        {2, 0x0000000000000000L, 0xDADF7DA717BFE154L},
        {2, 0x9E3779B97F4A7C15L, 0xC89A133687D95091L},
        {2, 0xDEADBEEFCAFEBABEL, 0x30F0C4A46F6AD806L},
        {3, 0x0000000000000000L, 0xEA21F17856742557L},
        {3, 0x9E3779B97F4A7C15L, 0xE58B6749C35ABE5CL},
        {3, 0xDEADBEEFCAFEBABEL, 0x6D72E08B53427EE0L},
        {4, 0x0000000000000000L, 0x7C58CF3C18F9B496L},
        {4, 0x9E3779B97F4A7C15L, 0x99AAF1833279AA9DL},
        {4, 0xDEADBEEFCAFEBABEL, 0xA1B22F8158CFD63FL},
        {5, 0x0000000000000000L, 0x517724B9853C566CL},
        {5, 0x9E3779B97F4A7C15L, 0x250ACB4274262E38L},
        {5, 0xDEADBEEFCAFEBABEL, 0x31760EE78A0023EFL},
        {6, 0x0000000000000000L, 0xE1D10A4FBBE1DBC7L},
        {6, 0x9E3779B97F4A7C15L, 0xC93F66BF5463FFF0L},
        {6, 0xDEADBEEFCAFEBABEL, 0x8C20FD5F52DCE3CFL},
        {7, 0x0000000000000000L, 0x56A3989BDDD7CBE5L},
        {7, 0x9E3779B97F4A7C15L, 0xD7D254EE5D533404L},
        {7, 0xDEADBEEFCAFEBABEL, 0xC1D3AB0CC35936B9L},
        {8, 0x0000000000000000L, 0xDFD74858772B0E91L},
        {8, 0x9E3779B97F4A7C15L, 0x9BB51F7D28C94379L},
        {8, 0xDEADBEEFCAFEBABEL, 0x600552A3FF515FAEL},
        {9, 0x0000000000000000L, 0x0D246B6B3127404EL},
        {9, 0x9E3779B97F4A7C15L, 0x96A02B773AF52564L},
        {9, 0xDEADBEEFCAFEBABEL, 0x691BF2C6A9EEF2E0L},
        {10, 0x0000000000000000L, 0x940CED4143CCCB2BL},
        {10, 0x9E3779B97F4A7C15L, 0x6D653DB391FBCACCL},
        {10, 0xDEADBEEFCAFEBABEL, 0x24169BD3021086ACL},
        {11, 0x0000000000000000L, 0x00F4C1EB06392F40L},
        {11, 0x9E3779B97F4A7C15L, 0xA239C14889BA224AL},
        {11, 0xDEADBEEFCAFEBABEL, 0x29D070B34AF489CEL},
        {12, 0x0000000000000000L, 0x35985EE7C1DFC292L},
        {12, 0x9E3779B97F4A7C15L, 0xC65A041E835E8250L},
        {12, 0xDEADBEEFCAFEBABEL, 0x310DFF6EFCE0F6BDL},
        {13, 0x0000000000000000L, 0xFEA35C4B388EC02BL},
        {13, 0x9E3779B97F4A7C15L, 0xAA3CEFED2E20869EL},
        {13, 0xDEADBEEFCAFEBABEL, 0xF478BC0F8259144CL},
        {14, 0x0000000000000000L, 0x08524E7E5AA2CE93L},
        {14, 0x9E3779B97F4A7C15L, 0x5D380960C8653731L},
        {14, 0xDEADBEEFCAFEBABEL, 0x8887680638A97434L},
        {15, 0x0000000000000000L, 0xE82F8F2F83E24DC3L},
        {15, 0x9E3779B97F4A7C15L, 0xA55CC72365471962L},
        {15, 0xDEADBEEFCAFEBABEL, 0xAF5A8128D9513D53L},
        {16, 0x0000000000000000L, 0xC6C3B656C926EF2BL},
        {16, 0x9E3779B97F4A7C15L, 0x348B4B44E949A4A5L},
        {16, 0xDEADBEEFCAFEBABEL, 0x8B444516C3FCE921L},
        {17, 0x0000000000000000L, 0xA90B41F0B4D835FBL},
        {17, 0x9E3779B97F4A7C15L, 0xEB9CF0A2219A8A79L},
        {17, 0xDEADBEEFCAFEBABEL, 0x2B0BCB27D533F29DL},
        {20, 0x0000000000000000L, 0xD71BDC0ADCA8ADF8L},
        {20, 0x9E3779B97F4A7C15L, 0x1DED18E5D2C6641DL},
        {20, 0xDEADBEEFCAFEBABEL, 0x50E620AA5892FC5EL},
        {24, 0x0000000000000000L, 0x6982F8BDFE69F930L},
        {24, 0x9E3779B97F4A7C15L, 0xFB5432EF4D039215L},
        {24, 0xDEADBEEFCAFEBABEL, 0xB7A4906D498A9857L},
        {31, 0x0000000000000000L, 0xB01CADA4E3595061L},
        {31, 0x9E3779B97F4A7C15L, 0xBB07CE100EF1ACE7L},
        {31, 0xDEADBEEFCAFEBABEL, 0xD0B87A0842A08418L},
        {32, 0x0000000000000000L, 0x242FD3F914303C1DL},
        {32, 0x9E3779B97F4A7C15L, 0x343BA63D7E4FC2F6L},
        {32, 0xDEADBEEFCAFEBABEL, 0x66D7EF7DA4627549L},
        {33, 0x0000000000000000L, 0xE87DE7C9F18E3D9DL},
        {33, 0x9E3779B97F4A7C15L, 0x548C908A58A6CDF1L},
        {33, 0xDEADBEEFCAFEBABEL, 0xC115EA8FBA551490L},
        {47, 0x0000000000000000L, 0x696428DD947DF322L},
        {47, 0x9E3779B97F4A7C15L, 0x39BE535C74585AC5L},
        {47, 0xDEADBEEFCAFEBABEL, 0x52811A1D01562807L},
        {48, 0x0000000000000000L, 0x1059B4E24C8CFDF0L},
        {48, 0x9E3779B97F4A7C15L, 0x643BA76560D01C09L},
        {48, 0xDEADBEEFCAFEBABEL, 0xA537F4069657C426L},
        {63, 0x0000000000000000L, 0x4471E159CA7F1AA1L},
        {63, 0x9E3779B97F4A7C15L, 0xE979866C4F557390L},
        {63, 0xDEADBEEFCAFEBABEL, 0xB026D08EC12753F1L},
        {64, 0x0000000000000000L, 0x62BC7D33C15657E9L},
        {64, 0x9E3779B97F4A7C15L, 0x0078437E7D379478L},
        {64, 0xDEADBEEFCAFEBABEL, 0x765229403E95673BL},
        {65, 0x0000000000000000L, 0xA57F09711AE70C77L},
        {65, 0x9E3779B97F4A7C15L, 0xA5F941F9895FAF8DL},
        {65, 0xDEADBEEFCAFEBABEL, 0x74F19F9600A6C10FL},
        {96, 0x0000000000000000L, 0x2186FDC93E032C9CL},
        {96, 0x9E3779B97F4A7C15L, 0x4164E090EA0C5DF9L},
        {96, 0xDEADBEEFCAFEBABEL, 0x967410100E8C7C8AL},
        {127, 0x0000000000000000L, 0xDAA46CC2E67CF5C3L},
        {127, 0x9E3779B97F4A7C15L, 0x1D78B9BCE14CF66FL},
        {127, 0xDEADBEEFCAFEBABEL, 0x7CC9599ACC50F32BL},
        {128, 0x0000000000000000L, 0xD4C430490D0CE9D1L},
        {128, 0x9E3779B97F4A7C15L, 0xAE0594746A45D322L},
        {128, 0xDEADBEEFCAFEBABEL, 0xC777BD88BF800192L},
        {191, 0x0000000000000000L, 0xAB4EB4F6A214AB26L},
        {191, 0x9E3779B97F4A7C15L, 0x2F01EEFE05F61681L},
        {191, 0xDEADBEEFCAFEBABEL, 0x0AC11D2D060A6D39L},
        {192, 0x0000000000000000L, 0xBD4668FD0E37A0D8L},
        {192, 0x9E3779B97F4A7C15L, 0xB33BD21C20D7F1EEL},
        {192, 0xDEADBEEFCAFEBABEL, 0x2C05041A563EFC0AL},
        {255, 0x0000000000000000L, 0x3F35C5137D9DDD92L},
        {255, 0x9E3779B97F4A7C15L, 0xB3450BEEA6A88C0EL},
        {255, 0xDEADBEEFCAFEBABEL, 0x10EEAFB383803642L},
        {319, 0x0000000000000000L, 0xFB6F356631C62298L},
        {319, 0x9E3779B97F4A7C15L, 0x82A92D2F3C0D3FC2L},
        {319, 0xDEADBEEFCAFEBABEL, 0xC187B939C37F8EC7L},
        {320, 0x0000000000000000L, 0xAAB4DE0105C41715L},
        {320, 0x9E3779B97F4A7C15L, 0x24747E138240D684L},
        {320, 0xDEADBEEFCAFEBABEL, 0xD908512F166E3CD2L},
        {321, 0x0000000000000000L, 0xD58DE26140651F72L},
        {321, 0x9E3779B97F4A7C15L, 0xAE8B28A6EF04CC35L},
        {321, 0xDEADBEEFCAFEBABEL, 0xAA4EFD9BA8E0C810L},
        {383, 0x0000000000000000L, 0x6FBC1354616B9257L},
        {383, 0x9E3779B97F4A7C15L, 0x09A754CB4921EAB0L},
        {383, 0xDEADBEEFCAFEBABEL, 0xF1548B3D85DA380EL},
        {512, 0x0000000000000000L, 0x2968882331191FDBL},
        {512, 0x9E3779B97F4A7C15L, 0x143C166BDE64236DL},
        {512, 0xDEADBEEFCAFEBABEL, 0x5E7F7FF6C918FE7FL},
        {1023, 0x0000000000000000L, 0xDA47A412C97502BEL},
        {1023, 0x9E3779B97F4A7C15L, 0x003D27583FDEE215L},
        {1023, 0xDEADBEEFCAFEBABEL, 0x80F98032FDB103FDL},
        {1024, 0x0000000000000000L, 0xD3594D0A25CB043BL},
        {1024, 0x9E3779B97F4A7C15L, 0xED0E2941D2F3D593L},
        {1024, 0xDEADBEEFCAFEBABEL, 0xF00E1771AB6A3869L},
    };

    @Test
    void matchesCReference() {
        byte[] buf = katBuffer();
        for (long[] v : VECTORS) {
            int len = (int) v[0];
            String at = String.format("len=%d seed=%#018x", len, v[1]);
            assertEquals(v[2], Hayahash.hash64(Arrays.copyOf(buf, len), v[1]), at);
            assertEquals(v[2], Hayahash.hash64(buf, 0, len, v[1]), at);
        }
    }

    // Reproduces SMHasher3's self-test: hash the key prefix of length i
    // with seed 256-i for i in 0..=255, concatenating the little-endian
    // digests (key byte i is set to i after round i), then hash that
    // buffer with seed 0. The low 32 bits must match the registered
    // verification value.
    @Test
    void smhasher3VerificationValue() {
        byte[] key = new byte[256];
        ByteBuffer hashes = ByteBuffer.allocate(256 * 8).order(ByteOrder.LITTLE_ENDIAN);
        for (int i = 0; i < 256; i++) {
            hashes.putLong(i * 8, Hayahash.hash64(key, 0, i, 256 - i));
            key[i] = (byte) i;
        }
        long total = Hayahash.hash64(hashes.array(), 0);
        assertEquals(0xF3C4A9B4, (int) total, "verification value");
    }

    @Test
    void helloWorldExample() {
        byte[] input = "hello world".getBytes(StandardCharsets.UTF_8);
        assertEquals(0xF2172C5BD68EC576L, Hayahash.hash64(input, 0));
    }

    @Test
    void rangeOverloadMatchesCopy() {
        byte[] buf = katBuffer();
        int[] offsets = {0, 1, 7, 13, 64, 401};
        int[] lengths = {0, 1, 2, 3, 5, 8, 13, 16, 17, 31, 32, 33, 63, 64, 65, 127, 319, 320, 321, 512};
        long[] seeds = {0x0000000000000000L, 0x9E3779B97F4A7C15L, 0xDEADBEEFCAFEBABEL};
        for (int off : offsets) {
            for (int len : lengths) {
                if (off + len > buf.length) {
                    continue;
                }
                byte[] copy = Arrays.copyOfRange(buf, off, off + len);
                for (long seed : seeds) {
                    assertEquals(Hayahash.hash64(copy, seed),
                            Hayahash.hash64(buf, off, len, seed),
                            String.format("off=%d len=%d seed=%#018x", off, len, seed));
                }
            }
        }
    }

    @Test
    void rangeOverloadChecksBounds() {
        byte[] buf = new byte[8];
        assertThrows(IndexOutOfBoundsException.class, () -> Hayahash.hash64(buf, 1, 8, 0));
        assertThrows(IndexOutOfBoundsException.class, () -> Hayahash.hash64(buf, -1, 4, 0));
        assertThrows(IndexOutOfBoundsException.class, () -> Hayahash.hash64(buf, 0, -1, 0));
        assertThrows(IndexOutOfBoundsException.class, () -> Hayahash.hash64(buf, 9, 0, 0));
    }
}
