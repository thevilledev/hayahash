// Driver for playground.html.
//
// Two engines power the page. The shootout module (bench.wasm, from
// tests/wasm/bench_wasm.c) holds five hashes compiled from unmodified
// upstream sources with one compiler and one set of flags. Its timing
// loops run inside wasm, so the JS boundary is not part of a
// measurement. The calculator and the file hasher use the npm package
// modules (vendor/, from js/dist), so they show what a package
// consumer gets, input copy included.
//
// Before any measurement the page recomputes every line of
// vendor/kat.txt and compares. The hayahash subset of that file is
// diffed against the committed, natively verified kat_haya.txt at
// build time (scripts/build-playground.sh).
//
// This is free and unencumbered software released into the public
// domain. For more information, please refer to <https://unlicense.org/>
import { hayahash64, hayahash64Pure, getEngine } from "./vendor/index.js";

const $ = (id) => document.getElementById(id);

// ---------------------------------------------------------------
// Shootout module
// ---------------------------------------------------------------

// wasi-libc links in a few OS imports we never call at runtime.
const wasiStubs = new Proxy({}, { get: () => () => 0 });

let ex = null; // shootout wasm exports
let mem = null; // view of the 1 MiB benchmark buffer

async function loadShootout() {
	const req = fetch("bench.wasm");
	let result;
	try {
		result = await WebAssembly.instantiateStreaming(req, {
			wasi_snapshot_preview1: wasiStubs,
		});
	} catch {
		// Server without the wasm MIME type: fall back to bytes.
		const res = await fetch("bench.wasm");
		if (!res.ok) {
			throw new Error(`bench.wasm: HTTP ${res.status}`);
		}
		result = await WebAssembly.instantiate(await res.arrayBuffer(), {
			wasi_snapshot_preview1: wasiStubs,
		});
	}
	return result.instance.exports;
}

// Deterministic pseudo-random fill (xorshift32). The known answers
// in vendor/kat.txt assume exactly this fill; keep it in sync with
// tests/wasm/driver.mjs and kat_native.c.
function fillBuffer(exports) {
	const ptr = exports.buf_ptr();
	const cap = exports.buf_cap();
	const view = new Uint8Array(exports.memory.buffer, ptr, cap);
	let x = 0x9e3779b9 >>> 0;
	for (let i = 0; i < cap; i++) {
		x = (x ^ (x << 13)) >>> 0;
		x = (x ^ (x >>> 17)) >>> 0;
		x = (x ^ (x << 5)) >>> 0;
		view[i] = x & 0xff;
	}
	return view;
}

async function selfCheck(exports) {
	const res = await fetch("vendor/kat.txt");
	if (!res.ok) {
		throw new Error(`vendor/kat.txt: HTTP ${res.status}`);
	}
	const lines = (await res.text()).trim().split("\n");
	for (const line of lines) {
		const [key, len, want] = line.split(" ");
		const got = BigInt.asUintN(64, exports["one_" + key](Number(len), 0x1234n))
			.toString(16)
			.padStart(16, "0");
		if (got !== want) {
			throw new Error(`${key} at ${len} bytes: got ${got}, want ${want}`);
		}
	}
	return lines.length;
}

// ---------------------------------------------------------------
// Measurement
// ---------------------------------------------------------------

// Yield one macrotask so the status line can paint between rounds.
// MessageChannel, not setTimeout: browsers clamp timers hard when the
// page loses focus, and a clamped yield would stretch the test from
// seconds to minutes. Message tasks are not clamped.
const yieldUI = () =>
	new Promise((resolve) => {
		const ch = new MessageChannel();
		ch.port1.onmessage = () => resolve();
		ch.port2.postMessage(0);
	});

// Smallest observable performance.now() increment. Browsers coarsen
// the timer; each measured round runs long enough (TARGET_MS) that
// the step stays below a few percent of the reading.
function timerStep() {
	let step = Infinity;
	let last = performance.now();
	for (let i = 0; i < 100; i++) {
		let t = performance.now();
		while (t === last) {
			t = performance.now();
		}
		if (t - last < step) {
			step = t - last;
		}
		last = t;
	}
	return step;
}

const TARGET_MS = 50;
const ROUNDS = 3;

// runner(iters) does the work and returns elapsed ms.
// Grows iters until one call is measurable, scales it to TARGET_MS,
// then keeps the best of ROUNDS rounds.
async function measureCell(runner) {
	let iters = 1;
	let t = await runner(iters);
	while (t < 8 && iters < 1 << 28) {
		iters *= 8;
		await yieldUI();
		t = await runner(iters);
	}
	iters = Math.max(1, Math.round((iters * TARGET_MS) / Math.max(t, 0.25)));
	let best = Infinity;
	for (let r = 0; r < ROUNDS; r++) {
		await yieldUI();
		const dt = await runner(iters);
		if (dt < best) {
			best = dt;
		}
	}
	return { ms: best, iters };
}

const timed = (f) => {
	const t0 = performance.now();
	f();
	return performance.now() - t0;
};

// Independent hashes, accumulated: throughput with instruction-level
// parallelism allowed. Mirrors tp_* in bench_wasm.c.
function bulkRunner(row, size) {
	if (row.kind === "wasm") {
		return async (iters) => timed(() => ex["tp_" + row.key](size, iters, 42n));
	}
	if (row.kind === "purejs") {
		const view = mem.subarray(0, size);
		return async (iters) => {
			return timed(() => {
				let acc = 0n;
				for (let i = 0; i < iters; i++) {
					acc ^= hayahash64Pure(view, 42n + BigInt(i));
				}
				globalThis.__sink = acc;
			});
		};
	}
	// WebCrypto. One digest call at a time, so the promise round
	// trip is part of the number. That is what a caller gets.
	const view = mem.subarray(0, size);
	return async (iters) => {
		const t0 = performance.now();
		for (let i = 0; i < iters; i++) {
			await crypto.subtle.digest("SHA-256", view);
		}
		return performance.now() - t0;
	};
}

// Seed-chained hashes: each digest seeds the next call. Dependent
// latency, what a lookup chain feels like. Mirrors lat_*.
function smallRunner(row, len) {
	if (row.kind === "wasm") {
		return async (iters) => timed(() => ex["lat_" + row.key](len, iters, 0x1234n));
	}
	const view = mem.subarray(0, len);
	return async (iters) => {
		return timed(() => {
			let h = 0x1234n;
			for (let i = 0; i < iters; i++) {
				h = hayahash64Pure(view, h);
			}
			globalThis.__sink = h;
		});
	};
}

// ---------------------------------------------------------------
// Tables
// ---------------------------------------------------------------

const ROWS = [
	{ key: "haya", label: "hayahash64", kind: "wasm", self: true },
	{ key: "chibi2", label: "ChibiHash v2", kind: "wasm" },
	{ key: "rapid", label: "rapidhash v3", kind: "wasm" },
	{ key: "xxh3", label: "XXH3-64", kind: "wasm" },
	{ key: "xxh64", label: "XXH64", kind: "wasm" },
	{ key: "purejs", label: "hayahash64, pure JS", kind: "purejs" },
	{ key: "sha256", label: "SHA-256, WebCrypto", kind: "sha256", bulkOnly: true },
];

const BULK_SIZES = [16384, 1048576];
const SMALL_LENS = [8, 64, 256];

const fmtGbps = (v) => (v >= 1 ? v.toFixed(2) : v.toFixed(3));
const fmtNs = (v) => (v >= 100 ? v.toFixed(0) : v.toFixed(1));

function makeRow(tbody, row, cols) {
	const tr = document.createElement("tr");
	if (row.self) {
		tr.className = "self";
	}
	const th = document.createElement("th");
	th.scope = "row";
	th.textContent = row.label;
	tr.appendChild(th);
	const cells = [];
	for (let i = 0; i < cols; i++) {
		const td = document.createElement("td");
		td.className = "n";
		td.textContent = "-";
		tr.appendChild(td);
		cells.push(td);
	}
	const barTd = document.createElement("td");
	barTd.className = "bar-cell";
	tr.appendChild(barTd);
	tbody.appendChild(tr);
	return { tr, cells, barTd };
}

// Bar length is a rate, so a long bar is fast in both tables. For
// ns/hash (betterAsc) the bar shows the reciprocal. The exact value
// sits in the cells and in a title tooltip on the bar.
function finishTable(tbody, entries, barIndex, fmt, betterAsc) {
	const done = entries.filter((e) => e.values[barIndex] !== null);
	const barVal = (v) => (betterAsc ? 1 / v : v);
	const max = Math.max(...done.map((e) => barVal(e.values[barIndex])));
	for (const e of done) {
		const v = e.values[barIndex];
		const bar = document.createElement("div");
		bar.className = "bar";
		bar.style.width = `${Math.max(0.5, (barVal(v) / max) * 100)}%`;
		e.barTd.title = fmt(v);
		e.barTd.replaceChildren(bar);
	}
	entries
		.slice()
		.sort((a, b) => {
			const av = a.values[barIndex];
			const bv = b.values[barIndex];
			if (av === null) return 1;
			if (bv === null) return -1;
			return betterAsc ? av - bv : bv - av;
		})
		.forEach((e) => tbody.appendChild(e.tr));
}

// ---------------------------------------------------------------
// The speed test
// ---------------------------------------------------------------

let running = false;

async function runSpeedTest() {
	if (running || ex === null) {
		return;
	}
	running = true;
	const button = $("run-bench");
	const status = $("bench-status");
	button.disabled = true;
	// Browsers give a background tab less CPU. Warn when that
	// happened instead of publishing quietly low numbers.
	let wasHidden = document.hidden;
	const onVis = () => {
		wasHidden = wasHidden || document.hidden;
	};
	document.addEventListener("visibilitychange", onVis);
	try {
		const hasSubtle = typeof crypto !== "undefined" && !!crypto.subtle;
		const bulkRows = ROWS.filter((r) => r.kind !== "sha256" || hasSubtle);
		const smallRows = ROWS.filter((r) => !r.bulkOnly);
		const total =
			bulkRows.length * BULK_SIZES.length + smallRows.length * SMALL_LENS.length;
		let cell = 0;

		const bulkBody = $("bulk-body");
		const smallBody = $("small-body");
		bulkBody.replaceChildren();
		smallBody.replaceChildren();
		$("bench-results").hidden = false;

		const step = timerStep();
		$("bench-env").textContent =
			`Your run: ${navigator.userAgent}. ` +
			`${navigator.hardwareConcurrency ?? "?"} logical cores. ` +
			`Timer step ${step.toFixed(3)} ms, ` +
			`${ROUNDS} rounds of about ${TARGET_MS} ms per cell, best round kept.`;

		// Warm up so the engine tiers up every export first.
		for (const row of ROWS.filter((r) => r.kind === "wasm")) {
			ex["lat_" + row.key](64, 20000, 1n);
			ex["tp_" + row.key](16384, 100, 1n);
		}
		await yieldUI();

		const bulkEntries = [];
		for (const row of bulkRows) {
			const e = makeRow(bulkBody, row, BULK_SIZES.length);
			e.values = new Array(BULK_SIZES.length).fill(null);
			bulkEntries.push(e);
		}
		for (let s = 0; s < BULK_SIZES.length; s++) {
			const size = BULK_SIZES[s];
			for (let i = 0; i < bulkRows.length; i++) {
				cell++;
				status.textContent = `Measuring ${cell} of ${total}: ${bulkRows[i].label}, bulk ${size >= 1048576 ? "1 MiB" : "16 KiB"}.`;
				await yieldUI();
				const { ms, iters } = await measureCell(bulkRunner(bulkRows[i], size));
				const gbps = (size * iters) / (ms / 1000) / 1e9;
				bulkEntries[i].values[s] = gbps;
				bulkEntries[i].cells[s].textContent = fmtGbps(gbps);
			}
		}
		finishTable(
			bulkBody,
			bulkEntries,
			BULK_SIZES.length - 1,
			(v) => `${fmtGbps(v)} GB/s`,
			false,
		);

		const smallEntries = [];
		for (const row of smallRows) {
			const e = makeRow(smallBody, row, SMALL_LENS.length);
			e.values = new Array(SMALL_LENS.length).fill(null);
			smallEntries.push(e);
		}
		for (let s = 0; s < SMALL_LENS.length; s++) {
			const len = SMALL_LENS[s];
			for (let i = 0; i < smallRows.length; i++) {
				cell++;
				status.textContent = `Measuring ${cell} of ${total}: ${smallRows[i].label}, ${len} B keys.`;
				await yieldUI();
				const { ms, iters } = await measureCell(smallRunner(smallRows[i], len));
				const ns = (ms * 1e6) / iters;
				smallEntries[i].values[s] = ns;
				smallEntries[i].cells[s].textContent = fmtNs(ns);
			}
		}
		finishTable(smallBody, smallEntries, 0, (v) => `${fmtNs(v)} ns/hash`, true);

		let done = "Done.";
		if (wasHidden) {
			done +=
				" This tab was in the background during the run. Background tabs get less CPU, so expect low numbers. Keep the tab in front for a fair run.";
		}
		if (!hasSubtle) {
			done +=
				" No SHA-256 row: this page is not in a secure context, so WebCrypto is off.";
		}
		status.textContent = `${done} Press the button to run again.`;
	} catch (err) {
		status.textContent = `The speed test failed: ${err.message}`;
	} finally {
		document.removeEventListener("visibilitychange", onVis);
		button.disabled = false;
		running = false;
	}
}

// ---------------------------------------------------------------
// Calculator
// ---------------------------------------------------------------

function parseSeed(text) {
	const t = text.trim();
	return t === "" ? 0n : BigInt(t);
}

function updateDigest() {
	const out = $("hash-digest");
	const bytes = new TextEncoder().encode($("hash-input").value);
	let seed;
	try {
		seed = parseSeed($("hash-seed").value);
	} catch {
		out.textContent = "seed must be an integer, decimal or 0x hex";
		return;
	}
	const d = hayahash64(bytes, seed);
	out.textContent = `0x${d.toString(16).padStart(16, "0")}`;
	$("hash-bytes").textContent = `${bytes.length} bytes of UTF-8, engine: ${getEngine()}`;
}

// ---------------------------------------------------------------
// File hashing
// ---------------------------------------------------------------

async function hashFile(file) {
	const out = $("file-result");
	if (file.size > 0x7fffffff) {
		out.textContent =
			"The file is 2 GiB or larger. The wasm engine takes less than 2 GiB in one call.";
		return;
	}
	out.textContent = `Reading ${file.name}...`;
	try {
		const bytes = new Uint8Array(await file.arrayBuffer());
		const t0 = performance.now();
		const digest = hayahash64(bytes, 0n);
		let ms = performance.now() - t0;
		let note = "";
		// A small file finishes below the timer step. Repeat it for
		// about 100 ms and report the mean.
		if (ms < 20 && bytes.length > 0) {
			let n = Math.max(1, Math.round(100 / Math.max(ms, 0.01)));
			if (n > 200000) {
				n = 200000;
			}
			const t1 = performance.now();
			for (let i = 0; i < n; i++) {
				hayahash64(bytes, 0n);
			}
			ms = (performance.now() - t1) / n;
			note = `, mean of ${n} runs`;
		}
		const rate = bytes.length / (ms / 1000) / 1e9;
		out.innerHTML = "";
		const code = document.createElement("code");
		code.textContent = `0x${digest.toString(16).padStart(16, "0")}`;
		out.append(
			`${file.name}: ${bytes.length.toLocaleString("en")} bytes, seed 0, hash value `,
			code,
			`. Hashed in ${ms >= 10 ? ms.toFixed(1) : ms.toFixed(3)} ms${note}. ` +
				`That is ${fmtGbps(rate)} GB/s through the npm package, input copy included.`,
		);
	} catch (err) {
		out.textContent = `Could not hash the file: ${err.message}`;
	}
}

// ---------------------------------------------------------------
// Init
// ---------------------------------------------------------------

async function init() {
	$("hash-input").addEventListener("input", updateDigest);
	$("hash-seed").addEventListener("input", updateDigest);
	updateDigest();

	$("file-input").addEventListener("change", (ev) => {
		const f = ev.target.files[0];
		if (f) {
			hashFile(f);
		}
	});

	const status = $("bench-status");
	try {
		status.textContent = "Loading the shootout module...";
		ex = await loadShootout();
		mem = fillBuffer(ex);
		const n = await selfCheck(ex);
		status.textContent = `Ready. The module computed all ${n} known answers correctly. Press the button.`;
		$("run-bench").disabled = false;
		$("run-bench").addEventListener("click", runSpeedTest);
	} catch (err) {
		status.textContent =
			`The shootout module did not load: ${err.message}. ` +
			"The calculator and the file hasher below still work.";
	}
}

init();
