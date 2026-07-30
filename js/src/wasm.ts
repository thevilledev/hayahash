// WebAssembly engine: instantiates the embedded wasm module (the
// reference hayahash.h compiled for wasm32-freestanding, see
// wasm/build.sh) and wraps it behind the same (data, seed) -> digest
// signature as the pure engine. Input bytes are copied into linear
// memory at __heap_base; memory is grown on demand and never shrunk.
//
// This is free and unencumbered software released into the public
// domain. For more information, please refer to <https://unlicense.org/>

import { wasmBase64 } from "./wasm-module.js";

const PAGE = 65536;

interface WasmApi {
	memory: WebAssembly.Memory;
	__heap_base: WebAssembly.Global;
	hayahash64: (ptr: number, len: number, seed: bigint) => bigint;
}

export interface Engine {
	hash(data: Uint8Array, seed: bigint): bigint;
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
		!(api.memory instanceof WebAssembly.Memory) ||
		typeof api.__heap_base?.value !== "number"
	) {
		throw new TypeError(
			"not a hayahash wasm module (expected exports: hayahash64, memory, __heap_base)",
		);
	}
	const memory = api.memory;
	const heapBase = api.__heap_base.value as number;
	let mem = new Uint8Array(memory.buffer);
	return {
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
	};
}

// Returns null when WebAssembly is unavailable or when compiling the
// embedded bytes is blocked (a Content Security Policy without
// 'wasm-unsafe-eval', or edge runtimes like Cloudflare Workers that
// disallow runtime wasm compilation); index.ts then falls back to
// the pure engine. The module is ~1.5 KB, below the 4 KB limit
// browsers place on synchronous compilation, so no async
// initialization is needed.
export function initWasm(): Engine | null {
	try {
		return initWasmFromModule(new WebAssembly.Module(decodeBase64(wasmBase64)));
	} catch {
		return null;
	}
}
