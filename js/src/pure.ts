// Pure-JavaScript (BigInt) port of the reference C implementation
// (hayahash.h at the repository root); see that header for the full
// design notes. Bit-exact with the reference for every input and
// seed, and independent of host endianness.
//
// This is the fallback engine for environments without WebAssembly
// (or where a Content Security Policy forbids compiling it). BigInt
// arithmetic is heap-allocating, so this path is roughly two orders
// of magnitude slower than the wasm engine; correctness, not speed,
// is its job. It also serves as an independent implementation the
// test suite cross-checks the wasm module against.
//
// This is free and unencumbered software released into the public
// domain. For more information, please refer to <https://unlicense.org/>

const MASK64 = 0xffffffffffffffffn;

// Multiplier: 2^64 / golden ratio, odd.
const K = 0x9e3779b97f4a7c15n;
// moremur finalizer constants (Pelle Evensen); M1 doubles as the
// second multiplier of the short path.
const M1 = 0x3c79ac492ba7b653n;
const M2 = 0x1c69b3f74ac4ae35n;

// Shifted copies of K used for lane IVs, precomputed and masked so
// the hash body never mixes an over-wide value into an xor.
const K_SHL9 = (K << 9n) & MASK64;
const K_SHL21 = (K << 21n) & MASK64;
const K_SHL30 = (K << 30n) & MASK64;
const K_SHL42 = (K << 42n) & MASK64;
const K_SHR13 = K >> 13n;
const K_SHR19 = K >> 19n;
const K_SHR27 = K >> 27n;
const K_SHR40 = K >> 40n;

// Input length (in bytes) at or above which the 8-lane bulk loop
// kicks in.
const BULK_MIN = 320;

function rotl(x: bigint, n: bigint): bigint {
	return ((x << n) & MASK64) | (x >> (64n - n));
}

// moremur finalizer (Pelle Evensen).
function fmix(x: bigint): bigint {
	x ^= x >> 27n;
	x = (x * M1) & MASK64;
	x ^= x >> 33n;
	x = (x * M2) & MASK64;
	x ^= x >> 27n;
	return x;
}

// Bijective stripe injections; see hayahash.h for why the short path
// needs a second one with different rotation amounts.
function inj(w: bigint): bigint {
	return w ^ rotl(w, 21n) ^ rotl(w, 41n);
}

function inj2(w: bigint): bigint {
	return w ^ rotl(w, 11n) ^ rotl(w, 50n);
}

// Core of the pure engine. `seed` must already be masked to 64 bits;
// the public wrappers in index.ts take care of that. Sums feeding a
// multiply or another sum are left unmasked where modular arithmetic
// makes the deferred mask equivalent; every value that meets an xor
// or rotl is masked first.
export function hashPure(data: Uint8Array, seed: bigint): bigint {
	const len = data.length;
	const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
	const load64 = (i: number): bigint => view.getBigUint64(i, true);
	const load32 = (i: number): bigint => BigInt(view.getUint32(i, true));
	const injp = (i: number): bigint => inj(load64(i));

	let l = len;
	let p = 0;
	// Seed & length premix; feeds every path so length extension and
	// overlapping tail reads can never collide across lengths.
	const s = seed ^ ((BigInt(len) * K) & MASK64);

	if (l <= 16) {
		let a: bigint;
		let b: bigint;
		if (l >= 8) {
			a = load64(0);
			b = load64(l - 8);
		} else if (l >= 4) {
			a = load32(0);
			b = load32(l - 4);
		} else if (l > 0) {
			// 1..3 bytes: head, middle, tail.
			a = BigInt(data[0]);
			b = (BigInt(data[l >> 1]) << 8n) | (BigInt(data[l - 1]) << 16n);
		} else {
			a = 0n;
			b = 0n;
		}
		const x = ((inj(a) ^ s ^ K) * K) & MASK64;
		const y = ((inj2(b) ^ rotl(s, 23n) ^ K_SHR19) * M1) & MASK64;
		return fmix(rotl(x, 27n) ^ y);
	}

	let h0 = s ^ K;
	let h1 = (rotl(s, 17n) + K_SHL21) & MASK64;
	let h2 = rotl(s, 34n) ^ K_SHR13;
	let h3 = (rotl(s, 51n) + K_SHL42) & MASK64;
	let w = 0n;
	let wp = 0n;

	if (l >= BULK_MIN) {
		let h4 = (s + K_SHR27) & MASK64;
		let h5 = rotl(s, 13n) ^ K_SHL9;
		let h6 = (rotl(s, 26n) + K_SHR40) & MASK64;
		let h7 = rotl(s, 39n) ^ K_SHL30;
		do {
			w = load64(p);
			h0 = ((h0 + w + rotl(wp, 27n)) * K) & MASK64;
			wp = w;
			w = load64(p + 8);
			h1 = ((h1 + w + rotl(wp, 27n)) * K) & MASK64;
			wp = w;
			w = load64(p + 16);
			h2 = ((h2 + w + rotl(wp, 27n)) * K) & MASK64;
			wp = w;
			w = load64(p + 24);
			h3 = ((h3 + w + rotl(wp, 27n)) * K) & MASK64;
			wp = w;
			w = load64(p + 32);
			h4 = ((h4 + w + rotl(wp, 27n)) * K) & MASK64;
			wp = w;
			w = load64(p + 40);
			h5 = ((h5 + w + rotl(wp, 27n)) * K) & MASK64;
			wp = w;
			w = load64(p + 48);
			h6 = ((h6 + w + rotl(wp, 27n)) * K) & MASK64;
			wp = w;
			w = load64(p + 56);
			h7 = ((h7 + w + rotl(wp, 27n)) * K) & MASK64;
			wp = w;
			p += 64;
			l -= 64;
		} while (l >= 64);
		h0 = ((h0 ^ rotl(h4, 11n)) * K) & MASK64;
		h1 = ((h1 ^ rotl(h5, 19n)) * K) & MASK64;
		h2 = ((h2 ^ rotl(h6, 31n)) * K) & MASK64;
		h3 = ((h3 ^ rotl(h7, 47n)) * K) & MASK64;
	}

	// Mid loop: same absorb as the bulk loop, 4 lanes over 32-byte
	// blocks; wp chains in from the bulk loop.
	for (; l >= 32; l -= 32, p += 32) {
		w = load64(p);
		h0 = ((h0 + w + rotl(wp, 27n)) * K) & MASK64;
		wp = w;
		w = load64(p + 8);
		h1 = ((h1 + w + rotl(wp, 27n)) * K) & MASK64;
		wp = w;
		w = load64(p + 16);
		h2 = ((h2 + w + rotl(wp, 27n)) * K) & MASK64;
		wp = w;
		w = load64(p + 24);
		h3 = ((h3 + w + rotl(wp, 27n)) * K) & MASK64;
		wp = w;
	}

	// Absorb the final loop stripe's dangling rotated copy.
	h0 = (h0 + rotl(wp, 27n)) & MASK64;

	// 0..31 bytes left.
	if (l > 16) {
		h0 = ((h0 + injp(p)) * K) & MASK64;
		h1 = ((h1 + injp(p + 8)) * K) & MASK64;
		p += 16;
		l -= 16;
	}
	// 0..16 bytes left; total length > 16, so reading the last 16
	// input bytes (overlapping already-hashed data) is always valid.
	if (l > 0) {
		h2 = ((h2 + injp(p + l - 16)) * K) & MASK64;
		h3 = ((h3 + injp(p + l - 8)) * K) & MASK64;
	}

	const t0 = ((h0 ^ rotl(h1, 13n)) * K) & MASK64;
	const t1 = ((h2 ^ rotl(h3, 33n)) * K) & MASK64;
	return fmix(s ^ t0 ^ rotl(t1, 29n));
}
