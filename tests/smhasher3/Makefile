# SMHasher3 conformance run for hayahash64.
#
# SMHasher3 is fetched at test time and pinned to an exact upstream commit
# for reproducibility. Its test framework is GPL-3.0-or-later and is not
# included in Hayahash release artifacts. Local builds do not change
# Hayahash's Unlicense/public-domain status. Redistribution of the resulting
# combined SMHasher3 executable must comply with the GPL.
#
# The source files in this directory are GPL-3.0-or-later; see COPYING. That
# covers the upstream-modifying patches and the adapter, which is written
# against SMHasher3's headers and macros. It does not cover hayahash.h, which
# stays public domain; the adapter merely includes it.
#
# Fetching rather than vendoring follows tests/wasm/Makefile, which pins
# competitor headers by commit for the same reason.
#
# hayahash.cpp installs into the clone's hashes/ directory and #includes
# ../../hayahash.h directly, so the suite tests the shipped header rather
# than a transcription. See the comment at the top of hayahash.cpp.
#
# PATCHES lists the fixes applied on top of the pin. They are kept as files
# so the delta from upstream is reviewable and archivable, and they are
# applied on every host so all runs build identical source. Like the adapter,
# both carry their own SPDX headers and are excluded from this repository's
# Unlicense; see the note in ../../LICENSE.
#
# Both are temporary. They have been submitted upstream, as merge requests
# !31 (util/Random.cpp) and !32 (platform/family.cmake), and were open as of
# 2026-08-01. When they merge, advance SMHASHER3_COMMIT past them and drop
# the corresponding entry from PATCHES; the harness should then build from
# unmodified upstream.
#
# Both fix macOS issues: the pinned commit does not compile there
# (util/Random.cpp binds a size_t to a uint64_t&, which are the same width
# but distinct types), and its CMake does not recognize Apple Silicon's
# "arm64" as the Arm family, which silently disables NEON for every hash and
# would make a speed comparison there unfair to SIMD hashes.
#
#   make build          configure and build (honors CXX)
#   make verify         print SMHasher3's computed verification values
#   make run            full default test suite, raw output to stdout
#   make distclean      remove the clone entirely
#
# Build with a specific compiler and into a separate tree:
#   make run CXX=clang++ BUILD=build-clang

SMHASHER3_URL    = https://gitlab.com/fwojcik/smhasher3.git
SMHASHER3_COMMIT = 6ab4343396fbe0f7a1c7ac4f01d0eb9acffe4202

PATCHES = 0001-macos-size_t-reference-binding.patch \
          0002-cmake-detect-apple-silicon-as-arm.patch

SRC    = smhasher3
BUILD ?= build

# hayahash.h lives at the repository root.
INC = -I$(abspath ../..)

# SMHasher3 matches hash names case-insensitively.
HASH ?= hayahash64

.PHONY: all build install verify run clean distclean

all: build

# Clone and patch out of tree, then move into place, so a failed checkout or
# patch never leaves a half-configured clone that later runs would treat as
# ready. Make does not remove a directory target when a recipe fails.
$(SRC)/.git:
	rm -rf $(SRC).tmp
	git clone $(SMHASHER3_URL) $(SRC).tmp
	git -C $(SRC).tmp checkout --detach $(SMHASHER3_COMMIT)
	for p in $(PATCHES); do git -C $(SRC).tmp apply $(abspath .)/$$p; done
	mv $(SRC).tmp $(SRC)

# Copy the adapter in and add it to the hash source list. Both steps are
# idempotent, so `make install` is safe to repeat after editing the adapter.
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
	cd $(SRC)/$(BUILD) && CXXFLAGS="$(INC)" cmake ..
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
