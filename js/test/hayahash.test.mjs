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
	[17, 0x0000000000000000n, 0xf2097d97dde357aen],
	[17, 0x9e3779b97f4a7c15n, 0x9632041cd63db454n],
	[17, 0xdeadbeefcafebaben, 0x1723899582c3ea6fn],
	[20, 0x0000000000000000n, 0x755baf1f2b76c412n],
	[20, 0x9e3779b97f4a7c15n, 0x3618dccef36b9b12n],
	[20, 0xdeadbeefcafebaben, 0xd1dd986517b516b5n],
	[24, 0x0000000000000000n, 0xa9eef61768886934n],
	[24, 0x9e3779b97f4a7c15n, 0x3e37447d706f68dcn],
	[24, 0xdeadbeefcafebaben, 0x7ff1ab972fe165c1n],
	[31, 0x0000000000000000n, 0xa4ebc213a72566e4n],
	[31, 0x9e3779b97f4a7c15n, 0x8c97e8fb408409ebn],
	[31, 0xdeadbeefcafebaben, 0x00ff9db9173a7670n],
	[32, 0x0000000000000000n, 0x9733f484b26360e2n],
	[32, 0x9e3779b97f4a7c15n, 0x5536b4d8b2cdc730n],
	[32, 0xdeadbeefcafebaben, 0x8715ce33bca2379an],
	[33, 0x0000000000000000n, 0xa745a3e83613c58cn],
	[33, 0x9e3779b97f4a7c15n, 0x553ff7d4fc5d9dedn],
	[33, 0xdeadbeefcafebaben, 0x4a2c6ef5dc5531c2n],
	[47, 0x0000000000000000n, 0x273936d61142dc44n],
	[47, 0x9e3779b97f4a7c15n, 0xe5310d240eef34a6n],
	[47, 0xdeadbeefcafebaben, 0x6f1e75d18841e6a1n],
	[48, 0x0000000000000000n, 0x14fe95d8c0bbff90n],
	[48, 0x9e3779b97f4a7c15n, 0x12363278fa0c626cn],
	[48, 0xdeadbeefcafebaben, 0x7cedb4152693bad0n],
	[63, 0x0000000000000000n, 0xa2d251874d348d01n],
	[63, 0x9e3779b97f4a7c15n, 0x16a48c318e8b07e0n],
	[63, 0xdeadbeefcafebaben, 0xb1e770fd7bf42e61n],
	[64, 0x0000000000000000n, 0x363a32091a09fcbbn],
	[64, 0x9e3779b97f4a7c15n, 0xc4e3781c47efc9d1n],
	[64, 0xdeadbeefcafebaben, 0x8d03a93f23de3191n],
	[65, 0x0000000000000000n, 0x45f1ee618787ff12n],
	[65, 0x9e3779b97f4a7c15n, 0x04985102207489f3n],
	[65, 0xdeadbeefcafebaben, 0xf363e94bf76c2046n],
	[96, 0x0000000000000000n, 0x4e69ed01f30c31c8n],
	[96, 0x9e3779b97f4a7c15n, 0xc30a61fb3b222764n],
	[96, 0xdeadbeefcafebaben, 0x58af9e68db1ee021n],
	[127, 0x0000000000000000n, 0x0f0fe54bc082e22an],
	[127, 0x9e3779b97f4a7c15n, 0xe3e8106c41ea1694n],
	[127, 0xdeadbeefcafebaben, 0xce05be2901f0d305n],
	[128, 0x0000000000000000n, 0x2a2679b53f1a4841n],
	[128, 0x9e3779b97f4a7c15n, 0x300390cfa9b638b4n],
	[128, 0xdeadbeefcafebaben, 0x53f276cabae36eecn],
	[191, 0x0000000000000000n, 0xa92ba7e80f7062f9n],
	[191, 0x9e3779b97f4a7c15n, 0xeb0b5c65254f5714n],
	[191, 0xdeadbeefcafebaben, 0x139a18908f2ca7ean],
	[192, 0x0000000000000000n, 0xb7e79cdb9b877b00n],
	[192, 0x9e3779b97f4a7c15n, 0xe792ede59093081en],
	[192, 0xdeadbeefcafebaben, 0x1803df1a8dec4887n],
	[255, 0x0000000000000000n, 0xebc48b87e0cbc931n],
	[255, 0x9e3779b97f4a7c15n, 0xf93bda5aeeebf508n],
	[255, 0xdeadbeefcafebaben, 0xb1c14189418ffea4n],
	[319, 0x0000000000000000n, 0xc09297a1f1d64937n],
	[319, 0x9e3779b97f4a7c15n, 0xa41da5a41f8dcf12n],
	[319, 0xdeadbeefcafebaben, 0xec77e71be598ce5cn],
	[320, 0x0000000000000000n, 0xee3764436c14457an],
	[320, 0x9e3779b97f4a7c15n, 0xaa5826fa7260fedan],
	[320, 0xdeadbeefcafebaben, 0x949a1075c6520355n],
	[321, 0x0000000000000000n, 0xe29605c4e053b57en],
	[321, 0x9e3779b97f4a7c15n, 0x0220a8e06a7488fdn],
	[321, 0xdeadbeefcafebaben, 0x671ed25d1c2e46c6n],
	[383, 0x0000000000000000n, 0xe35440f8e70e9da9n],
	[383, 0x9e3779b97f4a7c15n, 0xd7efc77ef2d2bb7bn],
	[383, 0xdeadbeefcafebaben, 0x06bc8e5a39e7105an],
	[512, 0x0000000000000000n, 0xe3e6f3e6aeb93bdbn],
	[512, 0x9e3779b97f4a7c15n, 0x33aeb18ca7d3e3b0n],
	[512, 0xdeadbeefcafebaben, 0xe5f56f3152dd7519n],
	[1023, 0x0000000000000000n, 0x9fa8b5ef83709862n],
	[1023, 0x9e3779b97f4a7c15n, 0x8b29a20dd57d53ban],
	[1023, 0xdeadbeefcafebaben, 0x5cff532d7767ac0en],
	[1024, 0x0000000000000000n, 0xeff14c6adc7654f1n],
	[1024, 0x9e3779b97f4a7c15n, 0x8d6553c8dca3f2c0n],
	[1024, 0xdeadbeefcafebaben, 0x1fa36eeb572c3496n],
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
		assert.equal(hash(hashes, 0n) & 0xffffffffn, 0x99b3876fn);
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
