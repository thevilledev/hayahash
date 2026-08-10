// Incremental hashing: absorb input in pieces, digest at any point.
//
// The digest equals hayahash64()/hayahash128() of the concatenation of
// everything written, for every split of that input.
//
// This runs on the pure-BigInt core rather than the wasm engine: the
// wasm module exports only the one-shot entry points, so a streaming
// state would have to live in linear memory and cross the boundary on
// every update. One-shot hashing of a whole buffer still takes the
// wasm fast path.
//
// This is free and unencumbered software released into the public
// domain. For more information, please refer to https://unlicense.org/

import {
	fmix128,
	fmixLong,
	hash128Pure,
	hashPure,
	inj,
	K,
	K_SHL9,
	K_SHL21,
	K_SHL30,
	K_SHL42,
	K_SHR13,
	K_SHR27,
	K_SHR40,
	MASK64,
	rotl,
	type Hash128,
} from "./pure.js";

// Streaming buffer size. Totals below it stay buffered so short and mid
// inputs take the one-shot dispatch at digest time, exactly as the C
// reference does.
const BUF_CAP = 448;

// The floor the buffer is drained to. The digest-time mid/tail phase
// reaches back up to 16 bytes before the current pointer, so the buffer
// has to retain more than that.
const KEEP = 128;

/**
 * A streaming hayahash state.
 *
 * The digest equals the one-shot function over the concatenation of
 * every {@link Hasher.update}, for any split. Digesting does not
 * consume the state, so absorbing may continue afterwards.
 *
 * Not safe for concurrent use.
 */
export class Hasher {
	readonly #h = new BigUint64Array(8);
	readonly #buf = new Uint8Array(BUF_CAP);
	readonly #view: DataView;
	#wp = 0n;
	#seed: bigint;
	#total = 0n;
	#nbuf = 0;
	#bulk = false;

	/** Creates an empty state seeded with `seed`. */
	constructor(seed: bigint = 0n) {
		this.#view = new DataView(this.#buf.buffer);
		this.#seed = seed & MASK64;
		this.#init(this.#seed);
	}

	/** The seed this state was created or last reset with. */
	get seed(): bigint {
		return this.#seed;
	}

	/** Number of bytes absorbed so far. */
	get length(): bigint {
		return this.#total;
	}

	#init(seed: bigint): void {
		const s = seed ^ K;
		const h = this.#h;
		h[0] = s ^ K;
		h[1] = (rotl(s, 17n) + K_SHL21) & MASK64;
		h[2] = rotl(s, 34n) ^ K_SHR13;
		h[3] = (rotl(s, 51n) + K_SHL42) & MASK64;
		h[4] = (s + K_SHR27) & MASK64;
		h[5] = rotl(s, 13n) ^ K_SHL9;
		h[6] = (rotl(s, 26n) + K_SHR40) & MASK64;
		h[7] = rotl(s, 39n) ^ K_SHL30;
		this.#wp = 0n;
		this.#total = 0n;
		this.#nbuf = 0;
		this.#bulk = false;
	}

	/** Discards absorbed input, optionally reseeding. */
	reset(seed?: bigint): void {
		if (seed !== undefined) {
			this.#seed = seed & MASK64;
		}
		this.#init(this.#seed);
	}

	/** Absorbs `data`. */
	update(data: Uint8Array): void {
		let p = data;
		if (p.length === 0) {
			return;
		}
		this.#total = (this.#total + BigInt(p.length)) & MASK64;

		if (!this.#bulk) {
			// Undecided between the one-shot finish and the bulk path:
			// totals up to BUF_CAP-1 stay buffered.
			if (this.#nbuf + p.length < BUF_CAP) {
				this.#buf.set(p, this.#nbuf);
				this.#nbuf += p.length;
				return;
			}
			// Total is now >= 448 > BULK_MIN: commit to the bulk path.
			this.#bulk = true;
		}

		for (;;) {
			// Buffer at its floor with plenty incoming: drain the floor,
			// then stream whole blocks straight from the caller's array,
			// leaving a [KEEP, KEEP+63]-byte remainder for the buffer.
			if (this.#nbuf === KEEP && p.length > BUF_CAP) {
				const direct = (p.length - KEEP) & ~63;
				this.#blocks(this.#view, 0, KEEP);
				const dv = new DataView(p.buffer, p.byteOffset, p.byteLength);
				this.#blocks(dv, 0, direct);
				p = p.subarray(direct);
				this.#nbuf = 0;
			}
			const take = Math.min(BUF_CAP - this.#nbuf, p.length);
			this.#buf.set(p.subarray(0, take), this.#nbuf);
			this.#nbuf += take;
			p = p.subarray(take);
			if (this.#nbuf < BUF_CAP) {
				break;
			}
			// Buffer full: consume whole blocks down to the keep floor.
			const consume = (this.#nbuf - KEEP) & ~63;
			this.#blocks(this.#view, 0, consume);
			this.#nbuf -= consume;
			this.#buf.copyWithin(0, consume, consume + this.#nbuf);
		}
	}

	/** Absorbs `len` bytes from `off`, which must be a multiple of 64. */
	#blocks(view: DataView, off: number, len: number): void {
		const h = this.#h;
		let h0 = h[0] as bigint;
		let h1 = h[1] as bigint;
		let h2 = h[2] as bigint;
		let h3 = h[3] as bigint;
		let h4 = h[4] as bigint;
		let h5 = h[5] as bigint;
		let h6 = h[6] as bigint;
		let h7 = h[7] as bigint;
		let wp = this.#wp;
		for (let i = off; i < off + len; i += 64) {
			let w: bigint;
			w = view.getBigUint64(i, true);
			h0 = ((h0 ^ ((w + rotl(wp, 27n)) & MASK64)) * K) & MASK64;
			wp = w;
			w = view.getBigUint64(i + 8, true);
			h1 = ((h1 ^ ((w + rotl(wp, 27n)) & MASK64)) * K) & MASK64;
			wp = w;
			w = view.getBigUint64(i + 16, true);
			h2 = ((h2 ^ ((w + rotl(wp, 27n)) & MASK64)) * K) & MASK64;
			wp = w;
			w = view.getBigUint64(i + 24, true);
			h3 = ((h3 ^ ((w + rotl(wp, 27n)) & MASK64)) * K) & MASK64;
			wp = w;
			w = view.getBigUint64(i + 32, true);
			h4 = ((h4 ^ ((w + rotl(wp, 27n)) & MASK64)) * K) & MASK64;
			wp = w;
			w = view.getBigUint64(i + 40, true);
			h5 = ((h5 ^ ((w + rotl(wp, 27n)) & MASK64)) * K) & MASK64;
			wp = w;
			w = view.getBigUint64(i + 48, true);
			h6 = ((h6 ^ ((w + rotl(wp, 27n)) & MASK64)) * K) & MASK64;
			wp = w;
			w = view.getBigUint64(i + 56, true);
			h7 = ((h7 ^ ((w + rotl(wp, 27n)) & MASK64)) * K) & MASK64;
			wp = w;
			// Checkpoint the raw-word chain once per block so a 64-stripe
			// rotation orbit cannot hide a difference until it returns to
			// the same lane.
			h0 = (h0 + wp) & MASK64;
		}
		h[0] = h0;
		h[1] = h1;
		h[2] = h2;
		h[3] = h3;
		h[4] = h4;
		h[5] = h5;
		h[6] = h6;
		h[7] = h7;
		this.#wp = wp;
	}

	/**
	 * Returns the 64-bit digest of everything absorbed so far, without
	 * consuming the state.
	 */
	digest64(): bigint {
		if (!this.#bulk) {
			return hashPure(this.#buf.subarray(0, Number(this.#total)), this.#seed);
		}
		const [t0, t1, s] = this.#tail();
		return fmixLong(s ^ t0 ^ rotl(t1, 29n));
	}

	/**
	 * Returns both digest words, without consuming the state. `lo` is
	 * exactly {@link Hasher.digest64}.
	 */
	digest128(): Hash128 {
		if (!this.#bulk) {
			return hash128Pure(this.#buf.subarray(0, Number(this.#total)), this.#seed);
		}
		const [t0, t1, s] = this.#tail();
		return {
			lo: fmixLong(s ^ t0 ^ rotl(t1, 29n)),
			hi: fmix128(rotl(s, 32n) ^ ((t1 + rotl(t0, 47n)) & MASK64)),
		};
	}

	/**
	 * Continues the long path over the buffered remainder: the leftover
	 * whole blocks, then the same fold, mid round, wall and tail as the
	 * one-shot. Reads the state without mutating it.
	 */
	#tail(): [bigint, bigint, bigint] {
		const view = this.#view;
		const lenmix = (this.#total * K) & MASK64;
		const s = this.#seed ^ K;
		const h = this.#h;
		let h0 = h[0] as bigint;
		let h1 = h[1] as bigint;
		let h2 = h[2] as bigint;
		let h3 = h[3] as bigint;
		let h4 = h[4] as bigint;
		let h5 = h[5] as bigint;
		let h6 = h[6] as bigint;
		let h7 = h[7] as bigint;
		let wp = this.#wp;
		let p = 0;
		let l = this.#nbuf;
		const injp = (i: number): bigint => inj(view.getBigUint64(i, true));

		while (l >= 64) {
			let w: bigint;
			w = view.getBigUint64(p, true);
			h0 = ((h0 ^ ((w + rotl(wp, 27n)) & MASK64)) * K) & MASK64;
			wp = w;
			w = view.getBigUint64(p + 8, true);
			h1 = ((h1 ^ ((w + rotl(wp, 27n)) & MASK64)) * K) & MASK64;
			wp = w;
			w = view.getBigUint64(p + 16, true);
			h2 = ((h2 ^ ((w + rotl(wp, 27n)) & MASK64)) * K) & MASK64;
			wp = w;
			w = view.getBigUint64(p + 24, true);
			h3 = ((h3 ^ ((w + rotl(wp, 27n)) & MASK64)) * K) & MASK64;
			wp = w;
			w = view.getBigUint64(p + 32, true);
			h4 = ((h4 ^ ((w + rotl(wp, 27n)) & MASK64)) * K) & MASK64;
			wp = w;
			w = view.getBigUint64(p + 40, true);
			h5 = ((h5 ^ ((w + rotl(wp, 27n)) & MASK64)) * K) & MASK64;
			wp = w;
			w = view.getBigUint64(p + 48, true);
			h6 = ((h6 ^ ((w + rotl(wp, 27n)) & MASK64)) * K) & MASK64;
			wp = w;
			w = view.getBigUint64(p + 56, true);
			h7 = ((h7 ^ ((w + rotl(wp, 27n)) & MASK64)) * K) & MASK64;
			wp = w;
			// Checkpoint the raw-word chain once per block so a 64-stripe
			// rotation orbit cannot hide a difference until it returns to
			// the same lane.
			h0 = (h0 + wp) & MASK64;
			p += 64;
			l -= 64;
		}
		h0 = ((h0 ^ rotl(h4, 11n)) * K) & MASK64;
		h1 = ((h1 ^ rotl(h5, 19n)) * K) & MASK64;
		h2 = ((h2 ^ rotl(h6, 31n)) * K) & MASK64;
		h3 = ((h3 ^ rotl(h7, 47n)) * K) & MASK64;

		if (l >= 32) {
			let w: bigint;
			w = view.getBigUint64(p, true);
			h0 = ((h0 ^ ((w + rotl(wp, 27n)) & MASK64)) * K) & MASK64;
			wp = w;
			w = view.getBigUint64(p + 8, true);
			h1 = ((h1 ^ ((w + rotl(wp, 27n)) & MASK64)) * K) & MASK64;
			wp = w;
			w = view.getBigUint64(p + 16, true);
			h2 = ((h2 ^ ((w + rotl(wp, 27n)) & MASK64)) * K) & MASK64;
			wp = w;
			w = view.getBigUint64(p + 24, true);
			h3 = ((h3 ^ ((w + rotl(wp, 27n)) & MASK64)) * K) & MASK64;
			wp = w;
			p += 32;
			l -= 32;
		}

		h0 = (h0 + rotl(wp, 27n)) & MASK64;
		if (l > 16) {
			h0 = ((h0 + injp(p)) * K) & MASK64;
			h1 = ((h1 + injp(p + 8)) * K) & MASK64;
		}
		// The last 16 bytes of the stream. KEEP >= 128 guarantees this
		// reach-back stays inside the buffer even when l is small.
		if (l > 0) {
			h2 = ((h2 + injp(this.#nbuf - 16)) * K) & MASK64;
			h3 = ((h3 + injp(this.#nbuf - 8)) * K) & MASK64;
		}

		return [
			((h0 ^ rotl(h1, 13n) ^ lenmix) * K) & MASK64,
			((h2 ^ rotl(h3, 33n)) * K) & MASK64,
			s,
		];
	}
}
