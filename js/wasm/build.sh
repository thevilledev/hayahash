#!/bin/sh
# Rebuild wasm/hayahash.wasm from the reference header (../hayahash.h)
# and regenerate the embedded module src/wasm-module.ts.
#
# Requires zig (used purely as a C cross compiler; any version that
# targets wasm32-freestanding works) and node. Both generated files
# are committed, so consumers of the package never need this script.
set -eu
cd "$(dirname "$0")/.."

# -fno-sanitize=undefined: zig cc injects UBSan traps by default; the
#   reference header is UB-free by design and the traps cost size and
#   speed in the hot loop.
# stack-size 32 KiB: a leaf hash function needs far less; this keeps
#   __heap_base low enough that inputs up to ~31 KiB fit in the first
#   64 KiB memory page without a memory.grow.
zig cc --target=wasm32-freestanding -O3 -nostdlib \
	-fno-sanitize=undefined \
	-Wall -Wextra -Werror \
	-isystem wasm/include \
	-Wl,--no-entry \
	-Wl,--export=__heap_base \
	-Wl,-z,stack-size=32768 \
	-Wl,--strip-all \
	-o wasm/hayahash.wasm wasm/shim.c

node scripts/embed.mjs

printf '%s: %s bytes\n' wasm/hayahash.wasm "$(wc -c < wasm/hayahash.wasm | tr -d ' ')"
