#!/usr/bin/env node

// Give a complete playground asset set one content-derived namespace and
// publish the mutable manifest only after that namespace is ready.

import { createHash } from "node:crypto";
import { readdir, readFile, rename, writeFile } from "node:fs/promises";
import { basename, dirname, join, posix, relative } from "node:path";

const [stageDirectory, manifestPath] = process.argv.slice(2);
if (!stageDirectory || !manifestPath) {
	throw new Error(
		"usage: version-playground.mjs <staging-directory> <manifest-path>",
	);
}

async function filesUnder(directory) {
	const files = [];
	for (const entry of await readdir(directory, { withFileTypes: true })) {
		const path = join(directory, entry.name);
		if (entry.isDirectory()) {
			files.push(...(await filesUnder(path)));
		} else if (entry.isFile()) {
			files.push(path);
		}
	}
	return files;
}

const paths = await filesUnder(stageDirectory);
paths.sort((a, b) =>
	relative(stageDirectory, a).localeCompare(relative(stageDirectory, b), "en"),
);

const files = [];
for (const path of paths) {
	files.push({
		name: relative(stageDirectory, path).split("\\").join("/"),
		bytes: await readFile(path),
	});
}

// The set has to be self-contained before it earns a namespace. The browser
// loads the entry module and everything below it from inside
// assets/<version>/, so one file missing from the set breaks the whole
// module graph -- and the page can only report "Importing a module script
// failed", with no clue which file the browser wanted. Resolve every
// reference here instead, while the set is still staged and nameable.
//
// Specifiers never contain whitespace, which keeps these patterns off the
// base64 payload in wasm-module.js.
const IMPORT_PATTERNS = [
	/\bfrom\s*["']([^"'\s]+)["']/g, // import ... from "x", export ... from "x"
	/\bimport\s*\(\s*["']([^"'\s]+)["']/g, // import("x")
	/\bimport\s+["']([^"'\s]+)["']/g, // import "x"
];
// Runtime fetches: playground.js reaches bench.wasm and vendor/kat.txt this
// way, and they have to travel with the modules that read them.
const URL_PATTERN =
	/\bnew URL\(\s*["']([^"'\s]+)["']\s*,\s*import\.meta\.url\s*\)/g;

// Blank the lines that only carry prose. The npm package documents its own
// usage in JSDoc ("import { setWasmModule } from \"hayahash\";"), and those
// examples are not fetches this bundle has to satisfy. Emitted code always
// opens a statement at the start of a line, so dropping comment lines cannot
// hide a real import.
function code(source) {
	return source
		.split("\n")
		.map((line) => (/^\s*(?:\*|\/\/|\/\*)/.test(line) ? "" : line))
		.join("\n");
}

function referencesIn(source) {
	const references = [];
	const scanned = code(source);
	for (const pattern of IMPORT_PATTERNS) {
		for (const [, specifier] of scanned.matchAll(pattern)) {
			references.push({ specifier, kind: "import" });
		}
	}
	for (const [, specifier] of scanned.matchAll(URL_PATTERN)) {
		references.push({ specifier, kind: "url" });
	}
	return references;
}

// Where a reference lands inside the staged set, or null when it leaves it.
// Anything outside cannot be part of an immutable namespace.
function resolveInSet(fromName, target) {
	if (target.startsWith("/")) {
		return null; // site-root, so not this directory
	}
	const resolved = posix.join(posix.dirname(fromName), target);
	return resolved === ".." || resolved.startsWith("../") ? null : resolved;
}

const present = new Set(files.map((file) => file.name));
const problems = [];

for (const file of files) {
	if (!file.name.endsWith(".js")) {
		continue;
	}
	for (const { specifier, kind } of referencesIn(file.bytes.toString("utf8"))) {
		const target = specifier.split(/[?#]/)[0];
		// Another origin fetches on its own terms; nothing for us to publish.
		if (/^[a-z][a-z0-9+.-]*:/i.test(target) || target.startsWith("//")) {
			continue;
		}
		if (
			kind === "import" &&
			!target.startsWith("/") &&
			!target.startsWith("./") &&
			!target.startsWith("../")
		) {
			problems.push(
				`${file.name}: "${specifier}" is a bare specifier, which a browser cannot resolve`,
			);
			continue;
		}
		const resolved = resolveInSet(file.name, target);
		if (resolved === null) {
			problems.push(
				`${file.name}: "${specifier}" points outside the versioned directory, which breaks the immutable namespace`,
			);
		} else if (!present.has(resolved)) {
			problems.push(
				`${file.name}: "${specifier}" resolves to ${resolved}, which is not in the asset set`,
			);
		}
	}
}

if (problems.length > 0) {
	throw new Error(
		`the staged playground asset set is incomplete:\n  ${problems.join("\n  ")}\n` +
			"Every module and runtime fetch has to travel with the set; see scripts/build-playground.sh.",
	);
}

const hash = createHash("sha256");
for (const file of files) {
	hash.update(file.name);
	hash.update("\0");
	hash.update(file.bytes);
	hash.update("\0");
}

const version = hash.digest("hex").slice(0, 16);
const assetRoot = dirname(stageDirectory);
const destination = join(assetRoot, version);
await rename(stageDirectory, destination);

const manifest = {
	version,
	entry: `${basename(assetRoot)}/${version}/playground.js`,
};
await writeFile(manifestPath, `${JSON.stringify(manifest, null, 2)}\n`);

process.stdout.write(version);
