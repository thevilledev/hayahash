package io.github.thevilledev.hayahash;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;

import java.util.ArrayList;
import java.util.List;
import org.junit.jupiter.api.Test;

/**
 * Streaming conformance: every split of an input must produce the one-shot digest, and digesting
 * must not consume the state.
 */
final class HasherTest {

  private static final long K = 0x9E3779B97F4A7C15L;

  /** The shared portable input fill used by the KAT tables and test_vectors/. */
  private static byte[] patternA(int n) {
    byte[] b = new byte[n];
    for (int i = 0; i < n; i++) {
      b[i] = (byte) ((i * K + 0x2545F4914F6CDD1DL) >>> 56);
    }
    return b;
  }

  /**
   * Chunk sizes that straddle the 448-byte buffer, the 128-byte keep floor and the 64-byte block.
   */
  private static final int[] SPLITS = {1, 7, 64, 127, 448, 449, Integer.MAX_VALUE};

  private static void feed(Hasher h, byte[] data, int chunk) {
    for (int i = 0; i < data.length; ) {
      int n = Math.min(chunk, data.length - i);
      h.update(data, i, n);
      i += n;
    }
  }

  private static List<Integer> lengths() {
    List<Integer> out = new ArrayList<>();
    for (int n = 0; n <= 640; n++) {
      out.add(n);
    }
    for (int n : new int[] {895, 896, 897, 1023, 1024, 1025, 4096, 20000, 131073}) {
      out.add(n);
    }
    return out;
  }

  @Test
  void streamingMatchesOneShot() {
    long[] seeds = {0L, K, 0xDEADBEEFCAFEBABEL};
    for (long seed : seeds) {
      for (int n : lengths()) {
        byte[] data = patternA(n);
        long want64 = Hayahash.hash64(data, seed);
        Hayahash.Hash128 want128 = Hayahash.hash128(data, seed);
        for (int chunk : SPLITS) {
          Hasher h = new Hasher(seed);
          feed(h, data, chunk);
          assertEquals(want64, h.digest64(), "len=" + n + " seed=" + seed + " chunk=" + chunk);
          assertEquals(want128, h.digest128(), "len=" + n + " seed=" + seed + " chunk=" + chunk);
          assertEquals(want64, h.digest128().lo());
        }
      }
    }
  }

  @Test
  void digestIsNonDestructive() {
    int total = 2000;
    byte[] data = patternA(total);
    for (int cut : new int[] {0, 1, 63, 64, 447, 448, 449, 1000, total}) {
      Hasher h = new Hasher(7);
      h.update(data, 0, cut);
      long first = h.digest64();
      assertEquals(first, h.digest64(), "repeated digest differs at cut=" + cut);
      assertEquals(Hayahash.hash64(data, 0, cut, 7), first, "cut=" + cut);
      assertEquals(first, h.digest128().lo(), "cut=" + cut);
      h.update(data, cut, total - cut);
      assertEquals(Hayahash.hash64(data, 7), h.digest64(), "continued at cut=" + cut);
    }
  }

  @Test
  void emptyAndZeroLengthUpdates() {
    Hasher h = new Hasher();
    assertEquals(Hayahash.hash64(new byte[0], 0), h.digest64());
    h.update(new byte[0]);
    assertEquals(Hayahash.hash64(new byte[0], 0), h.digest64());
    assertEquals(0L, h.length());

    byte[] data = patternA(500);
    h.update(data, 0, 200);
    h.update(new byte[0]);
    h.update(data, 200, 300);
    assertEquals(Hayahash.hash64(data, 0), h.digest64());
    assertEquals(500L, h.length());
  }

  @Test
  void resetKeepsOrReplacesSeed() {
    byte[] data = patternA(1000);
    Hasher h = new Hasher(0xABCDL);
    h.update(data);
    h.reset();
    assertEquals(0L, h.length());
    h.update(data, 0, 10);
    assertEquals(Hayahash.hash64(data, 0, 10, 0xABCDL), h.digest64());
    h.reset(1L);
    assertEquals(1L, h.seed());
    h.update(data, 0, 10);
    assertEquals(Hayahash.hash64(data, 0, 10, 1L), h.digest64());
  }

  @Test
  void updateChecksBounds() {
    Hasher h = new Hasher();
    byte[] data = new byte[10];
    assertThrows(IndexOutOfBoundsException.class, () -> h.update(data, -1, 1));
    assertThrows(IndexOutOfBoundsException.class, () -> h.update(data, 0, 11));
    assertThrows(IndexOutOfBoundsException.class, () -> h.update(data, 5, 6));
  }

  /**
   * Pins the "streaming equality samples" section of test_vectors/v0.5.0.txt, which until now no
   * port consumed. pattern_a input, seed 0, absorbed one byte at a time.
   */
  @Test
  void publishedStreamingVectors() {
    long[][] vectors = {
      {0, 0x68AC507CF298CA3FL},
      {5, 0x37EE1F8B5A98B84BL},
      {10, 0xE28B66FB1E4CB4EAL},
      {15, 0x9A8920A57F119D6BL},
      {20, 0xC311E14FF31FB2BFL},
      {25, 0xC27FDE4AC86CCE54L},
      {30, 0x16CC1E65CA2CB4F3L},
      {35, 0x1C6522BDC246DA12L},
      {40, 0xD110128D567CB9F8L},
    };
    for (long[] v : vectors) {
      Hasher h = new Hasher(0);
      for (byte b : patternA((int) v[0])) {
        h.update(new byte[] {b});
      }
      assertEquals(v[1], h.digest64(), "len=" + v[0]);
    }
  }
}
