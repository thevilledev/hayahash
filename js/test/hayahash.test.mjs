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
	[0, 0x0000000000000000n, 0x68ac507cf298ca3fn],
	[0, 0x9e3779b97f4a7c15n, 0xc4f85f43d5a9985en],
	[0, 0xdeadbeefcafebaben, 0x7edc9f1b603b7337n],
	[1, 0x0000000000000000n, 0x7ec9660a48395d15n],
	[1, 0x9e3779b97f4a7c15n, 0x4d49aadd61bed986n],
	[1, 0xdeadbeefcafebaben, 0x8e456fa77805e810n],
	[2, 0x0000000000000000n, 0x3ae1e83a68b10976n],
	[2, 0x9e3779b97f4a7c15n, 0xe27acd9cd85250aen],
	[2, 0xdeadbeefcafebaben, 0xb64d67091596299dn],
	[3, 0x0000000000000000n, 0x10e8b8fea2d42e52n],
	[3, 0x9e3779b97f4a7c15n, 0x377e32d405528932n],
	[3, 0xdeadbeefcafebaben, 0xdcc0cb335dc1de4bn],
	[4, 0x0000000000000000n, 0x3ff33333aeea0226n],
	[4, 0x9e3779b97f4a7c15n, 0x7bb1267af5779b6bn],
	[4, 0xdeadbeefcafebaben, 0x321409d41f3f0597n],
	[5, 0x0000000000000000n, 0x37ee1f8b5a98b84bn],
	[5, 0x9e3779b97f4a7c15n, 0xbef801ddd997c630n],
	[5, 0xdeadbeefcafebaben, 0x169255793c689422n],
	[6, 0x0000000000000000n, 0x7c024e9bc939e745n],
	[6, 0x9e3779b97f4a7c15n, 0xdbe499df16af6c98n],
	[6, 0xdeadbeefcafebaben, 0x38a42a135d5bfbc6n],
	[7, 0x0000000000000000n, 0x8d33eeb37aea4269n],
	[7, 0x9e3779b97f4a7c15n, 0x5e24209a2fd00b2cn],
	[7, 0xdeadbeefcafebaben, 0xf47ab25f56bdc3d7n],
	[8, 0x0000000000000000n, 0xa7e6d3110da23914n],
	[8, 0x9e3779b97f4a7c15n, 0xccb63e92ce9f688bn],
	[8, 0xdeadbeefcafebaben, 0x18665c87c237153dn],
	[9, 0x0000000000000000n, 0x09c8dfca0c41da5cn],
	[9, 0x9e3779b97f4a7c15n, 0xe75ffd2d1e883756n],
	[9, 0xdeadbeefcafebaben, 0xe5ee2bb71f19de1bn],
	[10, 0x0000000000000000n, 0xe28b66fb1e4cb4ean],
	[10, 0x9e3779b97f4a7c15n, 0x2a004fea465884cen],
	[10, 0xdeadbeefcafebaben, 0x0418428d16ca9a24n],
	[11, 0x0000000000000000n, 0x12778e6f25c1d32an],
	[11, 0x9e3779b97f4a7c15n, 0x48188c4bed9a1e46n],
	[11, 0xdeadbeefcafebaben, 0xad02cb6ae9d55e5bn],
	[12, 0x0000000000000000n, 0x48d755ebe2679385n],
	[12, 0x9e3779b97f4a7c15n, 0x011e9fd88e5940fen],
	[12, 0xdeadbeefcafebaben, 0x8180264c7b1768a9n],
	[13, 0x0000000000000000n, 0x381a61980d756222n],
	[13, 0x9e3779b97f4a7c15n, 0x64b7fc904bbc58b3n],
	[13, 0xdeadbeefcafebaben, 0x64c9fe2b9160c2can],
	[14, 0x0000000000000000n, 0xc71a2db50e6448ebn],
	[14, 0x9e3779b97f4a7c15n, 0x32011605ed340d8cn],
	[14, 0xdeadbeefcafebaben, 0x3a8f543c0f65c501n],
	[15, 0x0000000000000000n, 0x9a8920a57f119d6bn],
	[15, 0x9e3779b97f4a7c15n, 0x5572e81bab3953ffn],
	[15, 0xdeadbeefcafebaben, 0x7e1eb5f7f4a597f0n],
	[16, 0x0000000000000000n, 0xe1af813939ba1a9en],
	[16, 0x9e3779b97f4a7c15n, 0x72ef22a0197ac7e6n],
	[16, 0xdeadbeefcafebaben, 0x15ddb774f1becbf7n],
	[17, 0x0000000000000000n, 0xeb0531e9e3a3beben],
	[17, 0x9e3779b97f4a7c15n, 0x0f6dfa98935233f7n],
	[17, 0xdeadbeefcafebaben, 0x3f070cc2b4422ba0n],
	[20, 0x0000000000000000n, 0xc311e14ff31fb2bfn],
	[20, 0x9e3779b97f4a7c15n, 0xac8dc0fd5673d897n],
	[20, 0xdeadbeefcafebaben, 0x8b748e4515d7c27fn],
	[24, 0x0000000000000000n, 0x9a64d93e28cb5da0n],
	[24, 0x9e3779b97f4a7c15n, 0xdd2d9a95b8088061n],
	[24, 0xdeadbeefcafebaben, 0xbe8112b0f103e6c5n],
	[31, 0x0000000000000000n, 0x95d2421945aec7a1n],
	[31, 0x9e3779b97f4a7c15n, 0xcd63f6f92ae5ba34n],
	[31, 0xdeadbeefcafebaben, 0x1e915f729da2021an],
	[32, 0x0000000000000000n, 0xcbd35dab7ad91ce4n],
	[32, 0x9e3779b97f4a7c15n, 0x4e5482c9bc55ac72n],
	[32, 0xdeadbeefcafebaben, 0xefbff5d3a7172762n],
	[33, 0x0000000000000000n, 0x134d1f8689bf729cn],
	[33, 0x9e3779b97f4a7c15n, 0x02f60a6383c9bea7n],
	[33, 0xdeadbeefcafebaben, 0x51de032c8da94d2fn],
	[47, 0x0000000000000000n, 0x854a0e1fb80dc713n],
	[47, 0x9e3779b97f4a7c15n, 0x9c87c14bfad5f65dn],
	[47, 0xdeadbeefcafebaben, 0x8da40d3a16f8fbf1n],
	[48, 0x0000000000000000n, 0x6b6b8caa3ddb2a68n],
	[48, 0x9e3779b97f4a7c15n, 0xfefae7add93696a6n],
	[48, 0xdeadbeefcafebaben, 0xf3decf00052380b1n],
	[63, 0x0000000000000000n, 0x7fd21b276d3862d5n],
	[63, 0x9e3779b97f4a7c15n, 0xf8571e24784c85b0n],
	[63, 0xdeadbeefcafebaben, 0x68b11facbca125f5n],
	[64, 0x0000000000000000n, 0x8d2ce2017d1eccebn],
	[64, 0x9e3779b97f4a7c15n, 0x257d3ee25843f04bn],
	[64, 0xdeadbeefcafebaben, 0x71aa83b0d836f52dn],
	[65, 0x0000000000000000n, 0xa521c43309772cden],
	[65, 0x9e3779b97f4a7c15n, 0xfcd59327e5c4f6ddn],
	[65, 0xdeadbeefcafebaben, 0x2d7d45f44c1829d0n],
	[96, 0x0000000000000000n, 0x0e456a468ac7355bn],
	[96, 0x9e3779b97f4a7c15n, 0xe5f760fc0c083b17n],
	[96, 0xdeadbeefcafebaben, 0xd3b493d06042dc09n],
	[127, 0x0000000000000000n, 0x4907f10a034954d1n],
	[127, 0x9e3779b97f4a7c15n, 0x1d907a46a134ab8en],
	[127, 0xdeadbeefcafebaben, 0x350db49c244548a7n],
	[128, 0x0000000000000000n, 0xeeceee2b8790729dn],
	[128, 0x9e3779b97f4a7c15n, 0x8ded815ca5788588n],
	[128, 0xdeadbeefcafebaben, 0xc9ff25bfde22a5c7n],
	[191, 0x0000000000000000n, 0xb9e354abaf76cda3n],
	[191, 0x9e3779b97f4a7c15n, 0x8ab844bbf8df6893n],
	[191, 0xdeadbeefcafebaben, 0x0a1934ae61772e91n],
	[192, 0x0000000000000000n, 0x0503fd18db80ffffn],
	[192, 0x9e3779b97f4a7c15n, 0xedcacf5231ffedf9n],
	[192, 0xdeadbeefcafebaben, 0xe6357993d5cafbd4n],
	[255, 0x0000000000000000n, 0x1d0ee105fc8ee266n],
	[255, 0x9e3779b97f4a7c15n, 0x9eadeaf2612e6b65n],
	[255, 0xdeadbeefcafebaben, 0x674b0232e3ba8afbn],
	[319, 0x0000000000000000n, 0x8f078f3394ac0eebn],
	[319, 0x9e3779b97f4a7c15n, 0x0672714b8b89eaf4n],
	[319, 0xdeadbeefcafebaben, 0x66f3ecd10e74b602n],
	[320, 0x0000000000000000n, 0xf4bcf4fa135aabfen],
	[320, 0x9e3779b97f4a7c15n, 0x6f86504f4c61f014n],
	[320, 0xdeadbeefcafebaben, 0xf0563f11be6d85c7n],
	[321, 0x0000000000000000n, 0x68868a120fb9cef6n],
	[321, 0x9e3779b97f4a7c15n, 0x58b97afd4ada0656n],
	[321, 0xdeadbeefcafebaben, 0xedf253cd5a32819cn],
	[383, 0x0000000000000000n, 0x762cf976c6ffba80n],
	[383, 0x9e3779b97f4a7c15n, 0x738e8b85886f0eafn],
	[383, 0xdeadbeefcafebaben, 0x368281e467a93a0en],
	[512, 0x0000000000000000n, 0xdfbf7fc9292ff7ffn],
	[512, 0x9e3779b97f4a7c15n, 0x153a2fc22acbaea6n],
	[512, 0xdeadbeefcafebaben, 0x2e9b7d29f46f8552n],
	[1023, 0x0000000000000000n, 0x2578244c81138967n],
	[1023, 0x9e3779b97f4a7c15n, 0xb2c3356b389d297dn],
	[1023, 0xdeadbeefcafebaben, 0x125eaa99fbae5c4dn],
	[1024, 0x0000000000000000n, 0x951be6cf3bc7cf43n],
	[1024, 0x9e3779b97f4a7c15n, 0x3460eec64aa4799fn],
	[1024, 0xdeadbeefcafebaben, 0xc93d1f81fd51336an],
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
		assert.equal(hash(hashes, 0n) & 0xffffffffn, 0x65f2ac15n);
	});
}

test("string input is hashed as UTF-8", () => {
	assert.equal(hayahash64("hello world"), 0x4524b96611bfc05an);
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
	assert.equal(hayahash64("hello world"), 0x4524b96611bfc05an);
});

test("setWasmModule rejects foreign modules, keeping the engine", () => {
	// Smallest valid wasm module: magic + version, no exports.
	const empty = new Uint8Array([0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00]);
	assert.throws(() => setWasmModule(new WebAssembly.Module(empty)), TypeError);
	assert.equal(getEngine(), "wasm");
	assert.equal(hayahash64("hello world"), 0x4524b96611bfc05an);
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
