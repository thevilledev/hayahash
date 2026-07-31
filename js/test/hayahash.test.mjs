// Conformance tests against the C reference implementation, for both
// the wasm engine (the reference header compiled to wasm32) and the
// pure-JS BigInt engine.
//
// The vector table is shared with the other ports (see go/kat_test.go,
// rust/tests/kat.rs, zig/tests/kat.zig and the Java KatTest); it was
// generated from hayahash.h. Regenerate with the same key-byte formula
// if the algorithm ever changes on purpose.
import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { test } from "node:test";

import { getEngine, hayahash64, hayahash64Pure, setWasmModule } from "../dist/index.js";

const K = 0x9e3779b97f4a7c15n;

// byteAt returns the deterministic key material shared with the C
// generator: byte(i) = ((i*K + 0x2545F4914F6CDD1D) >> 56) & 0xFF.
function byteAt(i) {
	return Number(((BigInt(i) * K + 0x2545f4914f6cdd1dn) >> 56n) & 0xffn);
}

// (input length, seed, expected digest) triples generated from the C
// reference. Lengths cover every dispatch path: empty, 1..3 byte,
// 4..7 byte, 8..16 byte, tail-only, mid-loop, and bulk-loop (>= 320)
// inputs, including all boundary values.
const vectors = [
	[0, 0x0000000000000000n, 0xc4f85f43d5a9985en],
	[0, 0x9e3779b97f4a7c15n, 0x68ac507cf298ca3fn],
	[0, 0xdeadbeefcafebaben, 0x4b9e2d31e2f3be1an],
	[1, 0x0000000000000000n, 0x01a6bdfc8c3d62dbn],
	[1, 0x9e3779b97f4a7c15n, 0x9d50f14e915849cen],
	[1, 0xdeadbeefcafebaben, 0xcccc62ac48d47c24n],
	[2, 0x0000000000000000n, 0xdadf7da717bfe154n],
	[2, 0x9e3779b97f4a7c15n, 0xc89a133687d95091n],
	[2, 0xdeadbeefcafebaben, 0x30f0c4a46f6ad806n],
	[3, 0x0000000000000000n, 0xea21f17856742557n],
	[3, 0x9e3779b97f4a7c15n, 0xe58b6749c35abe5cn],
	[3, 0xdeadbeefcafebaben, 0x6d72e08b53427ee0n],
	[4, 0x0000000000000000n, 0x7c58cf3c18f9b496n],
	[4, 0x9e3779b97f4a7c15n, 0x99aaf1833279aa9dn],
	[4, 0xdeadbeefcafebaben, 0xa1b22f8158cfd63fn],
	[5, 0x0000000000000000n, 0x517724b9853c566cn],
	[5, 0x9e3779b97f4a7c15n, 0x250acb4274262e38n],
	[5, 0xdeadbeefcafebaben, 0x31760ee78a0023efn],
	[6, 0x0000000000000000n, 0xe1d10a4fbbe1dbc7n],
	[6, 0x9e3779b97f4a7c15n, 0xc93f66bf5463fff0n],
	[6, 0xdeadbeefcafebaben, 0x8c20fd5f52dce3cfn],
	[7, 0x0000000000000000n, 0x56a3989bddd7cbe5n],
	[7, 0x9e3779b97f4a7c15n, 0xd7d254ee5d533404n],
	[7, 0xdeadbeefcafebaben, 0xc1d3ab0cc35936b9n],
	[8, 0x0000000000000000n, 0xdfd74858772b0e91n],
	[8, 0x9e3779b97f4a7c15n, 0x9bb51f7d28c94379n],
	[8, 0xdeadbeefcafebaben, 0x600552a3ff515faen],
	[9, 0x0000000000000000n, 0x0d246b6b3127404en],
	[9, 0x9e3779b97f4a7c15n, 0x96a02b773af52564n],
	[9, 0xdeadbeefcafebaben, 0x691bf2c6a9eef2e0n],
	[10, 0x0000000000000000n, 0x940ced4143cccb2bn],
	[10, 0x9e3779b97f4a7c15n, 0x6d653db391fbcaccn],
	[10, 0xdeadbeefcafebaben, 0x24169bd3021086acn],
	[11, 0x0000000000000000n, 0x00f4c1eb06392f40n],
	[11, 0x9e3779b97f4a7c15n, 0xa239c14889ba224an],
	[11, 0xdeadbeefcafebaben, 0x29d070b34af489cen],
	[12, 0x0000000000000000n, 0x35985ee7c1dfc292n],
	[12, 0x9e3779b97f4a7c15n, 0xc65a041e835e8250n],
	[12, 0xdeadbeefcafebaben, 0x310dff6efce0f6bdn],
	[13, 0x0000000000000000n, 0xfea35c4b388ec02bn],
	[13, 0x9e3779b97f4a7c15n, 0xaa3cefed2e20869en],
	[13, 0xdeadbeefcafebaben, 0xf478bc0f8259144cn],
	[14, 0x0000000000000000n, 0x08524e7e5aa2ce93n],
	[14, 0x9e3779b97f4a7c15n, 0x5d380960c8653731n],
	[14, 0xdeadbeefcafebaben, 0x8887680638a97434n],
	[15, 0x0000000000000000n, 0xe82f8f2f83e24dc3n],
	[15, 0x9e3779b97f4a7c15n, 0xa55cc72365471962n],
	[15, 0xdeadbeefcafebaben, 0xaf5a8128d9513d53n],
	[16, 0x0000000000000000n, 0xc6c3b656c926ef2bn],
	[16, 0x9e3779b97f4a7c15n, 0x348b4b44e949a4a5n],
	[16, 0xdeadbeefcafebaben, 0x8b444516c3fce921n],
	[17, 0x0000000000000000n, 0xa90b41f0b4d835fbn],
	[17, 0x9e3779b97f4a7c15n, 0xeb9cf0a2219a8a79n],
	[17, 0xdeadbeefcafebaben, 0x2b0bcb27d533f29dn],
	[20, 0x0000000000000000n, 0xd71bdc0adca8adf8n],
	[20, 0x9e3779b97f4a7c15n, 0x1ded18e5d2c6641dn],
	[20, 0xdeadbeefcafebaben, 0x50e620aa5892fc5en],
	[24, 0x0000000000000000n, 0x6982f8bdfe69f930n],
	[24, 0x9e3779b97f4a7c15n, 0xfb5432ef4d039215n],
	[24, 0xdeadbeefcafebaben, 0xb7a4906d498a9857n],
	[31, 0x0000000000000000n, 0xb01cada4e3595061n],
	[31, 0x9e3779b97f4a7c15n, 0xbb07ce100ef1ace7n],
	[31, 0xdeadbeefcafebaben, 0xd0b87a0842a08418n],
	[32, 0x0000000000000000n, 0x242fd3f914303c1dn],
	[32, 0x9e3779b97f4a7c15n, 0x343ba63d7e4fc2f6n],
	[32, 0xdeadbeefcafebaben, 0x66d7ef7da4627549n],
	[33, 0x0000000000000000n, 0xe87de7c9f18e3d9dn],
	[33, 0x9e3779b97f4a7c15n, 0x548c908a58a6cdf1n],
	[33, 0xdeadbeefcafebaben, 0xc115ea8fba551490n],
	[47, 0x0000000000000000n, 0x696428dd947df322n],
	[47, 0x9e3779b97f4a7c15n, 0x39be535c74585ac5n],
	[47, 0xdeadbeefcafebaben, 0x52811a1d01562807n],
	[48, 0x0000000000000000n, 0x1059b4e24c8cfdf0n],
	[48, 0x9e3779b97f4a7c15n, 0x643ba76560d01c09n],
	[48, 0xdeadbeefcafebaben, 0xa537f4069657c426n],
	[63, 0x0000000000000000n, 0x4471e159ca7f1aa1n],
	[63, 0x9e3779b97f4a7c15n, 0xe979866c4f557390n],
	[63, 0xdeadbeefcafebaben, 0xb026d08ec12753f1n],
	[64, 0x0000000000000000n, 0x62bc7d33c15657e9n],
	[64, 0x9e3779b97f4a7c15n, 0x0078437e7d379478n],
	[64, 0xdeadbeefcafebaben, 0x765229403e95673bn],
	[65, 0x0000000000000000n, 0xa57f09711ae70c77n],
	[65, 0x9e3779b97f4a7c15n, 0xa5f941f9895faf8dn],
	[65, 0xdeadbeefcafebaben, 0x74f19f9600a6c10fn],
	[96, 0x0000000000000000n, 0x2186fdc93e032c9cn],
	[96, 0x9e3779b97f4a7c15n, 0x4164e090ea0c5df9n],
	[96, 0xdeadbeefcafebaben, 0x967410100e8c7c8an],
	[127, 0x0000000000000000n, 0xdaa46cc2e67cf5c3n],
	[127, 0x9e3779b97f4a7c15n, 0x1d78b9bce14cf66fn],
	[127, 0xdeadbeefcafebaben, 0x7cc9599acc50f32bn],
	[128, 0x0000000000000000n, 0xd4c430490d0ce9d1n],
	[128, 0x9e3779b97f4a7c15n, 0xae0594746a45d322n],
	[128, 0xdeadbeefcafebaben, 0xc777bd88bf800192n],
	[191, 0x0000000000000000n, 0xab4eb4f6a214ab26n],
	[191, 0x9e3779b97f4a7c15n, 0x2f01eefe05f61681n],
	[191, 0xdeadbeefcafebaben, 0x0ac11d2d060a6d39n],
	[192, 0x0000000000000000n, 0xbd4668fd0e37a0d8n],
	[192, 0x9e3779b97f4a7c15n, 0xb33bd21c20d7f1een],
	[192, 0xdeadbeefcafebaben, 0x2c05041a563efc0an],
	[255, 0x0000000000000000n, 0x3f35c5137d9ddd92n],
	[255, 0x9e3779b97f4a7c15n, 0xb3450beea6a88c0en],
	[255, 0xdeadbeefcafebaben, 0x10eeafb383803642n],
	[319, 0x0000000000000000n, 0xfb6f356631c62298n],
	[319, 0x9e3779b97f4a7c15n, 0x82a92d2f3c0d3fc2n],
	[319, 0xdeadbeefcafebaben, 0xc187b939c37f8ec7n],
	[320, 0x0000000000000000n, 0xc81c58ff60a61519n],
	[320, 0x9e3779b97f4a7c15n, 0x9b044c0a4af56a86n],
	[320, 0xdeadbeefcafebaben, 0xd26cbf8b66025b75n],
	[321, 0x0000000000000000n, 0x3a305a1273ebf04fn],
	[321, 0x9e3779b97f4a7c15n, 0x9f45dacf398275fcn],
	[321, 0xdeadbeefcafebaben, 0xc3de88b3556ce065n],
	[383, 0x0000000000000000n, 0xdd8facb7f74b1409n],
	[383, 0x9e3779b97f4a7c15n, 0x5c882c0517facc54n],
	[383, 0xdeadbeefcafebaben, 0x1de5b1c702d219can],
	[512, 0x0000000000000000n, 0xd15aab5b1bfc55c5n],
	[512, 0x9e3779b97f4a7c15n, 0xc2e37fd41a5ed608n],
	[512, 0xdeadbeefcafebaben, 0x88de08068a26f736n],
	[1023, 0x0000000000000000n, 0x1a8a16006994c3d6n],
	[1023, 0x9e3779b97f4a7c15n, 0xae6247018ec68c31n],
	[1023, 0xdeadbeefcafebaben, 0x0f4d23af7d088a7fn],
	[1024, 0x0000000000000000n, 0xab13d66aa1023222n],
	[1024, 0x9e3779b97f4a7c15n, 0x874a870e1aa48896n],
	[1024, 0xdeadbeefcafebaben, 0x5d9052ee0cf697ean],
];

const key = new Uint8Array(1024);
for (let i = 0; i < key.length; i++) {
	key[i] = byteAt(i);
}

const engines = [
	["wasm-backed hayahash64", hayahash64],
	["pure-JS hayahash64Pure", hayahash64Pure],
];

test("wasm engine is active in this environment", () => {
	assert.equal(getEngine(), "wasm");
});

for (const [name, hash] of engines) {
	test(`known-answer vectors (${name})`, () => {
		for (const [len, seed, want] of vectors) {
			assert.equal(
				hash(key.subarray(0, len), seed),
				want,
				`len=${len} seed=${seed.toString(16)}`,
			);
		}
	});

	// Reproduces SMHasher3's self-test: hash the key prefix of length
	// i with seed 256-i for i in 0..=255, concatenating the
	// little-endian digests (key byte i is set to i after round i),
	// then hash that buffer with seed 0. The low 32 bits must match
	// the registered verification value.
	test(`SMHasher3 verification value (${name})`, () => {
		const vkey = new Uint8Array(256);
		const hashes = new Uint8Array(256 * 8);
		const view = new DataView(hashes.buffer);
		for (let i = 0; i < 256; i++) {
			view.setBigUint64(i * 8, hash(vkey.subarray(0, i), BigInt(256 - i)), true);
			vkey[i] = i;
		}
		assert.equal(hash(hashes, 0n) & 0xffffffffn, 0x97cd6266n);
	});
}

test("string input is hashed as UTF-8", () => {
	assert.equal(hayahash64("hello world"), 0xf2172c5bd68ec576n);
	assert.equal(
		hayahash64("häyähäsh \u{1F680}"),
		hayahash64(new TextEncoder().encode("häyähäsh \u{1F680}")),
	);
});

test("seed handling", () => {
	assert.equal(hayahash64("abc"), hayahash64("abc", 0n));
	assert.equal(hayahash64("abc", 42), hayahash64("abc", 42n));
	// Negative seeds map to their two's-complement uint64_t pattern.
	assert.equal(hayahash64("abc", -1), hayahash64("abc", 0xffffffffffffffffn));
	assert.throws(() => hayahash64("abc", 1.5), RangeError);
});

test("subarrays with a byte offset hash like copies", () => {
	const sub = key.subarray(7, 7 + 37);
	assert.equal(hayahash64(sub), hayahash64(key.slice(7, 7 + 37)));
	assert.equal(hayahash64Pure(sub), hayahash64Pure(key.slice(7, 7 + 37)));
});

// The edge-runtime path: activating the engine from a precompiled
// WebAssembly.Module (as a Cloudflare Workers user would after
// importing the .wasm shipped in the package).
test("setWasmModule accepts a precompiled module", async () => {
	const bytes = await readFile(new URL("../wasm/hayahash.wasm", import.meta.url));
	setWasmModule(new WebAssembly.Module(bytes));
	assert.equal(getEngine(), "wasm");
	assert.equal(hayahash64("hello world"), 0xf2172c5bd68ec576n);
});

test("setWasmModule rejects foreign modules, keeping the engine", () => {
	// Smallest valid wasm module: magic + version, no exports.
	const empty = new Uint8Array([0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00]);
	assert.throws(() => setWasmModule(new WebAssembly.Module(empty)), TypeError);
	assert.equal(getEngine(), "wasm");
	assert.equal(hayahash64("hello world"), 0xf2172c5bd68ec576n);
});

// Cross-check the two engines on inputs big enough to force the wasm
// memory to grow past its initial single page (heap base 32 KiB).
test("engines agree on large inputs", () => {
	const big = new Uint8Array(300_000);
	let x = 0x9e3779b9;
	for (let i = 0; i < big.length; i++) {
		x ^= (x << 13) | 0;
		x ^= x >>> 17;
		x ^= (x << 5) | 0;
		big[i] = x & 0xff;
	}
	for (const len of [31_991, 32_768, 65_537, 300_000]) {
		const data = big.subarray(0, len);
		assert.equal(hayahash64(data, 0x1234n), hayahash64Pure(data, 0x1234n), `len=${len}`);
	}
});
