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
    {0, 0x0000000000000000L, 0x68AC507CF298CA3FL},
    {0, 0x9E3779B97F4A7C15L, 0xC4F85F43D5A9985EL},
    {0, 0xDEADBEEFCAFEBABEL, 0x7EDC9F1B603B7337L},
    {1, 0x0000000000000000L, 0x7EC9660A48395D15L},
    {1, 0x9E3779B97F4A7C15L, 0x4D49AADD61BED986L},
    {1, 0xDEADBEEFCAFEBABEL, 0x8E456FA77805E810L},
    {2, 0x0000000000000000L, 0x3AE1E83A68B10976L},
    {2, 0x9E3779B97F4A7C15L, 0xE27ACD9CD85250AEL},
    {2, 0xDEADBEEFCAFEBABEL, 0xB64D67091596299DL},
    {3, 0x0000000000000000L, 0x10E8B8FEA2D42E52L},
    {3, 0x9E3779B97F4A7C15L, 0x377E32D405528932L},
    {3, 0xDEADBEEFCAFEBABEL, 0xDCC0CB335DC1DE4BL},
    {4, 0x0000000000000000L, 0x3FF33333AEEA0226L},
    {4, 0x9E3779B97F4A7C15L, 0x7BB1267AF5779B6BL},
    {4, 0xDEADBEEFCAFEBABEL, 0x321409D41F3F0597L},
    {5, 0x0000000000000000L, 0x37EE1F8B5A98B84BL},
    {5, 0x9E3779B97F4A7C15L, 0xBEF801DDD997C630L},
    {5, 0xDEADBEEFCAFEBABEL, 0x169255793C689422L},
    {6, 0x0000000000000000L, 0x7C024E9BC939E745L},
    {6, 0x9E3779B97F4A7C15L, 0xDBE499DF16AF6C98L},
    {6, 0xDEADBEEFCAFEBABEL, 0x38A42A135D5BFBC6L},
    {7, 0x0000000000000000L, 0x8D33EEB37AEA4269L},
    {7, 0x9E3779B97F4A7C15L, 0x5E24209A2FD00B2CL},
    {7, 0xDEADBEEFCAFEBABEL, 0xF47AB25F56BDC3D7L},
    {8, 0x0000000000000000L, 0xA7E6D3110DA23914L},
    {8, 0x9E3779B97F4A7C15L, 0xCCB63E92CE9F688BL},
    {8, 0xDEADBEEFCAFEBABEL, 0x18665C87C237153DL},
    {9, 0x0000000000000000L, 0x09C8DFCA0C41DA5CL},
    {9, 0x9E3779B97F4A7C15L, 0xE75FFD2D1E883756L},
    {9, 0xDEADBEEFCAFEBABEL, 0xE5EE2BB71F19DE1BL},
    {10, 0x0000000000000000L, 0xE28B66FB1E4CB4EAL},
    {10, 0x9E3779B97F4A7C15L, 0x2A004FEA465884CEL},
    {10, 0xDEADBEEFCAFEBABEL, 0x0418428D16CA9A24L},
    {11, 0x0000000000000000L, 0x12778E6F25C1D32AL},
    {11, 0x9E3779B97F4A7C15L, 0x48188C4BED9A1E46L},
    {11, 0xDEADBEEFCAFEBABEL, 0xAD02CB6AE9D55E5BL},
    {12, 0x0000000000000000L, 0x48D755EBE2679385L},
    {12, 0x9E3779B97F4A7C15L, 0x011E9FD88E5940FEL},
    {12, 0xDEADBEEFCAFEBABEL, 0x8180264C7B1768A9L},
    {13, 0x0000000000000000L, 0x381A61980D756222L},
    {13, 0x9E3779B97F4A7C15L, 0x64B7FC904BBC58B3L},
    {13, 0xDEADBEEFCAFEBABEL, 0x64C9FE2B9160C2CAL},
    {14, 0x0000000000000000L, 0xC71A2DB50E6448EBL},
    {14, 0x9E3779B97F4A7C15L, 0x32011605ED340D8CL},
    {14, 0xDEADBEEFCAFEBABEL, 0x3A8F543C0F65C501L},
    {15, 0x0000000000000000L, 0x9A8920A57F119D6BL},
    {15, 0x9E3779B97F4A7C15L, 0x5572E81BAB3953FFL},
    {15, 0xDEADBEEFCAFEBABEL, 0x7E1EB5F7F4A597F0L},
    {16, 0x0000000000000000L, 0xE1AF813939BA1A9EL},
    {16, 0x9E3779B97F4A7C15L, 0x72EF22A0197AC7E6L},
    {16, 0xDEADBEEFCAFEBABEL, 0x15DDB774F1BECBF7L},
    {17, 0x0000000000000000L, 0xEB0531E9E3A3BEBEL},
    {17, 0x9E3779B97F4A7C15L, 0x0F6DFA98935233F7L},
    {17, 0xDEADBEEFCAFEBABEL, 0x3F070CC2B4422BA0L},
    {20, 0x0000000000000000L, 0xC311E14FF31FB2BFL},
    {20, 0x9E3779B97F4A7C15L, 0xAC8DC0FD5673D897L},
    {20, 0xDEADBEEFCAFEBABEL, 0x8B748E4515D7C27FL},
    {24, 0x0000000000000000L, 0x9A64D93E28CB5DA0L},
    {24, 0x9E3779B97F4A7C15L, 0xDD2D9A95B8088061L},
    {24, 0xDEADBEEFCAFEBABEL, 0xBE8112B0F103E6C5L},
    {31, 0x0000000000000000L, 0x95D2421945AEC7A1L},
    {31, 0x9E3779B97F4A7C15L, 0xCD63F6F92AE5BA34L},
    {31, 0xDEADBEEFCAFEBABEL, 0x1E915F729DA2021AL},
    {32, 0x0000000000000000L, 0xCBD35DAB7AD91CE4L},
    {32, 0x9E3779B97F4A7C15L, 0x4E5482C9BC55AC72L},
    {32, 0xDEADBEEFCAFEBABEL, 0xEFBFF5D3A7172762L},
    {33, 0x0000000000000000L, 0x134D1F8689BF729CL},
    {33, 0x9E3779B97F4A7C15L, 0x02F60A6383C9BEA7L},
    {33, 0xDEADBEEFCAFEBABEL, 0x51DE032C8DA94D2FL},
    {47, 0x0000000000000000L, 0x854A0E1FB80DC713L},
    {47, 0x9E3779B97F4A7C15L, 0x9C87C14BFAD5F65DL},
    {47, 0xDEADBEEFCAFEBABEL, 0x8DA40D3A16F8FBF1L},
    {48, 0x0000000000000000L, 0x6B6B8CAA3DDB2A68L},
    {48, 0x9E3779B97F4A7C15L, 0xFEFAE7ADD93696A6L},
    {48, 0xDEADBEEFCAFEBABEL, 0xF3DECF00052380B1L},
    {63, 0x0000000000000000L, 0x7FD21B276D3862D5L},
    {63, 0x9E3779B97F4A7C15L, 0xF8571E24784C85B0L},
    {63, 0xDEADBEEFCAFEBABEL, 0x68B11FACBCA125F5L},
    {64, 0x0000000000000000L, 0x8D2CE2017D1ECCEBL},
    {64, 0x9E3779B97F4A7C15L, 0x257D3EE25843F04BL},
    {64, 0xDEADBEEFCAFEBABEL, 0x71AA83B0D836F52DL},
    {65, 0x0000000000000000L, 0xA521C43309772CDEL},
    {65, 0x9E3779B97F4A7C15L, 0xFCD59327E5C4F6DDL},
    {65, 0xDEADBEEFCAFEBABEL, 0x2D7D45F44C1829D0L},
    {96, 0x0000000000000000L, 0x0E456A468AC7355BL},
    {96, 0x9E3779B97F4A7C15L, 0xE5F760FC0C083B17L},
    {96, 0xDEADBEEFCAFEBABEL, 0xD3B493D06042DC09L},
    {127, 0x0000000000000000L, 0x4907F10A034954D1L},
    {127, 0x9E3779B97F4A7C15L, 0x1D907A46A134AB8EL},
    {127, 0xDEADBEEFCAFEBABEL, 0x350DB49C244548A7L},
    {128, 0x0000000000000000L, 0xEECEEE2B8790729DL},
    {128, 0x9E3779B97F4A7C15L, 0x8DED815CA5788588L},
    {128, 0xDEADBEEFCAFEBABEL, 0xC9FF25BFDE22A5C7L},
    {191, 0x0000000000000000L, 0xB9E354ABAF76CDA3L},
    {191, 0x9E3779B97F4A7C15L, 0x8AB844BBF8DF6893L},
    {191, 0xDEADBEEFCAFEBABEL, 0x0A1934AE61772E91L},
    {192, 0x0000000000000000L, 0x0503FD18DB80FFFFL},
    {192, 0x9E3779B97F4A7C15L, 0xEDCACF5231FFEDF9L},
    {192, 0xDEADBEEFCAFEBABEL, 0xE6357993D5CAFBD4L},
    {255, 0x0000000000000000L, 0x1D0EE105FC8EE266L},
    {255, 0x9E3779B97F4A7C15L, 0x9EADEAF2612E6B65L},
    {255, 0xDEADBEEFCAFEBABEL, 0x674B0232E3BA8AFBL},
    {319, 0x0000000000000000L, 0x8F078F3394AC0EEBL},
    {319, 0x9E3779B97F4A7C15L, 0x0672714B8B89EAF4L},
    {319, 0xDEADBEEFCAFEBABEL, 0x66F3ECD10E74B602L},
    {320, 0x0000000000000000L, 0xF4BCF4FA135AABFEL},
    {320, 0x9E3779B97F4A7C15L, 0x6F86504F4C61F014L},
    {320, 0xDEADBEEFCAFEBABEL, 0xF0563F11BE6D85C7L},
    {321, 0x0000000000000000L, 0x68868A120FB9CEF6L},
    {321, 0x9E3779B97F4A7C15L, 0x58B97AFD4ADA0656L},
    {321, 0xDEADBEEFCAFEBABEL, 0xEDF253CD5A32819CL},
    {383, 0x0000000000000000L, 0x762CF976C6FFBA80L},
    {383, 0x9E3779B97F4A7C15L, 0x738E8B85886F0EAFL},
    {383, 0xDEADBEEFCAFEBABEL, 0x368281E467A93A0EL},
    {512, 0x0000000000000000L, 0xDFBF7FC9292FF7FFL},
    {512, 0x9E3779B97F4A7C15L, 0x153A2FC22ACBAEA6L},
    {512, 0xDEADBEEFCAFEBABEL, 0x2E9B7D29F46F8552L},
    {1023, 0x0000000000000000L, 0x2578244C81138967L},
    {1023, 0x9E3779B97F4A7C15L, 0xB2C3356B389D297DL},
    {1023, 0xDEADBEEFCAFEBABEL, 0x125EAA99FBAE5C4DL},
    {1024, 0x0000000000000000L, 0x951BE6CF3BC7CF43L},
    {1024, 0x9E3779B97F4A7C15L, 0x3460EEC64AA4799FL},
    {1024, 0xDEADBEEFCAFEBABEL, 0xC93D1F81FD51336AL},
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
    assertEquals(0x65F2AC15, (int) total, "verification value");
  }

  @Test
  void helloWorldExample() {
    byte[] input = "hello world".getBytes(StandardCharsets.UTF_8);
    assertEquals(0x4524B96611BFC05AL, Hayahash.hash64(input, 0));
  }

  @Test
  void hash128BoundaryVectors() {
    long[][] vectors = {
      {0, 0xBDBDB99AFC307BE6L}, {1, 0x38A7F291F946E326L},
      {3, 0xF41308B1E23E701AL}, {7, 0x598517B1629A2661L},
      {8, 0xE2EF4308D2736A10L}, {16, 0x26634ED944557F63L},
      {17, 0xE4F325B405DF676EL}, {31, 0xAF319AB2213F66A1L},
      {32, 0x9AA61D0A8145639AL}, {63, 0xF897985E74078907L},
      {64, 0x13611650F75C9D77L}, {319, 0x2369951A61744E5DL},
      {320, 0x4BCD20725C38B8B8L}, {1000, 0x8700DBF3171144A7L},
    };
    byte[] buf = new byte[1000];
    int x = 0x9E3779B9;
    for (int i = 0; i < buf.length; i++) {
      x ^= x << 13;
      x ^= x >>> 17;
      x ^= x << 5;
      buf[i] = (byte) x;
    }
    for (long[] vector : vectors) {
      int length = (int) vector[0];
      Hayahash.Hash128 wide = Hayahash.hash128(buf, 0, length, 0x1234);
      assertEquals(Hayahash.hash64(buf, 0, length, 0x1234), wide.lo());
      assertEquals(vector[1], wide.hi(), "len=" + length);
    }
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
          assertEquals(
              Hayahash.hash64(copy, seed),
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
