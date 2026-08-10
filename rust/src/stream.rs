//! Incremental hashing: absorb input in pieces, digest at any point.
//!
//! [`Digest`] produces exactly [`hayahash64`]/[`hayahash128`] of the
//! concatenation of everything written, for every split of that input.

use crate::{fmix128, injp, load64le, long_avalanche, Hash128, K};

/// Streaming buffer size. Totals below it stay buffered so short and
/// mid inputs take the one-shot dispatch at digest time, exactly as the
/// C reference does.
const BUF_CAP: usize = 448;

/// The floor the buffer is drained to. The digest-time mid/tail phase
/// reaches back up to 16 bytes before the current pointer, so the
/// buffer has to retain more than that.
const KEEP: usize = 128;

/// A streaming hayahash state.
///
/// The digest equals the one-shot function over the concatenation of
/// every [`update`](Digest::update), for any split:
///
/// ```
/// use hayahash::{hayahash64, Digest};
///
/// let mut d = Digest::new(7);
/// d.update(b"hello ");
/// d.update(b"world");
/// assert_eq!(d.finish64(), hayahash64(b"hello world", 7));
/// ```
///
/// Finalizing does not consume the state, so hashing can continue:
///
/// ```
/// use hayahash::{hayahash64, Digest};
///
/// let mut d = Digest::new(0);
/// d.update(b"abc");
/// let prefix = d.finish64();
/// d.update(b"def");
/// assert_eq!(prefix, hayahash64(b"abc", 0));
/// assert_eq!(d.finish64(), hayahash64(b"abcdef", 0));
/// ```
///
/// This type is `no_std`-friendly and never allocates: the buffer is
/// inline, so a `Digest` is a little over 512 bytes. For hash-map keys
/// prefer [`HayaHasher`](crate::HayaHasher), which keeps a much smaller
/// inline buffer tuned for short inputs.
#[derive(Clone)]
pub struct Digest {
    h: [u64; 8],
    wp: u64,
    seed: u64,
    total: u64,
    nbuf: usize,
    bulk: bool,
    buf: [u8; BUF_CAP],
}

impl Digest {
    /// Creates an empty state seeded with `seed`.
    #[must_use]
    pub const fn new(seed: u64) -> Self {
        let s = seed ^ K;
        Self {
            h: [
                s ^ K,
                s.rotate_left(17).wrapping_add(K << 21),
                s.rotate_left(34) ^ (K >> 13),
                s.rotate_left(51).wrapping_add(K << 42),
                s.wrapping_add(K >> 27),
                s.rotate_left(13) ^ (K << 9),
                s.rotate_left(26).wrapping_add(K >> 40),
                s.rotate_left(39) ^ (K << 30),
            ],
            wp: 0,
            seed,
            total: 0,
            nbuf: 0,
            bulk: false,
            buf: [0; BUF_CAP],
        }
    }

    /// Restarts the state with a new seed, discarding buffered input.
    pub fn reset(&mut self, seed: u64) {
        *self = Self::new(seed);
    }

    /// Returns the number of bytes absorbed so far.
    #[must_use]
    pub const fn len(&self) -> u64 {
        self.total
    }

    /// Returns `true` if nothing has been absorbed yet.
    #[must_use]
    pub const fn is_empty(&self) -> bool {
        self.total == 0
    }

    /// Absorbs `data`.
    pub fn update(&mut self, data: &[u8]) {
        let mut p = data;
        if p.is_empty() {
            return;
        }
        self.total = self.total.wrapping_add(p.len() as u64);

        if !self.bulk {
            // Undecided between the one-shot finish and the bulk path:
            // totals up to BUF_CAP-1 stay buffered.
            if self.nbuf + p.len() < BUF_CAP {
                self.buf[self.nbuf..self.nbuf + p.len()].copy_from_slice(p);
                self.nbuf += p.len();
                return;
            }
            // Total is now >= 448 > BULK_MIN: commit to the bulk path.
            self.bulk = true;
        }

        loop {
            // Buffer at its floor with plenty incoming: drain the
            // floor, then stream whole blocks straight from the
            // caller's slice, leaving a [KEEP, KEEP+63]-byte remainder.
            if self.nbuf == KEEP && p.len() > BUF_CAP {
                let direct = (p.len() - KEEP) & !63;
                self.blocks_from_buf(KEEP);
                self.blocks(&p[..direct]);
                p = &p[direct..];
                self.nbuf = 0;
            }
            let take = core::cmp::min(BUF_CAP - self.nbuf, p.len());
            self.buf[self.nbuf..self.nbuf + take].copy_from_slice(&p[..take]);
            self.nbuf += take;
            p = &p[take..];
            if self.nbuf < BUF_CAP {
                break;
            }
            // Buffer full: consume whole blocks down to the keep floor.
            let consume = (self.nbuf - KEEP) & !63;
            self.blocks_from_buf(consume);
            self.nbuf -= consume;
            self.buf.copy_within(consume..consume + self.nbuf, 0);
        }
    }

    /// Absorbs `n` bytes from the front of the internal buffer.
    /// Split out so the borrow checker sees a disjoint borrow.
    fn blocks_from_buf(&mut self, n: usize) {
        let mut h = self.h;
        let mut wp = self.wp;
        absorb_blocks(&mut h, &mut wp, &self.buf[..n]);
        self.h = h;
        self.wp = wp;
    }

    fn blocks(&mut self, p: &[u8]) {
        let mut h = self.h;
        let mut wp = self.wp;
        absorb_blocks(&mut h, &mut wp, p);
        self.h = h;
        self.wp = wp;
    }

    /// Returns the 64-bit digest of everything absorbed so far, without
    /// consuming the state.
    #[must_use]
    pub fn finish64(&self) -> u64 {
        if !self.bulk {
            return crate::hayahash64(&self.buf[..self.total as usize], self.seed);
        }
        let (t0, t1, s) = self.tail();
        long_avalanche(s ^ t0 ^ t1.rotate_left(29))
    }

    /// Returns both digest words, without consuming the state. `lo` is
    /// exactly [`finish64`](Digest::finish64).
    #[must_use]
    pub fn finish128(&self) -> Hash128 {
        if !self.bulk {
            return crate::hayahash128(&self.buf[..self.total as usize], self.seed);
        }
        let (t0, t1, s) = self.tail();
        Hash128 {
            lo: long_avalanche(s ^ t0 ^ t1.rotate_left(29)),
            hi: fmix128(s.rotate_left(32) ^ t1.wrapping_add(t0.rotate_left(47))),
        }
    }

    /// Continues the long path over the buffered remainder: the
    /// leftover whole blocks, then the same fold, mid round, wall and
    /// tail as the one-shot. Reads the state without mutating it.
    fn tail(&self) -> (u64, u64, u64) {
        let lenmix = self.total.wrapping_mul(K);
        let s = self.seed ^ K;
        let mut h = self.h;
        let mut wp = self.wp;
        let buf = &self.buf[..self.nbuf];
        let mut off = 0usize;
        let mut l = self.nbuf;

        while l >= 64 {
            absorb_block(&mut h, &mut wp, buf, off);
            off += 64;
            l -= 64;
        }
        h[0] = (h[0] ^ h[4].rotate_left(11)).wrapping_mul(K);
        h[1] = (h[1] ^ h[5].rotate_left(19)).wrapping_mul(K);
        h[2] = (h[2] ^ h[6].rotate_left(31)).wrapping_mul(K);
        h[3] = (h[3] ^ h[7].rotate_left(47)).wrapping_mul(K);

        if l >= 32 {
            // Mid round: same absorb, four lanes over one 32-byte block.
            for (lane, slot) in h.iter_mut().enumerate().take(4) {
                let w = load64le(buf, off + lane * 8);
                *slot = (*slot ^ w.wrapping_add(wp.rotate_left(27))).wrapping_mul(K);
                wp = w;
            }
            off += 32;
            l -= 32;
        }

        h[0] = h[0].wrapping_add(wp.rotate_left(27));
        if l > 16 {
            h[0] = h[0].wrapping_add(injp(buf, off)).wrapping_mul(K);
            h[1] = h[1].wrapping_add(injp(buf, off + 8)).wrapping_mul(K);
        }
        // The last 16 bytes of the stream. KEEP >= 128 guarantees this
        // reach-back stays inside the buffer even when l is small.
        if l > 0 {
            h[2] = h[2].wrapping_add(injp(buf, self.nbuf - 16)).wrapping_mul(K);
            h[3] = h[3].wrapping_add(injp(buf, self.nbuf - 8)).wrapping_mul(K);
        }

        let t0 = (h[0] ^ h[1].rotate_left(13) ^ lenmix).wrapping_mul(K);
        let t1 = (h[2] ^ h[3].rotate_left(33)).wrapping_mul(K);
        (t0, t1, s)
    }
}

/// Absorbs one 64-byte block at `off` into all eight lanes.
#[inline(always)]
fn absorb_block(h: &mut [u64; 8], wp: &mut u64, p: &[u8], off: usize) {
    for (lane, slot) in h.iter_mut().enumerate() {
        let w = load64le(p, off + lane * 8);
        *slot = (*slot ^ w.wrapping_add(wp.rotate_left(27))).wrapping_mul(K);
        *wp = w;
    }
    // Checkpoint the raw-word chain once per block so a 64-stripe
    // rotation orbit cannot hide a difference until it returns to the
    // same lane.
    h[0] = h[0].wrapping_add(*wp);
}

/// Absorbs `p`, whose length must be a multiple of 64.
#[inline(always)]
fn absorb_blocks(h: &mut [u64; 8], wp: &mut u64, p: &[u8]) {
    let mut off = 0;
    while off < p.len() {
        absorb_block(h, wp, p, off);
        off += 64;
    }
}

impl core::fmt::Debug for Digest {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        f.debug_struct("Digest")
            .field("seed", &self.seed)
            .field("len", &self.total)
            .finish_non_exhaustive()
    }
}
