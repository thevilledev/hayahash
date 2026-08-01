// Nightly differential conformance against a randomized C-reference corpus.
import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { test } from "node:test";

import { hayahash64, hayahash64Pure } from "../dist/index.js";

const corpusPath = process.env.HAYAHASH_CORPUS;

test(
	"randomized C-reference corpus (wasm and pure JS)",
	{ skip: corpusPath === undefined ? "HAYAHASH_CORPUS is unset" : false },
	async () => {
		const file = await readFile(corpusPath);
		const corpus = new Uint8Array(file.buffer, file.byteOffset, file.byteLength);
		const view = new DataView(corpus.buffer, corpus.byteOffset, corpus.byteLength);
		let offset = 0;
		const take = (length) => {
			const end = offset + length;
			assert.ok(
				Number.isSafeInteger(end) && end <= corpus.length,
				`truncated differential corpus at byte ${offset}`,
			);
			const value = corpus.subarray(offset, end);
			offset = end;
			return value;
		};
		const readU32 = () => {
			const start = offset;
			take(4);
			return view.getUint32(start, true);
		};
		const readU64 = () => {
			const start = offset;
			take(8);
			return view.getBigUint64(start, true);
		};

		assert.equal(new TextDecoder().decode(take(8)), "HAYAFZ01");
		const caseCount = readU32();
		const prngSeed = readU64();
		const casesOffset = offset;
		const engines = [
			["wasm", hayahash64],
			["pure JS", hayahash64Pure],
		];

		for (const [engine, hash] of engines) {
			offset = casesOffset;
			for (let caseIndex = 0; caseIndex < caseCount; caseIndex++) {
				const length = readU32();
				const hashSeed = readU64();
				const expected = readU64();
				const input = take(length);
				assert.equal(
					hash(input, hashSeed),
					expected,
					`${engine}: case=${caseIndex} len=${length} ` +
						`hash_seed=0x${hashSeed.toString(16).padStart(16, "0")} ` +
						`corpus_prng_seed=0x${prngSeed.toString(16).padStart(16, "0")}`,
				);
			}
			assert.equal(offset, corpus.length, "trailing bytes in differential corpus");
		}

		console.error(
			`JavaScript wasm and pure engines matched ${caseCount} C-reference cases ` +
				`(corpus PRNG seed=0x${prngSeed.toString(16).padStart(16, "0")})`,
		);
	},
);
