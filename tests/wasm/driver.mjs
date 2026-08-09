// Node/V8 driver for bench_wasm.c. Times whole wasm calls; each call
// loops internally, so JS call overhead is amortized away.
//
//   node driver.mjs bench.wasm          run the shootout
//   node driver.mjs bench.wasm --kat    print known-answer lines
//                                       (diffed against kat_native)
//
// Workload knobs (defaults in parentheses):
//   BULK_MB (256)     bytes hashed per bulk round, in MB
//   BULK_ROUNDS (7)   rounds per bulk cell, best-of
//   SMALL_ITERS (4e6) chained hashes per small-key round
//   SMALL_ROUNDS (5)  rounds per small-key cell, best-of
import { readFile } from "node:fs/promises";
import os from "node:os";

const wasmPath = process.argv[2] ?? "bench.wasm";
// wasi-libc links in a few OS imports we never call at runtime.
const wasiStubs = new Proxy({}, { get: () => () => 0 });
const { instance } = await WebAssembly.instantiate(await readFile(wasmPath), {
  wasi_snapshot_preview1: wasiStubs,
});
const ex = instance.exports;

const ptr = ex.buf_ptr();
const cap = ex.buf_cap();
const mem = new Uint8Array(ex.memory.buffer, ptr, cap);
// Deterministic pseudo-random fill (xorshift32); keep in sync with
// the fill loop in kat_native.c. Note >>> everywhere: JS >> is a
// signed shift and would diverge from the C uint32_t semantics.
let x = 0x9e3779b9 >>> 0;
for (let i = 0; i < cap; i++) {
  x = (x ^ (x << 13)) >>> 0;
  x = (x ^ (x >>> 17)) >>> 0;
  x = (x ^ (x << 5)) >>> 0;
  mem[i] = x & 0xff;
}

const hashes = ["haya", "haya128", "chibi2", "rapid", "xxh3", "xxh64"]
  .filter((h) => ("one_" + h) in ex);

if (process.argv.includes("--kat")) {
  for (const h of hashes) {
    const lens = [0, 1, 3, 7, 8, 16, 17, 31, 32, 63, 64, 319, 320, 1000];
    if (h === "haya128") {
      for (const word of ["lo", "hi"]) {
        for (const len of lens) {
          const v = BigInt.asUintN(64, ex[`one_haya128_${word}`](len, 0x1234n));
          console.log(`haya128_${word} ${len} ${v.toString(16).padStart(16, "0")}`);
        }
      }
    } else {
      for (const len of lens) {
        const v = BigInt.asUintN(64, ex["one_" + h](len, 0x1234n));
        console.log(`${h} ${len} ${v.toString(16).padStart(16, "0")}`);
      }
    }
  }
  process.exit(0);
}

const BULK_MB = Number(process.env.BULK_MB ?? 256);
const BULK_ROUNDS = Number(process.env.BULK_ROUNDS ?? 7);
const SMALL_ITERS = Number(process.env.SMALL_ITERS ?? 4_000_000);
const SMALL_ROUNDS = Number(process.env.SMALL_ROUNDS ?? 5);

console.log(`# node ${process.version}, v8 ${process.versions.v8}`);
console.log(`# ${os.cpus()[0]?.model ?? "unknown cpu"} (${os.arch()})`);

function bestOf(rounds, f) {
  let best = Infinity;
  for (let r = 0; r < rounds; r++) {
    const t0 = performance.now();
    f();
    const dt = performance.now() - t0;
    if (dt < best) best = dt;
  }
  return best / 1000; // seconds
}

// Warm up every export so V8 tiers up before measurement.
for (const h of hashes) {
  ex["lat_" + h](64, 20000, 1n);
  ex["tp_" + h](16384, 200, 1n);
}

console.log("\n## bulk throughput (GB/s, higher is better)");
console.log(["size", ...hashes].join("\t"));
for (const size of [1024, 16384, 262144, 1048576]) {
  const iters = Math.max(16, Math.floor((BULK_MB << 20) / size));
  const row = [size];
  for (const h of hashes) {
    const sec = bestOf(BULK_ROUNDS, () => ex["tp_" + h](size, iters, 42n));
    row.push((size * iters / sec / 1e9).toFixed(2));
  }
  console.log(row.join("\t"));
}

console.log("\n## small-key latency, seed-chained (ns/hash, lower is better)");
console.log(["len", ...hashes].join("\t"));
for (const len of [4, 8, 16, 32, 64, 128, 256]) {
  const row = [len];
  for (const h of hashes) {
    const sec = bestOf(SMALL_ROUNDS, () => ex["lat_" + h](len, SMALL_ITERS, 0x1234n));
    row.push((sec / SMALL_ITERS * 1e9).toFixed(1));
  }
  console.log(row.join("\t"));
}
