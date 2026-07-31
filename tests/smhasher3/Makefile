# SMHasher3 conformance run for hayahash64.
#
# SMHasher3 is cloned pinned to an exact upstream commit rather than
# vendored: it is GPL-3.0 and this repository is public domain, so keeping
# it out of the tree keeps the repository license-clean, and pinning keeps
# runs reproducible. Same pattern as tests/wasm/Makefile, which fetches
# competitor headers by commit.
#
# hayahash.cpp installs into the clone's hashes/ directory and #includes
# ../../hayahash.h directly, so the suite tests the shipped header rather
# than a transcription. See the comment at the top of hayahash.cpp.
#
# The pinned commit does not compile on macOS: util/Random.cpp binds a
# size_t to a uint64_t& parameter, which are the same width but distinct
# types there. PATCHES lists the fixes applied on top of the pin; they are
# kept as files so the delta from upstream is reviewable and archivable,
# and they are applied on every host so all runs build identical source.
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

PATCHES = 0001-macos-size_t-reference-binding.patch

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
