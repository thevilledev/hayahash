// Driver for the simulator on design.html.
//
// This file re-implements hayahash64 in BigInt arithmetic, but
// instrumented: instead of only returning the digest, buildSteps()
// records one step per load, absorb, fold, and finalizer stage, with
// a full state snapshot after each step. The page then renders any
// step as a pure function of its index, which is what makes Play,
// Step, Back, and the scrubber trivial and always consistent.
//
// The simulated dataflow is the canonical one (short path, 4-lane mid
// loop, 8-lane bulk loop). The compiler-specific dispatch shapes in
// hayahash.h (length tiers, unrolling, the vectorized bulk spelling)
// are spellings of this same dataflow and make the same values.
//
// The SELF_CHECK vectors at the bottom are pinned to a native build
// of the reference header. If hayahash.h ever drifts from this file,
// the page shows a warning instead of teaching the wrong algorithm.
//
// This is free and unencumbered software released into the public
// domain. For more information, please refer to <https://unlicense.org/>

const MASK64 = (1n << 64n) - 1n;
const K = 0x9e3779b97f4a7c15n;
const M1 = 0x3c79ac492ba7b653n;
const M2 = 0x1c69b3f74ac4ae35n;

const mul = (a, b) => (a * b) & MASK64;
const add = (a, b) => (a + b) & MASK64;
const shl = (x, n) => (x << BigInt(n)) & MASK64;
const shr = (x, n) => x >> BigInt(n);

function rotl(x, n) {
	const r = BigInt(n);
	return ((x << r) | (x >> (64n - r))) & MASK64;
}

function load64(b, off) {
	let v = 0n;
	for (let i = 7; i >= 0; i--) {
		v = (v << 8n) | BigInt(b[off + i]);
	}
	return v;
}

function load32(b, off) {
	let v = 0n;
	for (let i = 3; i >= 0; i--) {
		v = (v << 8n) | BigInt(b[off + i]);
	}
	return v;
}

const inj = (w) => w ^ rotl(w, 21) ^ rotl(w, 41);
const inj2 = (w) => w ^ rotl(w, 11) ^ rotl(w, 50);
const hx = (v) => `0x${v.toString(16).padStart(16, "0")}`;

// ---------------------------------------------------------------
// Step builder
// ---------------------------------------------------------------

// Returns { path, rows, steps, digest }. Each step:
//   title  short label for the position line
//   pre    monospace text for the current-operation box
//   note   one narration paragraph
//   hl     byte highlights: [start, count, cls] with cls "r" (being
//          read), "p" (previous stripe, source of the rotated copy),
//          "o" (read again: overlapping tail read)
//   done   bytes fully absorbed after this step (light gray)
//   snap   state after this step, keyed by row name
export function buildSteps(bytes, seedIn) {
	const len = bytes.length;
	const seed = BigInt.asUintN(64, seedIn);
	const st = {};
	const steps = [];
	let done = 0;
	const push = (title, pre, note, hl = []) => {
		steps.push({ title, pre, note, hl, done, snap: { ...st } });
	};
	const rng = (off, n) =>
		n === 1 ? `byte ${off}` : `bytes ${off}..${off + n - 1}`;

	const s = seed ^ mul(BigInt(len), K);
	st.s = s;
	push(
		"premix",
		`K    = ${hx(K)}   (2^64 / golden ratio, odd)\n` +
			`len  = ${len} bytes\n` +
			`seed = ${hx(seed)}\n` +
			`s    = seed ^ (len * K)\n` +
			`     = ${hx(s)}`,
		"The seed and the length mix into s before any byte is read. " +
			"Every path folds s into its state, so a different length or " +
			"seed changes every step that follows.",
	);

	if (len <= 16) {
		buildShort(bytes, len, s, st, push, rng, (n) => {
			done = n;
		});
		return {
			path: "short",
			rows: ["s", "a", "b", "x", "y", "m"],
			steps,
			digest: st.m,
		};
	}

	const bulk = len >= 320;
	st.h0 = s ^ K;
	st.h1 = add(rotl(s, 17), shl(K, 21));
	st.h2 = rotl(s, 34) ^ shr(K, 13);
	st.h3 = add(rotl(s, 51), shl(K, 42));
	let initPre =
		`h0 = s ^ K                = ${hx(st.h0)}\n` +
		`h1 = rotl(s,17) + (K<<21) = ${hx(st.h1)}\n` +
		`h2 = rotl(s,34) ^ (K>>13) = ${hx(st.h2)}\n` +
		`h3 = rotl(s,51) + (K<<42) = ${hx(st.h3)}`;
	if (bulk) {
		st.h4 = add(s, shr(K, 27));
		st.h5 = rotl(s, 13) ^ shl(K, 9);
		st.h6 = add(rotl(s, 26), shr(K, 40));
		st.h7 = rotl(s, 39) ^ shl(K, 30);
		initPre +=
			`\nh4 = s + (K>>27)          = ${hx(st.h4)}\n` +
			`h5 = rotl(s,13) ^ (K<<9)  = ${hx(st.h5)}\n` +
			`h6 = rotl(s,26) + (K>>40) = ${hx(st.h6)}\n` +
			`h7 = rotl(s,39) ^ (K<<30) = ${hx(st.h7)}`;
	}
	push(
		"lane init",
		initPre,
		(bulk
			? "320 bytes or more: the bulk path. Eight lanes start from s " +
				"and shifted copies of K; each lane will take every 8th " +
				"stripe. "
			: "Four lanes start from s and shifted copies of K. ") +
			"No lane carries a big constant of its own, and the full " +
			"64-bit seed reaches every lane before the first input byte.",
	);

	let wp = 0n;
	let wpOff = -1;
	let p = 0;
	let l = len;
	let stripeNo = 0;

	// One stripe: read 8 bytes into w, absorb t = w + rotl(wp, 27)
	// into lane `li`, then w becomes the next stripe's wp.
	const stripe = (off, li) => {
		const w = load64(bytes, off);
		const t = add(w, rotl(wp, 27));
		const h = `h${li}`;
		st[h] = mul(st[h] ^ t, K);
		stripeNo++;
		const hl = [[off, 8, "r"]];
		let wpLine;
		if (wpOff >= 0) {
			hl.push([wpOff, 8, "p"]);
			wpLine = `rotl(wp, 27)             = ${hx(rotl(wp, 27))}   wp = ${rng(wpOff, 8)}`;
		} else {
			wpLine = `rotl(wp, 27)             = ${hx(0n)}   no stripe before this one`;
		}
		let note;
		if (stripeNo === 1) {
			note =
				"The first stripe: 8 bytes load as one little-endian word " +
				"w. No stripe came before it, so the rotated copy is zero. " +
				"The lane xors w in, then multiplies by K to spread it " +
				"upward.";
		} else if (stripeNo === 2) {
			note =
				"From here on, each stripe brings the previous one along " +
				"as rotl(wp, 27), planting every wp bit a second time, low " +
				"in the next lane. At the first stripe where two inputs " +
				"differ, wp is still equal, so t must differ: the absorb " +
				"sequence is injective.";
		} else {
			note =
				`Stripe ${stripeNo} into lane ${li}: ` +
				"t = w + rotl(wp, 27), then xor into the lane and multiply " +
				"by K.";
		}
		done = off + 8;
		wp = w;
		wpOff = off;
		push(
			`stripe ${stripeNo} -> lane ${li}`,
			`w    = ${rng(off, 8)} (LE)  = ${hx(w)}\n` +
				`${wpLine}\n` +
				`t    = w + rotl(wp, 27)  = ${hx(t)}\n` +
				`${h}   = (${h} ^ t) * K      = ${hx(st[h])}`,
			note,
			hl,
		);
	};

	if (bulk) {
		const blocks = Math.floor(l / 64);
		let block = 0;
		while (l >= 64) {
			block++;
			for (let i = 0; i < 8; i++) {
				stripe(p + 8 * i, i);
			}
			st.h0 = add(st.h0, wp);
			push(
				`checkpoint (block ${block})`,
				`block ${block} of ${blocks} done (${rng(p, 64)})\n` +
					`h0   = h0 + w            = ${hx(st.h0)}\n` +
					`       (w = ${rng(wpOff, 8)}, the raw last word)`,
				"After each 64-byte block, lane 0 also adds the block's " +
					"last raw word. This checkpoint stops a 64-stripe " +
					"rotation-orbit ladder from carrying a difference " +
					"silently across blocks.",
				[[wpOff, 8, "p"]],
			);
			p += 64;
			l -= 64;
		}
		st.h0 = mul(st.h0 ^ rotl(st.h4, 11), K);
		st.h1 = mul(st.h1 ^ rotl(st.h5, 19), K);
		st.h2 = mul(st.h2 ^ rotl(st.h6, 31), K);
		st.h3 = mul(st.h3 ^ rotl(st.h7, 47), K);
		st._folded = true;
		push(
			"lane fold 8 -> 4",
			`h0 = (h0 ^ rotl(h4,11)) * K = ${hx(st.h0)}\n` +
				`h1 = (h1 ^ rotl(h5,19)) * K = ${hx(st.h1)}\n` +
				`h2 = (h2 ^ rotl(h6,31)) * K = ${hx(st.h2)}\n` +
				`h3 = (h3 ^ rotl(h7,47)) * K = ${hx(st.h3)}`,
			"Fewer than 64 bytes remain, so the eight lanes fold into " +
				"four: xor a rotated upper lane, then multiply. An additive " +
				"fold could cancel a difference exactly; this form cannot.",
		);
	}

	// Mid loop: 4 lanes over 32-byte rounds. On the bulk path at most
	// one round remains, and wp chains in from the bulk loop.
	while (l >= 32) {
		for (let i = 0; i < 4; i++) {
			stripe(p + 8 * i, i);
		}
		p += 32;
		l -= 32;
	}

	if (wpOff >= 0) {
		st.h0 = add(st.h0, rotl(wp, 27));
		push(
			"wall absorb",
			`wp   = ${rng(wpOff, 8)}, the last loop stripe\n` +
				`h0   = h0 + rotl(wp, 27) = ${hx(st.h0)}`,
			"The last loop stripe never delivered its rotated copy to a " +
				"next stripe. Lane 0 absorbs it now. Without this wall, a " +
				"difference ladder could walk off the end of the input; " +
				"SMHasher3 found exact collisions that way.",
			[[wpOff, 8, "p"]],
		);
	}

	// Tail: an inj()-spread word absorbed additively into one lane.
	const tailWord = (off, li, note, extraHl = []) => {
		const w = load64(bytes, off);
		const h = `h${li}`;
		st[h] = mul(add(st[h], inj(w)), K);
		const hl = [[off, 8, "r"], ...extraHl];
		done = Math.max(done, off + 8);
		push(
			`tail word -> lane ${li}`,
			`w      = ${rng(off, 8)} (LE)  = ${hx(w)}\n` +
				`inj(w) = w ^ rotl(w,21) ^ rotl(w,41)\n` +
				`       = ${hx(inj(w))}\n` +
				`${h}     = (${h} + inj(w)) * K  = ${hx(st[h])}`,
			note,
			hl,
		);
	};

	if (l > 16) {
		tailWord(
			p,
			0,
			"More than 16 bytes remain after the rounds. The next word " +
				"absorbs through inj(), a bijective three-rotation spread, " +
				"so a difference can never sit only in the top bits going " +
				"into the multiply.",
		);
		tailWord(p + 8, 1, "Same for the following 8 bytes, into lane 1.");
	}
	if (l > 0) {
		const mkNote = (off, last) => {
			const seen = Math.min(8, Math.max(0, done - off));
			const ov =
				seen === 0
					? "None of these 8 bytes were absorbed before."
					: `${seen} of these 8 bytes were already absorbed once ` +
						"(underlined): an overlapping read, not a " +
						"byte-at-a-time loop.";
			return last
				? `The last 8 bytes, into lane 3. ${ov}`
				: "The final 16 bytes always absorb as two words read " +
					`backward from the end. ${ov} The length inside s keeps ` +
					"overlapping reads collision-safe across lengths.";
		};
		const ovHl = (off) => {
			const seen = Math.min(8, Math.max(0, done - off));
			return seen > 0 ? [[off, seen, "o"]] : [];
		};
		const o2 = len - 16;
		const o3 = len - 8;
		tailWord(o2, 2, mkNote(o2, false), ovHl(o2));
		tailWord(o3, 3, mkNote(o3, true), ovHl(o3));
	}

	st.t0 = mul(st.h0 ^ rotl(st.h1, 13), K);
	st.t1 = mul(st.h2 ^ rotl(st.h3, 33), K);
	push(
		"pair fold",
		`t0 = (h0 ^ rotl(h1,13)) * K = ${hx(st.t0)}\n` +
			`t1 = (h2 ^ rotl(h3,33)) * K = ${hx(st.t1)}`,
		"Four lanes merge pairwise into two. The fold rotations 13 and " +
			"33 must not undo the absorb rotation 27; a resonant choice " +
			"collided in an earlier version of the design.",
	);
	st.x = s ^ st.t0 ^ rotl(st.t1, 29);
	push(
		"combine",
		`x  = s ^ t0 ^ rotl(t1,29)   = ${hx(st.x)}`,
		"Both halves and s collapse into one 64-bit value. All that " +
			"remains is avalanche.",
	);

	let before = st.x;
	st.x = st.x ^ shr(st.x, 37);
	push(
		"finalize 1/3",
		`x ^= x >> 37\n   ${hx(before)}\n-> ${hx(st.x)}`,
		"Finalizer: an xor-shift folds the top 37 bits down onto the " +
			"low bits.",
	);
	before = st.x;
	st.x = mul(st.x, K);
	push(
		"finalize 2/3",
		`x *= K\n   ${hx(before)}\n-> ${hx(st.x)}`,
		"One multiply by K spreads the mixed bits back upward. Every " +
			"input byte already passed a multiply and a non-linear merge, " +
			"so one round is enough here.",
	);
	before = st.x;
	st.x = st.x ^ shr(st.x, 32);
	push(
		"finalize 3/3",
		`x ^= x >> 32\n   ${hx(before)}\n-> ${hx(st.x)}`,
		"A final xor-shift mixes the top half down.",
	);

	push(
		"digest",
		`digest = ${hx(st.x)}`,
		`Done. ${len} bytes, seed ${hx(seed)}: the playground, the C ` +
			"header, and every port compute this same value.",
	);
	return {
		path: bulk ? "bulk" : "mid",
		rows: bulk
			? ["s", "h0", "h1", "h2", "h3", "h4", "h5", "h6", "h7", "t0", "t1", "x"]
			: ["s", "h0", "h1", "h2", "h3", "t0", "t1", "x"],
		steps,
		digest: st.x,
	};
}

// The 0..16-byte path: two overlapping loads, two independent
// multiplies, one strong finalizer.
function buildShort(bytes, len, s, st, push, rng, setDone) {
	let a;
	let b;
	if (len >= 8) {
		a = load64(bytes, 0);
		b = load64(bytes, len - 8);
		st.a = a;
		setDone(8);
		push(
			"load a",
			`a = ${rng(0, 8)} (LE)      = ${hx(a)}`,
			"16 bytes or fewer take the short path: exactly two loads, " +
				"two multiplies, one finalizer, for every length. Word a " +
				"reads the first 8 bytes.",
			[[0, 8, "r"]],
		);
		st.b = b;
		setDone(len);
		const ov = Math.max(0, 16 - len);
		push(
			"load b",
			`b = ${rng(len - 8, 8)} (LE)     = ${hx(b)}`,
			`Word b reads the last 8 bytes. ${
				ov > 0
					? `The two reads overlap by ${ov} byte${ov === 1 ? "" : "s"} ` +
						"(underlined): overlap instead of a byte loop, made " +
						"safe by the length inside s."
					: "At 16 bytes the two reads meet exactly."
			}`,
			[
				[len - 8, 8, "r"],
				...(ov > 0 ? [[len - 8, ov, "o"]] : []),
			],
		);
	} else if (len >= 4) {
		a = load32(bytes, 0);
		b = load32(bytes, len - 4);
		st.a = a;
		setDone(4);
		push(
			"load a",
			`a = ${rng(0, 4)} (LE u32)  = ${hx(a)}`,
			"4 to 7 bytes: two 32-bit words cover the input. Word a " +
				"reads the first 4 bytes.",
			[[0, 4, "r"]],
		);
		st.b = b;
		setDone(len);
		const ov = 8 - len;
		push(
			"load b",
			`b = ${rng(len - 4, 4)} (LE u32)  = ${hx(b)}`,
			`Word b reads the last 4 bytes${
				ov > 0
					? `, overlapping a by ${ov} byte${ov === 1 ? "" : "s"} (underlined)`
					: ""
			}. No length uses a byte-at-a-time loop.`,
			[
				[len - 4, 4, "r"],
				...(ov > 0 ? [[len - 4, ov, "o"]] : []),
			],
		);
	} else if (len > 0) {
		a = BigInt(bytes[0]);
		b = (BigInt(bytes[len >> 1]) << 8n) | (BigInt(bytes[len - 1]) << 16n);
		st.a = a;
		setDone(1);
		push(
			"load a",
			`a = byte 0               = ${hx(a)}`,
			"1 to 3 bytes: three sampled bytes cover the whole input. " +
				"a is the first byte.",
			[[0, 1, "r"]],
		);
		st.b = b;
		setDone(len);
		push(
			"load b",
			`b = byte ${len >> 1} << 8 | byte ${len - 1} << 16\n  = ${hx(b)}`,
			"b packs the middle byte and the last byte at distinct " +
				"shifts, so all one-to-three-byte inputs stay distinct.",
			[
				[len >> 1, 1, "r"],
				[len - 1, 1, "r"],
			],
		);
	} else {
		a = 0n;
		b = 0n;
		st.a = a;
		st.b = b;
		push(
			"load a, b",
			"a = 0\nb = 0   (empty input)",
			"Empty input: both words are zero. The digest still depends " +
				"on the seed through s.",
		);
	}

	st.x = mul(inj(a) ^ s ^ K, K);
	push(
		"mix a -> x",
		`inj(a) = a ^ rotl(a,21) ^ rotl(a,41)\n` +
			`       = ${hx(inj(a))}\n` +
			`x      = (inj(a) ^ s ^ K) * K\n` +
			`       = ${hx(st.x)}`,
		"a spreads through inj(), a bijective three-rotation injection: " +
			"every bit of a lands at three positions, so no difference " +
			"can hide in the top bits of the product. The seed sits " +
			"inside the multiply.",
	);
	st.y = mul(inj2(b) ^ rotl(s, 23) ^ shr(K, 19), M1);
	push(
		"mix b -> y",
		`inj2(b) = b ^ rotl(b,11) ^ rotl(b,50)\n` +
			`        = ${hx(inj2(b))}\n` +
			`y       = (inj2(b) ^ rotl(s,23) ^ (K>>19)) * M1\n` +
			`        = ${hx(st.y)}`,
		"b uses a different injection on purpose: with the same one, a " +
			"matched pair of sparse differences in a and b could erase " +
			"the seed from both products at once. SMHasher3 found " +
			"exactly that in an earlier version.",
	);
	st.m = rotl(st.x, 27) ^ st.y;
	push(
		"merge",
		`m = rotl(x,27) ^ y       = ${hx(st.m)}`,
		"The two products merge. Neither depended on the other, so the " +
			"two multiplies run in parallel in hardware.",
	);

	const fm = [
		["m ^= m >> 27", (m) => m ^ shr(m, 27), "an xor-shift folds high bits down"],
		["m *= M1", (m) => mul(m, M1), "a multiply spreads them back up"],
		["m ^= m >> 33", (m) => m ^ shr(m, 33), "an xor-shift folds high bits down"],
		["m *= M2", (m) => mul(m, M2), "a second multiply spreads them up again"],
		["m ^= m >> 27", (m) => m ^ shr(m, 27), "a last xor-shift folds the top down"],
	];
	fm.forEach(([label, f, what], i) => {
		const before = st.m;
		st.m = f(before);
		push(
			`finalize ${i + 1}/5`,
			`${label}\n   ${hx(before)}\n-> ${hx(st.m)}`,
			`moremur finalizer, ${i + 1} of 5: ${what}.`,
		);
	});

	push(
		"digest",
		`digest = ${hx(st.m)}`,
		`Done. ${len} byte${len === 1 ? "" : "s"}: the playground, the C ` +
			"header, and every port compute this same value.",
	);
}

// ---------------------------------------------------------------
// Self-check: pinned to a native build of hayahash.h (v0.4.5).
// ---------------------------------------------------------------

const QUICK = "The quick brown fox jumps over the lazy dog. ";
export const PRESETS = [
	"hello, world",
	"The quick brown fox jumps over the lazy dog, twice.",
	QUICK.repeat(8),
];

const SELF_CHECK = [
	["", 0n, "c4f85f43d5a9985e"],
	[PRESETS[0], 0n, "c26fde83af876d4c"],
	[PRESETS[0], 0x1234n, "632082ecfcb372ea"],
	[PRESETS[1], 0n, "dd3e86af95e79c63"],
	[PRESETS[2], 0n, "981f16befe3fae77"],
	[PRESETS[2], 0xffffffffffffffffn, "32999d7a19232045"],
];

export function selfCheck() {
	const enc = new TextEncoder();
	for (const [text, seed, want] of SELF_CHECK) {
		const got = buildSteps(enc.encode(text), seed)
			.digest.toString(16)
			.padStart(16, "0");
		if (got !== want) {
			return `${text.length} bytes, seed ${seed}: got ${got}, want ${want}`;
		}
	}
	return null;
}

// ---------------------------------------------------------------
// Page wiring
// ---------------------------------------------------------------

const BYTE_CAP = 2048;
const SPEEDS = { slow: 1500, normal: 600, fast: 150 };

function initPage() {
	const $ = (id) => document.getElementById(id);
	const input = $("sim-input");
	const seedInput = $("sim-seed");
	const playBtn = $("sim-play");
	const stepBtn = $("sim-step");
	const backBtn = $("sim-back");
	const restartBtn = $("sim-restart");
	const speedSel = $("sim-speed");
	const scrub = $("sim-scrub");
	const pathEl = $("sim-path");
	const bytesEl = $("sim-bytes");
	const opEl = $("sim-op");
	const stateEl = $("sim-state");
	const noteEl = $("sim-note");
	const posEl = $("sim-pos");
	const digestEl = $("sim-digest");
	const enc = new TextEncoder();

	const checkFail = selfCheck();

	let sim = null;
	let cur = 0;
	let timer = null;
	let byteSpans = [];
	let rowEls = {};

	function setPlaying(on) {
		if (timer !== null) {
			clearInterval(timer);
			timer = null;
		}
		if (on) {
			timer = setInterval(() => {
				if (sim && cur < sim.steps.length - 1) {
					cur++;
					render();
				} else {
					setPlaying(false);
				}
			}, SPEEDS[speedSel.value] || 600);
		}
		playBtn.textContent = on ? "Pause" : "Play";
	}

	function buildByteStrip(bytes) {
		bytesEl.replaceChildren();
		byteSpans = [];
		let group = null;
		for (let i = 0; i < bytes.length; i++) {
			if (i % 8 === 0) {
				group = document.createElement("span");
				group.className = "g";
				bytesEl.appendChild(group);
			}
			const sp = document.createElement("span");
			sp.className = "b";
			sp.textContent = bytes[i].toString(16).padStart(2, "0");
			const ch =
				bytes[i] >= 0x20 && bytes[i] < 0x7f
					? ` '${String.fromCharCode(bytes[i])}'`
					: "";
			sp.title = `byte ${i} = 0x${sp.textContent}${ch}`;
			group.appendChild(sp);
			byteSpans.push(sp);
		}
		if (bytes.length === 0) {
			bytesEl.textContent = "(no bytes)";
		}
	}

	function buildStateRows() {
		stateEl.replaceChildren();
		rowEls = {};
		for (const name of sim.rows) {
			const nm = document.createElement("span");
			nm.className = "nm";
			nm.textContent = name;
			const v = document.createElement("span");
			v.className = "v";
			stateEl.append(nm, v);
			rowEls[name] = { nm, v };
		}
	}

	function renderValue(el, val, prevVal) {
		el.replaceChildren();
		if (val === undefined || val === null) {
			el.textContent = "-";
			return;
		}
		const now = val.toString(16).padStart(16, "0");
		const was =
			prevVal === undefined || prevVal === null
				? null
				: prevVal.toString(16).padStart(16, "0");
		el.append("0x");
		for (let i = 0; i < 16; i++) {
			if (was === null || was[i] !== now[i]) {
				const c = document.createElement("span");
				c.className = "chg";
				c.textContent = now[i];
				el.appendChild(c);
			} else {
				el.append(now[i]);
			}
		}
	}

	function render() {
		const n = sim.steps.length;
		const stp = sim.steps[cur];
		const prevSnap = cur > 0 ? sim.steps[cur - 1].snap : {};

		const marks = new Map();
		for (const [start, count, cls] of stp.hl) {
			for (let i = start; i < start + count; i++) {
				marks.set(i, `${marks.get(i) || ""} ${cls}`);
			}
		}
		for (let i = 0; i < byteSpans.length; i++) {
			byteSpans[i].className =
				`b${i < stp.done ? " d" : ""}${marks.get(i) || ""}`;
		}

		opEl.textContent = stp.pre;
		noteEl.textContent = stp.note;
		for (const name of sim.rows) {
			renderValue(rowEls[name].v, stp.snap[name], prevSnap[name]);
			const cold =
				stp.snap._folded === true && /^h[4-7]$/.test(name);
			rowEls[name].nm.classList.toggle("cold", cold);
			rowEls[name].v.classList.toggle("cold", cold);
		}
		posEl.textContent = `step ${cur + 1} of ${n}: ${stp.title}`;
		scrub.value = String(cur);
		digestEl.textContent =
			cur === n - 1
				? `digest = ${hx(sim.digest)}`
				: "digest = (not yet: play, step, or drag the slider)";
		backBtn.disabled = cur === 0;
		stepBtn.disabled = cur === n - 1;
	}

	function rebuild() {
		setPlaying(false);
		let seed;
		try {
			const t = seedInput.value.trim();
			seed = t === "" ? 0n : BigInt(t);
		} catch {
			pathEl.textContent = "seed must be an integer, decimal or 0x hex";
			return;
		}
		let bytes = enc.encode(input.value);
		let capNote = "";
		if (bytes.length > BYTE_CAP) {
			bytes = bytes.slice(0, BYTE_CAP);
			capNote = ` The simulator caps input at ${BYTE_CAP} bytes; this run hashes the first ${BYTE_CAP}.`;
		}
		sim = buildSteps(bytes, seed);
		const pathText = {
			short: "the short path (16 B or less): two overlapping loads, two multiplies",
			mid: "the mid path (17..319 B): 4 lanes over 32-byte rounds",
			bulk: "the bulk path (320 B and up): 8 lanes over 64-byte blocks",
		}[sim.path];
		pathEl.textContent =
			(checkFail
				? "WARNING: self-check failed, this simulator disagrees with the reference; do not trust it. "
				: "") +
			`${bytes.length} byte${bytes.length === 1 ? "" : "s"} of UTF-8 -> ${pathText}.${capNote}`;
		buildByteStrip(bytes);
		buildStateRows();
		scrub.max = String(sim.steps.length - 1);
		cur = 0;
		render();
	}

	playBtn.addEventListener("click", () => {
		if (timer !== null) {
			setPlaying(false);
			return;
		}
		if (sim && cur >= sim.steps.length - 1) {
			cur = 0;
			render();
		}
		setPlaying(true);
	});
	stepBtn.addEventListener("click", () => {
		setPlaying(false);
		if (sim && cur < sim.steps.length - 1) {
			cur++;
			render();
		}
	});
	backBtn.addEventListener("click", () => {
		setPlaying(false);
		if (sim && cur > 0) {
			cur--;
			render();
		}
	});
	restartBtn.addEventListener("click", () => {
		setPlaying(false);
		cur = 0;
		render();
	});
	scrub.addEventListener("input", () => {
		setPlaying(false);
		cur = Number(scrub.value);
		render();
	});
	speedSel.addEventListener("change", () => {
		if (timer !== null) {
			setPlaying(true);
		}
	});
	input.addEventListener("input", rebuild);
	seedInput.addEventListener("input", rebuild);
	for (const btn of document.querySelectorAll(".sim-preset")) {
		btn.addEventListener("click", () => {
			input.value = PRESETS[Number(btn.dataset.p)];
			rebuild();
		});
	}

	for (const el of [playBtn, stepBtn, backBtn, restartBtn, scrub]) {
		el.disabled = false;
	}
	$("sim-view").hidden = false;
	if (checkFail) {
		console.error(`hayahash simulator self-check failed: ${checkFail}`);
	}
	rebuild();
}

if (typeof document !== "undefined" && document.getElementById("sim-input")) {
	initPage();
}
