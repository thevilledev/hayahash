#!/usr/bin/env node

// Give a complete playground asset set one content-derived namespace and
// publish the mutable manifest only after that namespace is ready.

import { createHash } from "node:crypto";
import { readdir, readFile, rename, writeFile } from "node:fs/promises";
import { basename, dirname, join, relative } from "node:path";

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

const files = await filesUnder(stageDirectory);
files.sort((a, b) =>
	relative(stageDirectory, a).localeCompare(relative(stageDirectory, b), "en"),
);

const hash = createHash("sha256");
for (const file of files) {
	const name = relative(stageDirectory, file).split("\\").join("/");
	hash.update(name);
	hash.update("\0");
	hash.update(await readFile(file));
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
