// Quick benchmark of the published API: wasm engine vs the pure-JS
// BigInt fallback. Numbers include the JS -> wasm input copy, i.e.
// they are what a package consumer actually gets. Run `npm run bench`.
import { getEngine, hayahash64, hayahash64Pure } from "../dist/index.js";

console.log(`active engine: ${getEngine()}`);

function bestOf(rounds, f) {
	let best = Infinity;
	for (let r = 0; r < rounds; r++) {
		const t0 = performance.now();
		f();
		const dt = performance.now() - t0;
		if (dt < best) {
			best = dt;
		}
	}
	return best / 1000;
}

function fill(buf) {
	let x = 0x9e3779b9;
	for (let i = 0; i < buf.length; i++) {
		x ^= (x << 13) | 0;
		x ^= x >>> 17;
		x ^= (x << 5) | 0;
		buf[i] = x & 0xff;
	}
	return buf;
}

// Warm up so the JIT tiers up before measurement.
for (let i = 0; i < 50_000; i++) {
	hayahash64("warmup", BigInt(i));
}
hayahash64Pure(fill(new Uint8Array(4096)), 1n);

console.log("\n## bulk throughput (GB/s, higher is better)");
console.log("size\twasm\tpure-js");
for (const size of [1024, 16384, 262144, 1048576]) {
	const buf = fill(new Uint8Array(size));
	const iters = Math.max(8, Math.floor((1 << 28) / size));
	const wasmSec = bestOf(7, () => {
		for (let i = 0; i < iters; i++) {
			hayahash64(buf, 42n);
		}
	});
	const pureIters = Math.max(4, Math.floor(iters / 64));
	const pureSec = bestOf(3, () => {
		for (let i = 0; i < pureIters; i++) {
			hayahash64Pure(buf, 42n);
		}
	});
	const gbps = (sec, n) => ((size * n) / sec / 1e9).toFixed(2);
	console.log(`${size}\t${gbps(wasmSec, iters)}\t${gbps(pureSec, pureIters)}`);
}

console.log("\n## small keys, seed-chained (ns/hash, lower is better)");
console.log("len\twasm\tpure-js");
for (const len of [4, 8, 16, 64, 256]) {
	const buf = fill(new Uint8Array(len));
	const iters = 2_000_000;
	let seed = 0x1234n;
	const wasmSec = bestOf(5, () => {
		for (let i = 0; i < iters; i++) {
			seed = hayahash64(buf, seed);
		}
	});
	const pureIters = iters / 100;
	const pureSec = bestOf(3, () => {
		for (let i = 0; i < pureIters; i++) {
			seed = hayahash64Pure(buf, seed);
		}
	});
	const ns = (sec, n) => ((sec / n) * 1e9).toFixed(1);
	console.log(`${len}\t${ns(wasmSec, iters)}\t${ns(pureSec, pureIters)}`);
}
