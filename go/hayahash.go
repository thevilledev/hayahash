// Package hayahash implements hayahash64 and hayahash128: small, fast,
// portable hash functions.
//
// It is a bit-exact Go port of the reference C implementation
// (hayahash.h at the repository root); see that header for the full
// design notes. Output is identical for every input and seed on every
// platform, and is independent of host endianness.
//
// This is free and unencumbered software released into the public
// domain. For more information, please refer to https://unlicense.org/
package hayahash

import (
	"encoding/binary"
	"math/bits"
)

const (
	// k is the multiplier: 2^64 / golden ratio, odd.
	k = 0x9E3779B97F4A7C15
	// m1 and m2 are the moremur finalizer constants (Pelle Evensen);
	// m1 doubles as the second multiplier of the short path.
	m1 = 0x3C79AC492BA7B653
	m2 = 0x1C69B3F74AC4AE35
	// n1 and n2 are the MurmurHash3 finalizer constants used only by
	// the high word.
	n1 = 0xFF51AFD7ED558CCD
	n2 = 0xC4CEB9FE1A85EC53

	// bulkMin is the input length (in bytes) at or above which the
	// 8-lane bulk loop kicks in.
	bulkMin = 320

	// mask64 truncates Go's exact constant arithmetic back to 64 bits
	// where the reference derives lane constants from wrapping uint64
	// shifts of k.
	mask64 = 1<<64 - 1
)

func load64le(p []byte, i int) uint64 {
	return binary.LittleEndian.Uint64(p[i:])
}

func load32le(p []byte, i int) uint64 {
	return uint64(binary.LittleEndian.Uint32(p[i:]))
}

// fmix is the moremur finalizer (Pelle Evensen).
func fmix(x uint64) uint64 {
	x ^= x >> 27
	x *= m1
	x ^= x >> 33
	x *= m2
	return x ^ x>>27
}

// fmix128 is the bijective finalizer for the high word.
func fmix128(x uint64) uint64 {
	x ^= x >> 30
	x *= n1
	x ^= x >> 31
	x *= n2
	return x ^ x>>33
}

// longAvalanche is the one-multiply finalizer for the already-mixed
// long path.
func longAvalanche(x uint64) uint64 {
	x ^= x >> 37
	x *= k
	return x ^ x>>32
}

// inj and inj2 are bijective stripe injections (any odd number of
// rotation terms is invertible over GF(2)). The short path uses a
// second injection with different rotation amounts for its b word so
// the two multiply terms can never be erased simultaneously by one
// sparse difference.
func inj(w uint64) uint64 {
	return w ^ bits.RotateLeft64(w, 21) ^ bits.RotateLeft64(w, 41)
}

func inj2(w uint64) uint64 {
	return w ^ bits.RotateLeft64(w, 11) ^ bits.RotateLeft64(w, 50)
}

func injp(p []byte, i int) uint64 {
	return inj(load64le(p, i))
}

// stripe absorbs the 8-byte stripe at key[i:] into lane h and returns
// the updated lane and the stripe itself (the next lane's wp):
// h' = (h ^ (w + rotl(wp, 27))) * k.
func stripe(h, wp uint64, key []byte, i int) (uint64, uint64) {
	w := load64le(key, i)
	return (h ^ (w + bits.RotateLeft64(wp, 27))) * k, w
}

// Digest128 is a 128-bit digest represented as two words. Lo is exactly
// Hash64 for the same input and seed.
type Digest128 struct {
	Lo uint64
	Hi uint64
}

// Hash64 hashes key with seed, returning a 64-bit digest.
//
// It produces exactly the same value as the C reference hayahash64()
// for all inputs, seeds, and host endiannesses.
func Hash64(key []byte, seed uint64) uint64 {
	n := len(key)
	// The seed premixes into s; the length is absorbed in the
	// finalizer instead (through a multiply against state), so the
	// digest is a pure function of (seed, bytes-so-far) and a
	// streaming implementation can match it without knowing the total
	// length up front. len -> len*K is injective, which keeps the
	// overlapping tail reads collision-free across lengths.
	lenmix := uint64(n) * k
	s := seed ^ k

	if n <= 16 {
		var a, b uint64
		switch {
		case n >= 8:
			a = load64le(key, 0)
			b = load64le(key, n-8)
		case n >= 4:
			a = load32le(key, 0)
			b = load32le(key, n-4)
		case n > 0:
			// 1..3 bytes: head, middle, tail (ChibiHash v2 trick).
			a = uint64(key[0])
			b = uint64(key[n>>1])<<8 | uint64(key[n-1])<<16
		}
		// Two independent multiplies, then a strong finalizer; see the
		// C header for why each word gets its own injection.
		x := (inj(a) ^ s ^ k) * k
		y := (inj2(b) ^ bits.RotateLeft64(s, 23) ^ k>>19) * m1
		return fmix(bits.RotateLeft64(x, 27) ^ y ^ lenmix)
	}

	h0 := s ^ k
	h1 := bits.RotateLeft64(s, 17) + k<<21&mask64
	h2 := bits.RotateLeft64(s, 34) ^ k>>13
	h3 := bits.RotateLeft64(s, 51) + k<<42&mask64
	wp := uint64(0)
	off := 0
	l := n

	if l >= bulkMin {
		h4 := s + k>>27
		h5 := bits.RotateLeft64(s, 13) ^ k<<9&mask64
		h6 := bits.RotateLeft64(s, 26) + k>>40
		h7 := bits.RotateLeft64(s, 39) ^ k<<30&mask64
		for {
			h0, wp = stripe(h0, wp, key, off)
			h1, wp = stripe(h1, wp, key, off+8)
			h2, wp = stripe(h2, wp, key, off+16)
			h3, wp = stripe(h3, wp, key, off+24)
			h4, wp = stripe(h4, wp, key, off+32)
			h5, wp = stripe(h5, wp, key, off+40)
			h6, wp = stripe(h6, wp, key, off+48)
			h7, wp = stripe(h7, wp, key, off+56)
			// Checkpoint the raw-word chain once per block so a
			// 64-stripe rotation orbit cannot hide a difference until
			// it returns to the same lane.
			h0 += wp
			off += 64
			l -= 64
			if l < 64 {
				break
			}
		}
		// Fold the upper lanes in with xor + multiply: xor merges of
		// multiply spreads can only cancel by carry-pattern luck.
		h0 = (h0 ^ bits.RotateLeft64(h4, 11)) * k
		h1 = (h1 ^ bits.RotateLeft64(h5, 19)) * k
		h2 = (h2 ^ bits.RotateLeft64(h6, 31)) * k
		h3 = (h3 ^ bits.RotateLeft64(h7, 47)) * k
	}

	// Mid loop: same absorb as the bulk loop, 4 lanes over 32-byte
	// blocks; wp chains in from the bulk loop.
	for l >= 32 {
		h0, wp = stripe(h0, wp, key, off)
		h1, wp = stripe(h1, wp, key, off+8)
		h2, wp = stripe(h2, wp, key, off+16)
		h3, wp = stripe(h3, wp, key, off+24)
		off += 32
		l -= 32
	}

	// Absorb the final loop stripe's dangling rotated copy so
	// difference ladders cannot march off the end of the input.
	h0 += bits.RotateLeft64(wp, 27)

	// 0..31 bytes left.
	if l > 16 {
		h0 = (h0 + injp(key, off)) * k
		h1 = (h1 + injp(key, off+8)) * k
		l -= 16
	}
	// 0..16 bytes left; total length > 16, so reading the last 16
	// input bytes (overlapping already-hashed data) is always valid.
	// Length is folded into t0 below, through a multiply.
	if l > 0 {
		h2 = (h2 + injp(key, n-16)) * k
		h3 = (h3 + injp(key, n-8)) * k
	}

	t0 := (h0 ^ bits.RotateLeft64(h1, 13) ^ lenmix) * k
	t1 := (h2 ^ bits.RotateLeft64(h3, 33)) * k
	return longAvalanche(s ^ t0 ^ bits.RotateLeft64(t1, 29))
}

// Hash128 hashes key with seed, returning both 64-bit output words.
// The input is walked once and the returned Lo word is exactly Hash64
// for the same input and seed.
func Hash128(key []byte, seed uint64) Digest128 {
	n := len(key)
	lenmix := uint64(n) * k
	s := seed ^ k

	if n <= 16 {
		var a, b uint64
		switch {
		case n >= 8:
			a = load64le(key, 0)
			b = load64le(key, n-8)
		case n >= 4:
			a = load32le(key, 0)
			b = load32le(key, n-4)
		case n > 0:
			a = uint64(key[0])
			b = uint64(key[n>>1])<<8 | uint64(key[n-1])<<16
		}
		x := (inj(a) ^ s ^ k) * k
		y := (inj2(b) ^ bits.RotateLeft64(s, 23) ^ k>>19) * m1
		u := bits.RotateLeft64(x, 27) ^ y ^ lenmix
		return Digest128{Lo: fmix(u), Hi: fmix128(x + bits.RotateLeft64(u, 32))}
	}

	h0 := s ^ k
	h1 := bits.RotateLeft64(s, 17) + k<<21&mask64
	h2 := bits.RotateLeft64(s, 34) ^ k>>13
	h3 := bits.RotateLeft64(s, 51) + k<<42&mask64
	wp := uint64(0)
	off := 0
	l := n

	if l >= bulkMin {
		h4 := s + k>>27
		h5 := bits.RotateLeft64(s, 13) ^ k<<9&mask64
		h6 := bits.RotateLeft64(s, 26) + k>>40
		h7 := bits.RotateLeft64(s, 39) ^ k<<30&mask64
		for {
			h0, wp = stripe(h0, wp, key, off)
			h1, wp = stripe(h1, wp, key, off+8)
			h2, wp = stripe(h2, wp, key, off+16)
			h3, wp = stripe(h3, wp, key, off+24)
			h4, wp = stripe(h4, wp, key, off+32)
			h5, wp = stripe(h5, wp, key, off+40)
			h6, wp = stripe(h6, wp, key, off+48)
			h7, wp = stripe(h7, wp, key, off+56)
			h0 += wp
			off += 64
			l -= 64
			if l < 64 {
				break
			}
		}
		h0 = (h0 ^ bits.RotateLeft64(h4, 11)) * k
		h1 = (h1 ^ bits.RotateLeft64(h5, 19)) * k
		h2 = (h2 ^ bits.RotateLeft64(h6, 31)) * k
		h3 = (h3 ^ bits.RotateLeft64(h7, 47)) * k
	}

	for l >= 32 {
		h0, wp = stripe(h0, wp, key, off)
		h1, wp = stripe(h1, wp, key, off+8)
		h2, wp = stripe(h2, wp, key, off+16)
		h3, wp = stripe(h3, wp, key, off+24)
		off += 32
		l -= 32
	}

	h0 += bits.RotateLeft64(wp, 27)
	if l > 16 {
		h0 = (h0 + injp(key, off)) * k
		h1 = (h1 + injp(key, off+8)) * k
		l -= 16
	}
	if l > 0 {
		h2 = (h2 + injp(key, n-16)) * k
		h3 = (h3 + injp(key, n-8)) * k
	}

	t0 := (h0 ^ bits.RotateLeft64(h1, 13) ^ lenmix) * k
	t1 := (h2 ^ bits.RotateLeft64(h3, 33)) * k
	return Digest128{
		Lo: longAvalanche(s ^ t0 ^ bits.RotateLeft64(t1, 29)),
		Hi: fmix128(bits.RotateLeft64(s, 32) ^ (t1 + bits.RotateLeft64(t0, 47))),
	}
}
