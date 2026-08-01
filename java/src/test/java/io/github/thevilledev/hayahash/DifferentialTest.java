/* Nightly differential conformance against a randomized C-reference corpus. */
package io.github.thevilledev.hayahash;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.fail;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import org.junit.jupiter.api.Test;

class DifferentialTest {

  private static void requireRemaining(ByteBuffer cursor, int bytes) {
    if (bytes < 0 || cursor.remaining() < bytes) {
      fail("truncated differential corpus at byte " + cursor.position());
    }
  }

  @Test
  void randomizedCReferenceCorpus() throws IOException {
    String path = System.getenv("HAYAHASH_CORPUS");
    if (path == null || path.isEmpty()) {
      System.err.println("HAYAHASH_CORPUS is unset; skipping nightly differential corpus");
      return;
    }

    byte[] corpus = Files.readAllBytes(Path.of(path));
    ByteBuffer cursor = ByteBuffer.wrap(corpus).order(ByteOrder.LITTLE_ENDIAN);
    requireRemaining(cursor, 8);
    byte[] magic = new byte[8];
    cursor.get(magic);
    assertArrayEquals("HAYAFZ01".getBytes(StandardCharsets.US_ASCII), magic);
    requireRemaining(cursor, 12);
    long caseCount = Integer.toUnsignedLong(cursor.getInt());
    long prngSeed = cursor.getLong();

    for (long caseIndex = 0; caseIndex < caseCount; caseIndex++) {
      requireRemaining(cursor, 20);
      int length = cursor.getInt();
      long hashSeed = cursor.getLong();
      long expected = cursor.getLong();
      requireRemaining(cursor, length);
      int inputOffset = cursor.position();
      long actual = Hayahash.hash64(corpus, inputOffset, length, hashSeed);
      cursor.position(inputOffset + length);
      assertEquals(
          expected,
          actual,
          String.format(
              "case=%d len=%d hash_seed=0x%016x corpus_prng_seed=0x%016x",
              caseIndex, length, hashSeed, prngSeed));
    }

    assertEquals(0, cursor.remaining(), "trailing bytes in differential corpus");
    System.err.printf(
        "Java matched %d C-reference cases (corpus PRNG seed=0x%016x)%n", caseCount, prngSeed);
  }
}
