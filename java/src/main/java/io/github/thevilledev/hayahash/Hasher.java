package io.github.thevilledev.hayahash;

/**
 * Incremental hayahash state.
 *
 * <p>The digest equals {@link Hayahash#hash64(byte[], long)} or {@link Hayahash#hash128(byte[],
 * long)} over the concatenation of everything passed to {@link #update}, for every split of that
 * input:
 *
 * <pre>{@code
 * Hasher h = new Hasher(7);
 * h.update("hello ".getBytes(StandardCharsets.UTF_8));
 * h.update("world".getBytes(StandardCharsets.UTF_8));
 * long digest = h.digest64();
 * }</pre>
 *
 * <p>Digesting does not consume the state, so absorbing may continue afterwards.
 *
 * <p>Instances are not safe for concurrent use.
 */
public final class Hasher {

  /**
   * Streaming buffer size. Totals below it stay buffered so short and mid inputs take the one-shot
   * dispatch at digest time, exactly as the C reference does.
   */
  private static final int BUF_CAP = 448;

  /**
   * The floor the buffer is drained to. The digest-time mid/tail phase reaches back up to 16 bytes
   * before the current pointer, so the buffer has to retain more than that.
   */
  private static final int KEEP = 128;

  private final long[] h = new long[8];
  private final byte[] buf = new byte[BUF_CAP];
  private long wp;
  private long seed;
  private long total;
  private int nbuf;
  private boolean bulk;

  /** Creates an empty state with seed 0. */
  public Hasher() {
    this(0L);
  }

  /**
   * Creates an empty state with the given seed.
   *
   * @param seed the 64-bit seed
   */
  public Hasher(long seed) {
    reset(seed);
  }

  /**
   * Discards absorbed input and restarts with a new seed.
   *
   * @param seed the 64-bit seed
   */
  public void reset(long seed) {
    long k = Hayahash.K;
    long s = seed ^ k;
    h[0] = s ^ k;
    h[1] = Long.rotateLeft(s, 17) + (k << 21);
    h[2] = Long.rotateLeft(s, 34) ^ (k >>> 13);
    h[3] = Long.rotateLeft(s, 51) + (k << 42);
    h[4] = s + (k >>> 27);
    h[5] = Long.rotateLeft(s, 13) ^ (k << 9);
    h[6] = Long.rotateLeft(s, 26) + (k >>> 40);
    h[7] = Long.rotateLeft(s, 39) ^ (k << 30);
    this.wp = 0;
    this.seed = seed;
    this.total = 0;
    this.nbuf = 0;
    this.bulk = false;
  }

  /** Discards absorbed input, keeping the current seed. */
  public void reset() {
    reset(this.seed);
  }

  /**
   * Returns the seed this state was created or last reset with.
   *
   * @return the 64-bit seed
   */
  public long seed() {
    return seed;
  }

  /**
   * Returns the number of bytes absorbed so far.
   *
   * @return the absorbed length in bytes
   */
  public long length() {
    return total;
  }

  /**
   * Absorbs {@code data}.
   *
   * @param data the bytes to absorb
   */
  public void update(byte[] data) {
    update(data, 0, data.length);
  }

  /**
   * Absorbs {@code length} bytes of {@code data} starting at {@code offset}.
   *
   * @param data the backing array
   * @param offset the first index to absorb
   * @param length the number of bytes to absorb
   * @throws IndexOutOfBoundsException if the range is outside {@code data}
   */
  public void update(byte[] data, int offset, int length) {
    if (offset < 0 || length < 0 || offset > data.length - length) {
      throw new IndexOutOfBoundsException(
          "offset " + offset + " length " + length + " array " + data.length);
    }
    if (length == 0) {
      return;
    }
    int p = offset;
    int n = length;
    total += n;

    if (!bulk) {
      // Undecided between the one-shot finish and the bulk path:
      // totals up to BUF_CAP-1 stay buffered.
      if (nbuf + n < BUF_CAP) {
        System.arraycopy(data, p, buf, nbuf, n);
        nbuf += n;
        return;
      }
      // Total is now >= 448 > BULK_MIN: commit to the bulk path.
      bulk = true;
    }

    while (true) {
      // Buffer at its floor with plenty incoming: drain the floor, then
      // stream whole blocks straight from the caller's array, leaving a
      // [KEEP, KEEP+63]-byte remainder for the buffer.
      if (nbuf == KEEP && n > BUF_CAP) {
        int direct = (n - KEEP) & ~63;
        blocks(buf, 0, KEEP);
        blocks(data, p, direct);
        p += direct;
        n -= direct;
        nbuf = 0;
      }
      int take = Math.min(BUF_CAP - nbuf, n);
      System.arraycopy(data, p, buf, nbuf, take);
      nbuf += take;
      p += take;
      n -= take;
      if (nbuf < BUF_CAP) {
        break;
      }
      // Buffer full: consume whole blocks down to the keep floor.
      int consume = (nbuf - KEEP) & ~63;
      blocks(buf, 0, consume);
      nbuf -= consume;
      System.arraycopy(buf, consume, buf, 0, nbuf);
    }
  }

  /** Absorbs {@code len} bytes from {@code off}, which must be a multiple of 64. */
  private void blocks(byte[] p, int off, int len) {
    long k = Hayahash.K;
    long h0 = h[0];
    long h1 = h[1];
    long h2 = h[2];
    long h3 = h[3];
    long h4 = h[4];
    long h5 = h[5];
    long h6 = h[6];
    long h7 = h[7];
    long w = this.wp;
    for (int end = off + len; off != end; off += 64) {
      long v;
      v = Hayahash.load64(p, off);
      h0 = (h0 ^ (v + Long.rotateLeft(w, 27))) * k;
      w = v;
      v = Hayahash.load64(p, off + 8);
      h1 = (h1 ^ (v + Long.rotateLeft(w, 27))) * k;
      w = v;
      v = Hayahash.load64(p, off + 16);
      h2 = (h2 ^ (v + Long.rotateLeft(w, 27))) * k;
      w = v;
      v = Hayahash.load64(p, off + 24);
      h3 = (h3 ^ (v + Long.rotateLeft(w, 27))) * k;
      w = v;
      v = Hayahash.load64(p, off + 32);
      h4 = (h4 ^ (v + Long.rotateLeft(w, 27))) * k;
      w = v;
      v = Hayahash.load64(p, off + 40);
      h5 = (h5 ^ (v + Long.rotateLeft(w, 27))) * k;
      w = v;
      v = Hayahash.load64(p, off + 48);
      h6 = (h6 ^ (v + Long.rotateLeft(w, 27))) * k;
      w = v;
      v = Hayahash.load64(p, off + 56);
      h7 = (h7 ^ (v + Long.rotateLeft(w, 27))) * k;
      w = v;
      // Checkpoint the raw-word chain once per block so a 64-stripe
      // rotation orbit cannot hide a difference until it returns to
      // the same lane.
      h0 += w;
    }
    h[0] = h0;
    h[1] = h1;
    h[2] = h2;
    h[3] = h3;
    h[4] = h4;
    h[5] = h5;
    h[6] = h6;
    h[7] = h7;
    this.wp = w;
  }

  /**
   * Returns the 64-bit digest of everything absorbed so far, without consuming the state.
   *
   * @return the 64-bit digest
   */
  public long digest64() {
    if (!bulk) {
      return Hayahash.hash64(buf, 0, (int) total, seed);
    }
    long[] t = tail();
    long k = Hayahash.K;
    long x = (seed ^ k) ^ t[0] ^ Long.rotateLeft(t[1], 29);
    x ^= x >>> 37;
    x *= k;
    return x ^ (x >>> 32);
  }

  /**
   * Returns both digest words, without consuming the state. The low word is exactly {@link
   * #digest64()}.
   *
   * @return the 128-bit digest
   */
  public Hayahash.Hash128 digest128() {
    if (!bulk) {
      return Hayahash.hash128(buf, 0, (int) total, seed);
    }
    long[] t = tail();
    long k = Hayahash.K;
    long s = seed ^ k;
    long x = s ^ t[0] ^ Long.rotateLeft(t[1], 29);
    x ^= x >>> 37;
    x *= k;
    return new Hayahash.Hash128(
        x ^ (x >>> 32),
        Hayahash.fmix128(Long.rotateLeft(s, 32) ^ (t[1] + Long.rotateLeft(t[0], 47))));
  }

  /**
   * Continues the long path over the buffered remainder: the leftover whole blocks, then the same
   * fold, mid round, wall and tail as the one-shot. Reads the state without mutating it.
   *
   * @return {@code {t0, t1}}
   */
  private long[] tail() {
    long k = Hayahash.K;
    long lenmix = total * k;
    long h0 = h[0];
    long h1 = h[1];
    long h2 = h[2];
    long h3 = h[3];
    long h4 = h[4];
    long h5 = h[5];
    long h6 = h[6];
    long h7 = h[7];
    long w = wp;
    int p = 0;
    int l = nbuf;

    while (l >= 64) {
      long v;
      v = Hayahash.load64(buf, p);
      h0 = (h0 ^ (v + Long.rotateLeft(w, 27))) * k;
      w = v;
      v = Hayahash.load64(buf, p + 8);
      h1 = (h1 ^ (v + Long.rotateLeft(w, 27))) * k;
      w = v;
      v = Hayahash.load64(buf, p + 16);
      h2 = (h2 ^ (v + Long.rotateLeft(w, 27))) * k;
      w = v;
      v = Hayahash.load64(buf, p + 24);
      h3 = (h3 ^ (v + Long.rotateLeft(w, 27))) * k;
      w = v;
      v = Hayahash.load64(buf, p + 32);
      h4 = (h4 ^ (v + Long.rotateLeft(w, 27))) * k;
      w = v;
      v = Hayahash.load64(buf, p + 40);
      h5 = (h5 ^ (v + Long.rotateLeft(w, 27))) * k;
      w = v;
      v = Hayahash.load64(buf, p + 48);
      h6 = (h6 ^ (v + Long.rotateLeft(w, 27))) * k;
      w = v;
      v = Hayahash.load64(buf, p + 56);
      h7 = (h7 ^ (v + Long.rotateLeft(w, 27))) * k;
      w = v;
      h0 += w;
      p += 64;
      l -= 64;
    }
    h0 = (h0 ^ Long.rotateLeft(h4, 11)) * k;
    h1 = (h1 ^ Long.rotateLeft(h5, 19)) * k;
    h2 = (h2 ^ Long.rotateLeft(h6, 31)) * k;
    h3 = (h3 ^ Long.rotateLeft(h7, 47)) * k;

    if (l >= 32) {
      long v;
      v = Hayahash.load64(buf, p);
      h0 = (h0 ^ (v + Long.rotateLeft(w, 27))) * k;
      w = v;
      v = Hayahash.load64(buf, p + 8);
      h1 = (h1 ^ (v + Long.rotateLeft(w, 27))) * k;
      w = v;
      v = Hayahash.load64(buf, p + 16);
      h2 = (h2 ^ (v + Long.rotateLeft(w, 27))) * k;
      w = v;
      v = Hayahash.load64(buf, p + 24);
      h3 = (h3 ^ (v + Long.rotateLeft(w, 27))) * k;
      w = v;
      p += 32;
      l -= 32;
    }

    h0 += Long.rotateLeft(w, 27);
    if (l > 16) {
      h0 = (h0 + Hayahash.injAt(buf, p)) * k;
      h1 = (h1 + Hayahash.injAt(buf, p + 8)) * k;
    }
    // The last 16 bytes of the stream. KEEP >= 128 guarantees this
    // reach-back stays inside the buffer even when l is small.
    if (l > 0) {
      h2 = (h2 + Hayahash.injAt(buf, nbuf - 16)) * k;
      h3 = (h3 + Hayahash.injAt(buf, nbuf - 8)) * k;
    }

    return new long[] {
      (h0 ^ Long.rotateLeft(h1, 13) ^ lenmix) * k, (h2 ^ Long.rotateLeft(h3, 33)) * k
    };
  }
}
