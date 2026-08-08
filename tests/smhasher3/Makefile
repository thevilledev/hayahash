# SPDX-License-Identifier: GPL-3.0-or-later
# SMHasher3 conformance run for hayahash64.
#
# SMHasher3 is fetched at test time and pinned to an exact upstream commit
# for reproducibility. Its test framework is GPL-3.0-or-later and is not
# included in Hayahash release artifacts. Local builds do not change
# Hayahash's Unlicense/public-domain status. Redistribution of the resulting
# combined SMHasher3 executable must comply with the GPL.
#
# This harness Makefile is GPL-3.0-or-later; see COPYING. The mirrored
# hayahash.cpp, the hash algorithm, and the reference hayahash.h remain public
# domain under The Unlicense.
#
# Fetching rather than vendoring follows tests/wasm/Makefile, which pins
# competitor headers by commit for the same reason.
#
# hayahash.cpp is the self-contained SMHasher3 implementation. It must remain
# byte-for-byte identical to hashes/hayahash.cpp in the SMHasher3 repository;
# the harness copies that mirror into the pinned checkout before building.
#
#   make build          configure and build (honors CXX)
#   make verify         print SMHasher3's computed verification values
#   make run            full default test suite, raw output to stdout
#   make distclean      remove the clone entirely
#
# Build with a specific compiler and into a separate tree:
#   make run CXX=clang++ BUILD=build-clang

SMHASHER3_URL    = https://gitlab.com/fwojcik/smhasher3.git
SMHASHER3_COMMIT = 51d3cd1ac0aa4934f6aacb44d9d234f50300b6e3

SRC    = smhasher3
BUILD ?= build

# Extra compiler flags used when CMake is configured. Use this to select a
# dispatch shape the host would not pick by default, for example turning off
# the GCC bulk vectorization gate:
#   make run BUILD=build-gccnv EXTRA_CXXFLAGS=-U__AVX512DQ__
# CMake appends its own -march=native afterwards, but -U still wins.
EXTRA_CXXFLAGS ?=

# SMHasher3 matches hash names case-insensitively.
HASH ?= hayahash

.PHONY: all build install verify run clean distclean

all: build

# Clone out of tree, then move into place, so a failed checkout never leaves a
# half-configured clone that later runs would treat as ready. Make does not
# remove a directory target when a recipe fails.
$(SRC)/.git:
	rm -rf $(SRC).tmp
	git clone $(SMHASHER3_URL) $(SRC).tmp
	git -C $(SRC).tmp checkout --detach $(SMHASHER3_COMMIT)
	mv $(SRC).tmp $(SRC)

# Copy the translation in and add it to the hash source list. Both steps are
# idempotent, so `make install` is safe to repeat after editing the source.
# awk rather than `sed -i`: the -i spelling differs between BSD and GNU sed.
install: $(SRC)/.git hayahash.cpp
	cp hayahash.cpp $(SRC)/hashes/hayahash.cpp
	@if grep -q 'hashes/hayahash.cpp' $(SRC)/hashes/Hashsrc.cmake; then \
	    echo "Hashsrc.cmake already lists hayahash.cpp"; \
	else \
	    awk '/^\)$$/ && !done { print "  hashes/hayahash.cpp"; done = 1 } { print }' \
	        $(SRC)/hashes/Hashsrc.cmake > $(SRC)/hashes/Hashsrc.cmake.new && \
	    mv $(SRC)/hashes/Hashsrc.cmake.new $(SRC)/hashes/Hashsrc.cmake && \
	    echo "Added hashes/hayahash.cpp to Hashsrc.cmake"; \
	fi

build: install
	mkdir -p $(SRC)/$(BUILD)
	cd $(SRC)/$(BUILD) && CXXFLAGS="$(EXTRA_CXXFLAGS)" cmake ..
	$(MAKE) -C $(SRC)/$(BUILD) -j$(shell getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)

# Prints the verification values SMHasher3 computes, for both the canonical
# (CE) and the non-canonical (NE) byte order. Use this to fill in
# verification_LE and verification_BE in hayahash.cpp, then rebuild and
# confirm both report PASS rather than SKIP (unverifiable).
#
# VerifyAll ignores the hashname argument and walks every registered hash,
# so filter its output. SMHasher3 exits nonzero whenever any hash in the
# tree fails its own self-test, which says nothing about this one.
verify: build
	cd $(SRC)/$(BUILD) && ./SMHasher3 --test=VerifyAll --verbose 2>&1 | grep -i haya

run: build
	cd $(SRC)/$(BUILD) && ./SMHasher3 --version
	cd $(SRC)/$(BUILD) && ./SMHasher3 $(HASH)

clean:
	rm -rf $(SRC)/$(BUILD)

distclean:
	rm -rf $(SRC) $(SRC).tmp
