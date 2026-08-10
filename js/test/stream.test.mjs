// Streaming conformance: every split of an input must produce the
// one-shot digest, and digesting must not consume the state.
//
// The one-shot side runs on whichever engine is active (wasm by
// default), so these also check the streaming core against wasm.

import assert from "node:assert/strict";
import test from "node:test";

import {
	Hasher,
	PureHasher,
	getEngine,
	hayahash128,
	hayahash128Pure,
	hayahash64,
	hayahash64Pure,
} from "../dist/index.js";

const K = 0x9e3779b97f4a7c15n;
const MASK64 = 0xffffffffffffffffn;

/** The shared portable input fill used by the KAT tables. */
function patternA(n) {
	const out = new Uint8Array(n);
	for (let i = 0; i < n; i++) {
		out[i] = Number(((BigInt(i) * K + 0x2545f4914f6cdd1dn) & MASK64) >> 56n);
	}
	return out;
}

// Chunk sizes that straddle the 448-byte buffer, the 128-byte keep
// floor and the 64-byte block.
const SPLITS = [1, 7, 64, 127, 448, 449, Number.MAX_SAFE_INTEGER];

function feed(h, data, chunk) {
	for (let i = 0; i < data.length; ) {
		const n = Math.min(chunk, data.length - i);
		h.update(data.subarray(i, i + n));
		i += n;
	}
}

const LENGTHS = [
	...Array.from({ length: 641 }, (_, i) => i),
	895, 896, 897, 1023, 1024, 1025, 4096, 20000, 131073,
];

test("streaming matches one-shot", () => {
	for (const seed of [0n, K, 0xdeadbeefcafebaben]) {
		for (const n of LENGTHS) {
			const data = patternA(n);
			const want64 = hayahash64(data, seed);
			const want128 = hayahash128(data, seed);
			for (const chunk of SPLITS) {
				const h = new Hasher(seed);
				feed(h, data, chunk);
				assert.equal(h.digest64(), want64, `len=${n} seed=${seed} chunk=${chunk}`);
				const got = h.digest128();
				assert.equal(got.lo, want128.lo, `lo len=${n} chunk=${chunk}`);
				assert.equal(got.hi, want128.hi, `hi len=${n} chunk=${chunk}`);
				assert.equal(got.lo, want64);
			}
		}
	}
});

test("streaming matches the pure engine too", () => {
	for (const n of [0, 1, 100, 448, 449, 1000, 5000]) {
		const data = patternA(n);
		const h = new Hasher(3n);
		feed(h, data, 37);
		assert.equal(h.digest64(), hayahash64Pure(data, 3n), `len=${n}`);
		assert.equal(h.digest128().hi, hayahash128Pure(data, 3n).hi, `len=${n}`);
	}
});

test("digest is non-destructive", () => {
	const total = 2000;
	const data = patternA(total);
	for (const cut of [0, 1, 63, 64, 447, 448, 449, 1000, total]) {
		const h = new Hasher(7n);
		h.update(data.subarray(0, cut));
		const first = h.digest64();
		assert.equal(first, h.digest64(), `repeated digest differs at cut=${cut}`);
		assert.equal(first, hayahash64(data.subarray(0, cut), 7n), `cut=${cut}`);
		assert.equal(h.digest128().lo, first, `cut=${cut}`);
		h.update(data.subarray(cut));
		assert.equal(h.digest64(), hayahash64(data, 7n), `continued at cut=${cut}`);
	}
});

test("empty and zero-length updates", () => {
	const h = new Hasher();
	assert.equal(h.digest64(), hayahash64(new Uint8Array(0), 0n));
	h.update(new Uint8Array(0));
	assert.equal(h.digest64(), hayahash64(new Uint8Array(0), 0n));
	assert.equal(h.length, 0n);

	const data = patternA(500);
	h.update(data.subarray(0, 200));
	h.update(new Uint8Array(0));
	h.update(data.subarray(200));
	assert.equal(h.digest64(), hayahash64(data, 0n));
	assert.equal(h.length, 500n);
});

test("reset keeps or replaces the seed", () => {
	const data = patternA(1000);
	const h = new Hasher(0xabcdn);
	h.update(data);
	h.reset();
	assert.equal(h.length, 0n);
	h.update(data.subarray(0, 10));
	assert.equal(h.digest64(), hayahash64(data.subarray(0, 10), 0xabcdn));
	h.reset(1n);
	assert.equal(h.seed, 1n);
	h.update(data.subarray(0, 10));
	assert.equal(h.digest64(), hayahash64(data.subarray(0, 10), 1n));
});

test("subarrays with a byte offset hash like copies", () => {
	const backing = patternA(1000);
	const view = backing.subarray(137, 863);
	const h = new Hasher(9n);
	feed(h, view, 61);
	assert.equal(h.digest64(), hayahash64(Uint8Array.from(view), 9n));
});

// Pins the "streaming equality samples" section of
// test_vectors/v0.5.0.txt, which until now no port consumed.
test("published streaming vectors", () => {
	const vectors = [
		[0, 0x68ac507cf298ca3fn],
		[5, 0x37ee1f8b5a98b84bn],
		[10, 0xe28b66fb1e4cb4ean],
		[15, 0x9a8920a57f119d6bn],
		[20, 0xc311e14ff31fb2bfn],
		[25, 0xc27fde4ac86cce54n],
		[30, 0x16cc1e65ca2cb4f3n],
		[35, 0x1c6522bdc246da12n],
		[40, 0xd110128d567cb9f8n],
	];
	for (const [len, want] of vectors) {
		const h = new Hasher(0n);
		for (const b of patternA(len)) {
			h.update(Uint8Array.of(b));
		}
		assert.equal(h.digest64(), want, `len=${len}`);
	}
});

// The wasm and pure streaming cores must agree with each other, not
// just each with its own one-shot. This is the streaming counterpart
// of the two-engine check the KAT suite already does.
test("wasm and pure streaming agree", () => {
	for (const n of [0, 1, 63, 64, 127, 128, 447, 448, 449, 1000, 5000, 70000]) {
		const data = patternA(n);
		for (const chunk of [1, 64, 448, 449, 4096]) {
			const w = new Hasher(11n);
			const p = new PureHasher(11n);
			feed(w, data, chunk);
			feed(p, data, chunk);
			assert.equal(w.digest64(), p.digest64(), `len=${n} chunk=${chunk}`);
			assert.equal(w.digest128().hi, p.digest128().hi, `len=${n} chunk=${chunk}`);
		}
	}
});

test("several hashers stay independent", () => {
	// The wasm engine round-trips one shared scratch region, so
	// interleaving instances would corrupt them if state were resident.
	const a = new Hasher(1n);
	const b = new Hasher(2n);
	const da = patternA(900);
	const db = patternA(1500);
	a.update(da.subarray(0, 400));
	b.update(db.subarray(0, 700));
	a.update(da.subarray(400));
	b.update(db.subarray(700));
	assert.equal(a.digest64(), hayahash64(da, 1n));
	assert.equal(b.digest64(), hayahash64(db, 2n));
});

test("one-shot still works after streaming (shared scratch)", () => {
	const h = new Hasher(5n);
	const data = patternA(2000);
	h.update(data.subarray(0, 1000));
	assert.equal(hayahash64(data, 5n), hayahash64(data, 5n));
	h.update(data.subarray(1000));
	assert.equal(h.digest64(), hayahash64(data, 5n));
	assert.equal(getEngine() === "wasm" || getEngine() === "js", true);
});
