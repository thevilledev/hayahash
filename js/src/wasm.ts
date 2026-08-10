// WebAssembly engine: instantiates the embedded wasm module (the
// reference hayahash.h compiled for wasm32-freestanding, see
// wasm/build.sh) and wraps it behind the same (data, seed) -> digest
// signature as the pure engine. Input bytes are copied into linear
// memory at __heap_base; memory is grown on demand and never shrunk.
//
// This is free and unencumbered software released into the public
// domain. For more information, please refer to <https://unlicense.org/>

import { wasmBase64 } from "./wasm-module.js";
import type { Hash128 } from "./pure.js";

const PAGE = 65536;

interface WasmApi {
	memory: WebAssembly.Memory;
	__heap_base: WebAssembly.Global;
	hayahash64: (ptr: number, len: number, seed: bigint) => bigint;
	hayahash128: (ptr: number, len: number, seed: bigint, out: number) => void;
	hayahash_state_size: () => number;
	hayahash_stream_init: (st: number, seed: bigint) => void;
	hayahash_stream_update: (st: number, p: number, n: number) => void;
	hayahash_stream_digest: (st: number, out: number) => void;
}

export interface Engine {
	hash(data: Uint8Array, seed: bigint): bigint;
	hash128(data: Uint8Array, seed: bigint): Hash128;
	/** Byte length of an opaque streaming state. */
	stateSize: number;
	/** Initializes a JS-owned state buffer of length `stateSize`. */
	streamInit(state: Uint8Array, seed: bigint): void;
	/** Absorbs `data` into `state`. */
	streamUpdate(state: Uint8Array, data: Uint8Array): void;
	/** Digests `state` without consuming it. */
	streamDigest(state: Uint8Array): Hash128;
}

function decodeBase64(b64: string): Uint8Array<ArrayBuffer> {
	const bin = atob(b64);
	const bytes = new Uint8Array(bin.length);
	for (let i = 0; i < bin.length; i++) {
		bytes[i] = bin.charCodeAt(i);
	}
	return bytes;
}

// Builds an engine from an already-compiled module. Instantiation
// (unlike compilation) is permitted even on runtimes that forbid
// runtime code generation, so this is the entry point for edge
// runtimes: import wasm/hayahash.wasm as a module (precompiled at
// deploy time) and pass it to setWasmModule(). Throws if the module
// is not the hayahash wasm module.
export function initWasmFromModule(module: WebAssembly.Module): Engine {
	const instance = new WebAssembly.Instance(module);
	const api = instance.exports as unknown as WasmApi;
	if (
		typeof api.hayahash64 !== "function" ||
		typeof api.hayahash128 !== "function" ||
		typeof api.hayahash_state_size !== "function" ||
		typeof api.hayahash_stream_init !== "function" ||
		typeof api.hayahash_stream_update !== "function" ||
		typeof api.hayahash_stream_digest !== "function" ||
		!(api.memory instanceof WebAssembly.Memory) ||
		typeof api.__heap_base?.value !== "number"
	) {
		throw new TypeError(
			"not a hayahash wasm module (expected exports: hayahash64, hayahash128, hayahash_stream_*, memory, __heap_base)",
		);
	}
	const memory = api.memory;
	const heapBase = api.__heap_base.value as number;
	const stateSize = api.hayahash_state_size();
	// Scratch layout above __heap_base: the state, then the 16-byte
	// digest out-parameter, then input bytes.
	const statePtr = heapBase;
	const outPtr = heapBase + stateSize;
	const streamInputPtr = outPtr + 16;
	let mem = new Uint8Array(memory.buffer);

	function reserve(needed: number): Uint8Array {
		if (memory.buffer.byteLength < needed) {
			memory.grow(Math.ceil((needed - memory.buffer.byteLength) / PAGE));
		}
		// grow() detaches the old buffer; refresh the view.
		if (mem.buffer !== memory.buffer) {
			mem = new Uint8Array(memory.buffer);
		}
		return mem;
	}

	return {
		stateSize,

		streamInit(state: Uint8Array, seed: bigint): void {
			const m = reserve(statePtr + stateSize);
			api.hayahash_stream_init(statePtr, seed);
			state.set(m.subarray(statePtr, statePtr + stateSize));
		},

		// The state round-trips through linear memory on every call
		// rather than living there: that keeps the JS object the sole
		// owner, so there is no wasm-side allocator, no dispose() in
		// the public API and no finalizer. The cost is two stateSize
		// copies per update, which the hashing work dominates once
		// chunks are past a few KiB.
		streamUpdate(state: Uint8Array, data: Uint8Array): void {
			const m = reserve(streamInputPtr + data.length);
			m.set(state, statePtr);
			m.set(data, streamInputPtr);
			api.hayahash_stream_update(statePtr, streamInputPtr, data.length);
			state.set(m.subarray(statePtr, statePtr + stateSize));
		},

		streamDigest(state: Uint8Array): Hash128 {
			const m = reserve(outPtr + 16);
			m.set(state, statePtr);
			api.hayahash_stream_digest(statePtr, outPtr);
			const view = new DataView(memory.buffer);
			return {
				lo: view.getBigUint64(outPtr, true),
				hi: view.getBigUint64(outPtr + 8, true),
			};
		},

		hash(data: Uint8Array, seed: bigint): bigint {
			const needed = heapBase + data.length;
			if (memory.buffer.byteLength < needed) {
				memory.grow(Math.ceil((needed - memory.buffer.byteLength) / PAGE));
			}
			// grow() detaches the old buffer; refresh the view.
			if (mem.buffer !== memory.buffer) {
				mem = new Uint8Array(memory.buffer);
			}
			mem.set(data, heapBase);
			return BigInt.asUintN(64, api.hayahash64(heapBase, data.length, seed));
		},
		hash128(data: Uint8Array, seed: bigint): Hash128 {
			const input = heapBase + 16;
			const needed = input + data.length;
			if (memory.buffer.byteLength < needed) {
				memory.grow(Math.ceil((needed - memory.buffer.byteLength) / PAGE));
			}
			if (mem.buffer !== memory.buffer) {
				mem = new Uint8Array(memory.buffer);
			}
			mem.set(data, input);
			api.hayahash128(input, data.length, seed, heapBase);
			const view = new DataView(memory.buffer);
			return {
				lo: view.getBigUint64(heapBase, true),
				hi: view.getBigUint64(heapBase + 8, true),
			};
		},
	};
}

// Returns null when WebAssembly is unavailable or when compiling the
// embedded bytes is blocked (a Content Security Policy without
// 'wasm-unsafe-eval', or edge runtimes like Cloudflare Workers that
// disallow runtime wasm compilation); index.ts then falls back to
// the pure engine. The module is ~3 KB, below the 4 KB limit
// browsers place on synchronous compilation, so no async
// initialization is needed.
export function initWasm(): Engine | null {
	try {
		return initWasmFromModule(new WebAssembly.Module(decodeBase64(wasmBase64)));
	} catch {
		return null;
	}
}
