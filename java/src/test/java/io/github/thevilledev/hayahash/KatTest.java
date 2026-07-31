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
        {17, 0x0000000000000000L, 0xF2097D97DDE357AEL},
        {17, 0x9E3779B97F4A7C15L, 0x9632041CD63DB454L},
        {17, 0xDEADBEEFCAFEBABEL, 0x1723899582C3EA6FL},
        {20, 0x0000000000000000L, 0x755BAF1F2B76C412L},
        {20, 0x9E3779B97F4A7C15L, 0x3618DCCEF36B9B12L},
        {20, 0xDEADBEEFCAFEBABEL, 0xD1DD986517B516B5L},
        {24, 0x0000000000000000L, 0xA9EEF61768886934L},
        {24, 0x9E3779B97F4A7C15L, 0x3E37447D706F68DCL},
        {24, 0xDEADBEEFCAFEBABEL, 0x7FF1AB972FE165C1L},
        {31, 0x0000000000000000L, 0xA4EBC213A72566E4L},
        {31, 0x9E3779B97F4A7C15L, 0x8C97E8FB408409EBL},
        {31, 0xDEADBEEFCAFEBABEL, 0x00FF9DB9173A7670L},
        {32, 0x0000000000000000L, 0x9733F484B26360E2L},
        {32, 0x9E3779B97F4A7C15L, 0x5536B4D8B2CDC730L},
        {32, 0xDEADBEEFCAFEBABEL, 0x8715CE33BCA2379AL},
        {33, 0x0000000000000000L, 0xA745A3E83613C58CL},
        {33, 0x9E3779B97F4A7C15L, 0x553FF7D4FC5D9DEDL},
        {33, 0xDEADBEEFCAFEBABEL, 0x4A2C6EF5DC5531C2L},
        {47, 0x0000000000000000L, 0x273936D61142DC44L},
        {47, 0x9E3779B97F4A7C15L, 0xE5310D240EEF34A6L},
        {47, 0xDEADBEEFCAFEBABEL, 0x6F1E75D18841E6A1L},
        {48, 0x0000000000000000L, 0x14FE95D8C0BBFF90L},
        {48, 0x9E3779B97F4A7C15L, 0x12363278FA0C626CL},
        {48, 0xDEADBEEFCAFEBABEL, 0x7CEDB4152693BAD0L},
        {63, 0x0000000000000000L, 0xA2D251874D348D01L},
        {63, 0x9E3779B97F4A7C15L, 0x16A48C318E8B07E0L},
        {63, 0xDEADBEEFCAFEBABEL, 0xB1E770FD7BF42E61L},
        {64, 0x0000000000000000L, 0x363A32091A09FCBBL},
        {64, 0x9E3779B97F4A7C15L, 0xC4E3781C47EFC9D1L},
        {64, 0xDEADBEEFCAFEBABEL, 0x8D03A93F23DE3191L},
        {65, 0x0000000000000000L, 0x45F1EE618787FF12L},
        {65, 0x9E3779B97F4A7C15L, 0x04985102207489F3L},
        {65, 0xDEADBEEFCAFEBABEL, 0xF363E94BF76C2046L},
        {96, 0x0000000000000000L, 0x4E69ED01F30C31C8L},
        {96, 0x9E3779B97F4A7C15L, 0xC30A61FB3B222764L},
        {96, 0xDEADBEEFCAFEBABEL, 0x58AF9E68DB1EE021L},
        {127, 0x0000000000000000L, 0x0F0FE54BC082E22AL},
        {127, 0x9E3779B97F4A7C15L, 0xE3E8106C41EA1694L},
        {127, 0xDEADBEEFCAFEBABEL, 0xCE05BE2901F0D305L},
        {128, 0x0000000000000000L, 0x2A2679B53F1A4841L},
        {128, 0x9E3779B97F4A7C15L, 0x300390CFA9B638B4L},
        {128, 0xDEADBEEFCAFEBABEL, 0x53F276CABAE36EECL},
        {191, 0x0000000000000000L, 0xA92BA7E80F7062F9L},
        {191, 0x9E3779B97F4A7C15L, 0xEB0B5C65254F5714L},
        {191, 0xDEADBEEFCAFEBABEL, 0x139A18908F2CA7EAL},
        {192, 0x0000000000000000L, 0xB7E79CDB9B877B00L},
        {192, 0x9E3779B97F4A7C15L, 0xE792EDE59093081EL},
        {192, 0xDEADBEEFCAFEBABEL, 0x1803DF1A8DEC4887L},
        {255, 0x0000000000000000L, 0xEBC48B87E0CBC931L},
        {255, 0x9E3779B97F4A7C15L, 0xF93BDA5AEEEBF508L},
        {255, 0xDEADBEEFCAFEBABEL, 0xB1C14189418FFEA4L},
        {319, 0x0000000000000000L, 0xC09297A1F1D64937L},
        {319, 0x9E3779B97F4A7C15L, 0xA41DA5A41F8DCF12L},
        {319, 0xDEADBEEFCAFEBABEL, 0xEC77E71BE598CE5CL},
        {320, 0x0000000000000000L, 0xEE3764436C14457AL},
        {320, 0x9E3779B97F4A7C15L, 0xAA5826FA7260FEDAL},
        {320, 0xDEADBEEFCAFEBABEL, 0x949A1075C6520355L},
        {321, 0x0000000000000000L, 0xE29605C4E053B57EL},
        {321, 0x9E3779B97F4A7C15L, 0x0220A8E06A7488FDL},
        {321, 0xDEADBEEFCAFEBABEL, 0x671ED25D1C2E46C6L},
        {383, 0x0000000000000000L, 0xE35440F8E70E9DA9L},
        {383, 0x9E3779B97F4A7C15L, 0xD7EFC77EF2D2BB7BL},
        {383, 0xDEADBEEFCAFEBABEL, 0x06BC8E5A39E7105AL},
        {512, 0x0000000000000000L, 0xE3E6F3E6AEB93BDBL},
        {512, 0x9E3779B97F4A7C15L, 0x33AEB18CA7D3E3B0L},
        {512, 0xDEADBEEFCAFEBABEL, 0xE5F56F3152DD7519L},
        {1023, 0x0000000000000000L, 0x9FA8B5EF83709862L},
        {1023, 0x9E3779B97F4A7C15L, 0x8B29A20DD57D53BAL},
        {1023, 0xDEADBEEFCAFEBABEL, 0x5CFF532D7767AC0EL},
        {1024, 0x0000000000000000L, 0xEFF14C6ADC7654F1L},
        {1024, 0x9E3779B97F4A7C15L, 0x8D6553C8DCA3F2C0L},
        {1024, 0xDEADBEEFCAFEBABEL, 0x1FA36EEB572C3496L},
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
        assertEquals(0x99B3876F, (int) total, "verification value");
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
