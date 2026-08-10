package hayahash

import (
	"encoding/binary"
	"hash"
	"math/bits"
)

const (
	// bufCap is the streaming buffer size. Totals below it stay
	// buffered so short and mid inputs take the one-shot dispatch at
	// digest time, exactly as the C reference does.
	bufCap = 448
	// keep is the floor the buffer is drained to. The digest-time
	// mid/tail phase reaches back up to 16 bytes before the current
	// pointer, so the buffer has to retain more than that.
	keep = 128
)

// Digest is a streaming hayahash state. It absorbs input incrementally
// and produces the same digest as [Hash64] or [Hash128] over the
// concatenation of everything written, for every split of that input.
//
// A Digest is not safe for concurrent use.
//
// It implements [hash.Hash64], so it can be dropped into anything that
// takes one. Note that [Digest.Sum] appends the 64-bit digest in
// big-endian order, matching hash/crc64 and the rest of the standard
// library.
type Digest struct {
	h     [8]uint64
	wp    uint64
	seed  uint64
	total uint64
	nbuf  int
	bulk  bool
	buf   [bufCap]byte
}

var _ hash.Hash64 = (*Digest)(nil)

// New returns a streaming Digest seeded with seed.
func New(seed uint64) *Digest {
	d := &Digest{}
	d.Reset64(seed)
	return d
}

// Reset64 restarts the digest with a new seed, discarding any buffered
// input.
func (d *Digest) Reset64(seed uint64) {
	s := seed ^ k
	d.h[0] = s ^ k
	d.h[1] = bits.RotateLeft64(s, 17) + k<<21&mask64
	d.h[2] = bits.RotateLeft64(s, 34) ^ k>>13
	d.h[3] = bits.RotateLeft64(s, 51) + k<<42&mask64
	d.h[4] = s + k>>27
	d.h[5] = bits.RotateLeft64(s, 13) ^ k<<9&mask64
	d.h[6] = bits.RotateLeft64(s, 26) + k>>40
	d.h[7] = bits.RotateLeft64(s, 39) ^ k<<30&mask64
	d.wp = 0
	d.seed = seed
	d.total = 0
	d.nbuf = 0
	d.bulk = false
}

// Reset restarts the digest, keeping the current seed. It is the
// [hash.Hash] spelling of [Digest.Reset64].
func (d *Digest) Reset() { d.Reset64(d.seed) }

// Size returns the digest length in bytes, 8. It reports the 64-bit
// width because that is what [Digest.Sum] writes; [Digest.Sum128] is
// available separately.
func (d *Digest) Size() int { return 8 }

// BlockSize returns the absorb block size in bytes, 64.
func (d *Digest) BlockSize() int { return 64 }

// blocks absorbs len(p) bytes, which must be a non-negative multiple of
// 64, through the 8-lane bulk loop.
func (d *Digest) blocks(p []byte) {
	h0, h1, h2, h3 := d.h[0], d.h[1], d.h[2], d.h[3]
	h4, h5, h6, h7 := d.h[4], d.h[5], d.h[6], d.h[7]
	wp := d.wp
	for off := 0; off < len(p); off += 64 {
		h0, wp = stripe(h0, wp, p, off)
		h1, wp = stripe(h1, wp, p, off+8)
		h2, wp = stripe(h2, wp, p, off+16)
		h3, wp = stripe(h3, wp, p, off+24)
		h4, wp = stripe(h4, wp, p, off+32)
		h5, wp = stripe(h5, wp, p, off+40)
		h6, wp = stripe(h6, wp, p, off+48)
		h7, wp = stripe(h7, wp, p, off+56)
		h0 += wp
	}
	d.h[0], d.h[1], d.h[2], d.h[3] = h0, h1, h2, h3
	d.h[4], d.h[5], d.h[6], d.h[7] = h4, h5, h6, h7
	d.wp = wp
}

// Write absorbs p. It never returns an error.
func (d *Digest) Write(p []byte) (int, error) {
	n := len(p)
	if n == 0 {
		return 0, nil
	}
	d.total += uint64(n)

	if !d.bulk {
		// Undecided between the one-shot finish and the bulk path:
		// totals up to bufCap-1 stay buffered.
		if d.nbuf+n < bufCap {
			copy(d.buf[d.nbuf:], p)
			d.nbuf += n
			return n, nil
		}
		// Total is now >= 448 > bulkMin: commit to the bulk path.
		d.bulk = true
	}

	for {
		// Buffer at its floor with plenty incoming: drain the floor,
		// then stream whole blocks straight from the caller's slice,
		// leaving a [keep, keep+63]-byte remainder for the buffer.
		if d.nbuf == keep && len(p) > bufCap {
			direct := (len(p) - keep) &^ 63
			d.blocks(d.buf[:keep])
			d.blocks(p[:direct])
			p = p[direct:]
			d.nbuf = 0
		}
		take := bufCap - d.nbuf
		if take > len(p) {
			take = len(p)
		}
		copy(d.buf[d.nbuf:], p[:take])
		d.nbuf += take
		p = p[take:]
		if d.nbuf < bufCap {
			break
		}
		// Buffer full: consume whole blocks down to the keep floor.
		consume := (d.nbuf - keep) &^ 63
		d.blocks(d.buf[:consume])
		d.nbuf -= consume
		copy(d.buf[:d.nbuf], d.buf[consume:consume+d.nbuf])
	}
	return n, nil
}

// Sum appends the current 64-bit digest to b in big-endian order and
// returns the result. It does not modify the state.
func (d *Digest) Sum(b []byte) []byte {
	var out [8]byte
	binary.BigEndian.PutUint64(out[:], d.Sum64())
	return append(b, out[:]...)
}

// Sum64 returns the digest of everything written so far. It does not
// modify the state, so writing may continue afterwards.
func (d *Digest) Sum64() uint64 {
	if !d.bulk {
		return Hash64(d.buf[:d.total], d.seed)
	}
	t0, t1, s := d.finish()
	return longAvalanche(s ^ t0 ^ bits.RotateLeft64(t1, 29))
}

// Sum128 returns both digest words for everything written so far. Lo is
// exactly [Digest.Sum64]. It does not modify the state.
func (d *Digest) Sum128() Digest128 {
	if !d.bulk {
		return Hash128(d.buf[:d.total], d.seed)
	}
	t0, t1, s := d.finish()
	return Digest128{
		Lo: longAvalanche(s ^ t0 ^ bits.RotateLeft64(t1, 29)),
		Hi: fmix128(bits.RotateLeft64(s, 32) ^ (t1 + bits.RotateLeft64(t0, 47))),
	}
}

// finish continues the long path over the buffered remainder: the
// leftover whole blocks, then the same fold, mid round, wall, and tail
// as Hash64. It reads the state without mutating it.
func (d *Digest) finish() (t0, t1, s uint64) {
	lenmix := d.total * k
	s = d.seed ^ k
	h0, h1, h2, h3 := d.h[0], d.h[1], d.h[2], d.h[3]
	h4, h5, h6, h7 := d.h[4], d.h[5], d.h[6], d.h[7]
	wp := d.wp
	buf := d.buf[:d.nbuf]
	off := 0
	l := d.nbuf

	for l >= 64 {
		h0, wp = stripe(h0, wp, buf, off)
		h1, wp = stripe(h1, wp, buf, off+8)
		h2, wp = stripe(h2, wp, buf, off+16)
		h3, wp = stripe(h3, wp, buf, off+24)
		h4, wp = stripe(h4, wp, buf, off+32)
		h5, wp = stripe(h5, wp, buf, off+40)
		h6, wp = stripe(h6, wp, buf, off+48)
		h7, wp = stripe(h7, wp, buf, off+56)
		h0 += wp
		off += 64
		l -= 64
	}
	h0 = (h0 ^ bits.RotateLeft64(h4, 11)) * k
	h1 = (h1 ^ bits.RotateLeft64(h5, 19)) * k
	h2 = (h2 ^ bits.RotateLeft64(h6, 31)) * k
	h3 = (h3 ^ bits.RotateLeft64(h7, 47)) * k

	if l >= 32 {
		h0, wp = stripe(h0, wp, buf, off)
		h1, wp = stripe(h1, wp, buf, off+8)
		h2, wp = stripe(h2, wp, buf, off+16)
		h3, wp = stripe(h3, wp, buf, off+24)
		off += 32
		l -= 32
	}

	h0 += bits.RotateLeft64(wp, 27)
	if l > 16 {
		h0 = (h0 + injp(buf, off)) * k
		h1 = (h1 + injp(buf, off+8)) * k
	}
	// The last 16 bytes of the stream. keep >= 128 guarantees this
	// reach-back stays inside the buffer even when l is small.
	if l > 0 {
		h2 = (h2 + injp(buf, d.nbuf-16)) * k
		h3 = (h3 + injp(buf, d.nbuf-8)) * k
	}

	t0 = (h0 ^ bits.RotateLeft64(h1, 13) ^ lenmix) * k
	t1 = (h2 ^ bits.RotateLeft64(h3, 33)) * k
	return t0, t1, s
}
