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
        {32, 0x0000000000000000L, 0x4CB8942324250253L},
        {32, 0x9E3779B97F4A7C15L, 0xB42703DE5802CE94L},
        {32, 0xDEADBEEFCAFEBABEL, 0x8262A44DEEF5BB9BL},
        {33, 0x0000000000000000L, 0x10F0BCA1DF4D619EL},
        {33, 0x9E3779B97F4A7C15L, 0xCB48982F8FA4275AL},
        {33, 0xDEADBEEFCAFEBABEL, 0x9D20E623D2DABD98L},
        {47, 0x0000000000000000L, 0x6B7DAAB465ACE8D7L},
        {47, 0x9E3779B97F4A7C15L, 0x838C7FD540E2137BL},
        {47, 0xDEADBEEFCAFEBABEL, 0x2872D293429994C9L},
        {48, 0x0000000000000000L, 0x49107ADCDE8E3130L},
        {48, 0x9E3779B97F4A7C15L, 0x464BA3B8EC8AC2C2L},
        {48, 0xDEADBEEFCAFEBABEL, 0xDDE8583E92A44E9AL},
        {63, 0x0000000000000000L, 0xCEA6DA6E3F61096FL},
        {63, 0x9E3779B97F4A7C15L, 0x8A345448769249D7L},
        {63, 0xDEADBEEFCAFEBABEL, 0x5F7D616C0C270A64L},
        {64, 0x0000000000000000L, 0x448F9774A82C2543L},
        {64, 0x9E3779B97F4A7C15L, 0xCC07C69DFC7C4835L},
        {64, 0xDEADBEEFCAFEBABEL, 0x874B418FD0A7DD92L},
        {65, 0x0000000000000000L, 0xD6C623C771B90A0CL},
        {65, 0x9E3779B97F4A7C15L, 0xE212A3C24E1EC790L},
        {65, 0xDEADBEEFCAFEBABEL, 0xD8AC399BE6418F77L},
        {96, 0x0000000000000000L, 0x2D31F3DECB602FD3L},
        {96, 0x9E3779B97F4A7C15L, 0x140B1122BD6AFC19L},
        {96, 0xDEADBEEFCAFEBABEL, 0x96154215E49D4615L},
        {127, 0x0000000000000000L, 0xB719FF6021114CD8L},
        {127, 0x9E3779B97F4A7C15L, 0x961A22A3478552E7L},
        {127, 0xDEADBEEFCAFEBABEL, 0xF3B18B0FFD884B22L},
        {128, 0x0000000000000000L, 0x968B305502DBA431L},
        {128, 0x9E3779B97F4A7C15L, 0x471A48CF9B92DC42L},
        {128, 0xDEADBEEFCAFEBABEL, 0x0752440B83EE4A3DL},
        {191, 0x0000000000000000L, 0xF3FF82570D2D8256L},
        {191, 0x9E3779B97F4A7C15L, 0xF44D7D59B268EE48L},
        {191, 0xDEADBEEFCAFEBABEL, 0x9FC1503D455244E6L},
        {192, 0x0000000000000000L, 0x8B2F53A8D1592DE0L},
        {192, 0x9E3779B97F4A7C15L, 0x079AF5C58C9F34D8L},
        {192, 0xDEADBEEFCAFEBABEL, 0x34FF2D0B06CD24A1L},
        {255, 0x0000000000000000L, 0xE47C9AEC5C83F47AL},
        {255, 0x9E3779B97F4A7C15L, 0x609491AF8062780AL},
        {255, 0xDEADBEEFCAFEBABEL, 0x3EBBFF793A025A3AL},
        {319, 0x0000000000000000L, 0x0E3593E08BED4F79L},
        {319, 0x9E3779B97F4A7C15L, 0xCF66BCA2AE1CFE7AL},
        {319, 0xDEADBEEFCAFEBABEL, 0x8E3415FC76D03904L},
        {320, 0x0000000000000000L, 0x8C8BFEE038F4D9BCL},
        {320, 0x9E3779B97F4A7C15L, 0xC826AE9965DDA128L},
        {320, 0xDEADBEEFCAFEBABEL, 0x768A97E86095A1F5L},
        {321, 0x0000000000000000L, 0xC975DEB2B0A4D083L},
        {321, 0x9E3779B97F4A7C15L, 0x7BAC7E4CF1E08199L},
        {321, 0xDEADBEEFCAFEBABEL, 0x110D6A9104190DA9L},
        {383, 0x0000000000000000L, 0xEF1113151DEAE6EDL},
        {383, 0x9E3779B97F4A7C15L, 0x5D88576FE05458DEL},
        {383, 0xDEADBEEFCAFEBABEL, 0x8D9CD7C2CB9E80E7L},
        {512, 0x0000000000000000L, 0x6EC25257921A6924L},
        {512, 0x9E3779B97F4A7C15L, 0x6803A7F94C922829L},
        {512, 0xDEADBEEFCAFEBABEL, 0xA07F4EF94A4B5CA8L},
        {1023, 0x0000000000000000L, 0x97408D1577DE153AL},
        {1023, 0x9E3779B97F4A7C15L, 0xB2ED4FCC76F3C87EL},
        {1023, 0xDEADBEEFCAFEBABEL, 0xF2B98444F1CB4582L},
        {1024, 0x0000000000000000L, 0xE0A1CB1C1C0BAC68L},
        {1024, 0x9E3779B97F4A7C15L, 0xA5CEFA133BEE637CL},
        {1024, 0xDEADBEEFCAFEBABEL, 0xD0AE6DF69F1413A3L},
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
        assertEquals(0x6B558D9D, (int) total, "verification value");
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
