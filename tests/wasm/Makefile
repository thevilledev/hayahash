# Wasm shootout and cross-target verification for hayahash64.
#
# bench_wasm.c is compiled twice: to baseline wasm32 with zig cc (no
# SIMD, no wide multiply - what every wasm engine can be assumed to
# support) and natively with any C compiler (via kat_native.c).
# run-kat diffs the hash outputs of the two builds; run-bench times
# the wasm build under Node/V8. See driver.mjs for workload knobs.
#
# Competitor headers (rapidhash: BSD-2, xxHash: BSD-2) are fetched
# pinned to exact upstream commits rather than vendored, so the
# public-domain repository stays license-clean and runs stay
# reproducible.

ZIG    ?= zig
CC     ?= cc
NODE   ?= node
CFLAGS ?= -O3

# hayahash.h lives at the repository root, chibihash_v2.c in tests/.
INC     = -I../.. -I..

RAPIDHASH_COMMIT = 5c62c4b3352c7d4c87ed3429d2329ddaf9b74df9
XXHASH_COMMIT    = c0b5ea995d66691734b1a79ad89e73a0d2fd5a53

.PHONY: all run-kat run-bench clean distclean

all: bench.wasm kat_native

rapidhash.h:
	curl -fsSL -o $@ https://raw.githubusercontent.com/Nicoshev/rapidhash/$(RAPIDHASH_COMMIT)/rapidhash.h

xxhash.h:
	curl -fsSL -o $@ https://raw.githubusercontent.com/Cyan4973/xxHash/$(XXHASH_COMMIT)/xxhash.h

bench.wasm: bench_wasm.c ../../hayahash.h ../chibihash_v2.c rapidhash.h xxhash.h
	$(ZIG) cc -target wasm32-wasi $(CFLAGS) $(INC) -Wl,--no-entry -o $@ bench_wasm.c

kat_native: kat_native.c ../../hayahash.h ../chibihash_v2.c rapidhash.h xxhash.h
	$(CC) $(CFLAGS) $(INC) -o $@ kat_native.c

run-kat: bench.wasm kat_native
	./kat_native > kat_native.txt
	$(NODE) driver.mjs bench.wasm --kat > kat_wasm.txt
	diff kat_native.txt kat_wasm.txt
	@echo "OK: wasm and native builds are bit-exact"

run-bench: bench.wasm
	$(NODE) driver.mjs bench.wasm

clean:
	rm -f bench.wasm kat_native kat_native.txt kat_wasm.txt

distclean: clean
	rm -f rapidhash.h xxhash.h
