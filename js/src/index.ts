// hayahash64 - small, fast, portable 64-bit hash function.
//
// npm port of the reference C implementation (hayahash.h at the
// repository root); see that header for the full design notes. The
// fast path runs the reference header itself, compiled to a ~1.4 KB
// WebAssembly module; a pure-JS BigInt port takes over where wasm is
// unavailable. Output is identical for every input and seed on every
// platform and engine.
//
// This is free and unencumbered software released into the public
// domain. For more information, please refer to <https://unlicense.org/>

import { hashPure } from "./pure.js";
import { initWasm, initWasmFromModule } from "./wasm.js";

const MASK64 = 0xffffffffffffffffn;

// ptrdiff_t is 32-bit inside wasm32; longer inputs (unlikely as that
// is) are routed to the pure engine, which has no such limit.
const WASM_MAX_LEN = 0x7fffffff;

let wasm = initWasm();
const encoder = new TextEncoder();

/** Input accepted by the hash functions: raw bytes, or a string
 * (hashed as its UTF-8 encoding). */
export type HayahashInput = Uint8Array | string;

/** Which engine `hayahash64` currently uses: `"wasm"` (the reference
 * C header compiled to WebAssembly) or `"js"` (the pure BigInt
 * fallback). */
export function getEngine(): "wasm" | "js" {
	return wasm === null ? "js" : "wasm";
}

/**
 * Activates the wasm engine from an already-compiled
 * `WebAssembly.Module` of `hayahash.wasm` (shipped in this package).
 *
 * This exists for runtimes that forbid compiling wasm at runtime and
 * would otherwise silently use the slow pure-JS fallback. Cloudflare
 * Workers precompiles `.wasm` imports at deploy time, so there:
 *
 * ```js
 * import wasmModule from "hayahash/hayahash.wasm";
 * import { setWasmModule } from "hayahash";
 *
 * setWasmModule(wasmModule);
 * ```
 *
 * Throws (leaving the previous engine active) if the module is not
 * the hayahash wasm module.
 */
export function setWasmModule(module: WebAssembly.Module): void {
	wasm = initWasmFromModule(module);
}

function normalize(input: HayahashInput, seed: bigint | number): [Uint8Array, bigint] {
	const data = typeof input === "string" ? encoder.encode(input) : input;
	// BigInt() rejects non-integer numbers; the mask maps negative
	// seeds to their two's-complement bit pattern, matching a C
	// caller passing the same value as uint64_t.
	return [data, BigInt(seed) & MASK64];
}

/**
 * Hashes `input` with a 64-bit `seed` (default 0) and returns the
 * 64-bit digest as an unsigned bigint in `[0, 2^64)`.
 *
 * ```js
 * hayahash64("hello world").toString(16); // "f2172c5bd68ec576"
 * ```
 */
export function hayahash64(input: HayahashInput, seed: bigint | number = 0n): bigint {
	const [data, s] = normalize(input, seed);
	if (wasm !== null && data.length <= WASM_MAX_LEN) {
		return wasm.hash(data, s);
	}
	return hashPure(data, s);
}

/**
 * Same digest as `hayahash64`, always computed by the pure-JS BigInt
 * engine. Slow; exists for environments without WebAssembly and as
 * an independent cross-check (the test suite verifies both engines
 * against the shared known-answer vectors).
 */
export function hayahash64Pure(input: HayahashInput, seed: bigint | number = 0n): bigint {
	const [data, s] = normalize(input, seed);
	return hashPure(data, s);
}
