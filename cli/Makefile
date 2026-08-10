CC      ?= cc
CFLAGS  ?= -O2 -std=c11 -Wall -Wextra -Werror
INC      = -I..

# Kept out of CFLAGS because CI replaces that wholesale, and these are
# correctness rather than taste: _FILE_OFFSET_BITS lets 32-bit hosts
# open files past 2 GiB, and the version stamp has to match ../VERSION.
VERSION := $(shell tr -d ' \n' < ../VERSION)
DEFS     = -D_FILE_OFFSET_BITS=64 -DHAYASUM_VERSION='"$(VERSION)"'

# Fuzzing. FUZZ_CC must be a clang whose compiler-rt ships libFuzzer
# (Debian/Ubuntu: libclang-rt-<version>-dev). -Wno-unused-function is
# needed because each target includes all of hayasum.c and exercises
# one part of it.
FUZZ_CC       ?= clang
FUZZ_CFLAGS   ?= -O1 -g -std=c11 -Wall -Wextra -Werror -Wno-unused-function
FUZZ_SANITIZE ?= -fsanitize=fuzzer,address,undefined \
                 -fno-sanitize-recover=all -fno-omit-frame-pointer
FUZZ_SECONDS  ?= 30
FUZZ_MAXLEN   ?= 4096
FUZZ_FLAGS     = -max_total_time=$(FUZZ_SECONDS) -timeout=25 \
                 -print_final_stats=1 -artifact_prefix=fuzz/artifacts/

FUZZ_TARGETS   = fuzz/fuzz_args fuzz/fuzz_label fuzz/fuzz_stream \
                 fuzz/fuzz_stream_small
REPLAY_TARGETS = fuzz/fuzz_args-replay fuzz/fuzz_label-replay \
                 fuzz/fuzz_stream-replay fuzz/fuzz_stream_small-replay

.PHONY: all check fuzz fuzz-one fuzz-run fuzz-replay clean install

all: hayasum

hayasum: hayasum.c ../hayahash.h
	$(CC) $(CFLAGS) $(DEFS) $(INC) -o $@ hayasum.c

# One-shot oracle for the test harness: same digest, computed with the
# one-shot entry points over a single in-memory buffer, so any
# disagreement is a bug in hayasum's streaming reader.
tests/refhash: tests/refhash.c ../hayahash.h
	$(CC) $(CFLAGS) $(INC) -o $@ tests/refhash.c

check: hayasum tests/refhash fuzz-replay
	sh tests/run.sh ./hayasum ./tests/refhash

# libFuzzer builds.
fuzz: $(FUZZ_TARGETS)

fuzz/fuzz_stream_small: fuzz/fuzz_stream.c hayasum.c ../hayahash.h
	$(FUZZ_CC) $(FUZZ_CFLAGS) $(FUZZ_SANITIZE) $(DEFS) $(INC) \
		-DHAYASUM_CHUNK=61 -o $@ fuzz/fuzz_stream.c

fuzz/fuzz_%: fuzz/fuzz_%.c hayasum.c ../hayahash.h
	$(FUZZ_CC) $(FUZZ_CFLAGS) $(FUZZ_SANITIZE) $(DEFS) $(INC) -o $@ $<

# Portable drivers over the same targets: no libFuzzer needed, so the
# committed corpus is replayed by every compiler in CI, including under
# ASan/UBSan, as a plain regression suite.
fuzz/fuzz_stream_small-replay: fuzz/fuzz_stream.c fuzz/standalone.c \
		hayasum.c ../hayahash.h
	$(CC) $(CFLAGS) -Wno-unused-function $(DEFS) $(INC) \
		-DHAYASUM_CHUNK=61 -o $@ fuzz/fuzz_stream.c fuzz/standalone.c

fuzz/fuzz_%-replay: fuzz/fuzz_%.c fuzz/standalone.c hayasum.c ../hayahash.h
	$(CC) $(CFLAGS) -Wno-unused-function $(DEFS) $(INC) \
		-o $@ $< fuzz/standalone.c

fuzz-replay: $(REPLAY_TARGETS)
	./fuzz/fuzz_args-replay fuzz/corpus/args
	./fuzz/fuzz_label-replay fuzz/corpus/label
	./fuzz/fuzz_stream-replay fuzz/corpus/stream
	./fuzz/fuzz_stream_small-replay fuzz/corpus/stream

# libFuzzer only writes to the first corpus directory, so give it a
# scratch one and pass the committed seeds read-only behind it: a run
# never edits what is in git.
fuzz-run: fuzz
	mkdir -p fuzz/artifacts/args fuzz/artifacts/label \
		fuzz/artifacts/stream fuzz/artifacts/stream_small
	./fuzz/fuzz_args $(FUZZ_FLAGS) -max_len=$(FUZZ_MAXLEN) \
		fuzz/artifacts/args fuzz/corpus/args
	./fuzz/fuzz_label $(FUZZ_FLAGS) -max_len=$(FUZZ_MAXLEN) \
		fuzz/artifacts/label fuzz/corpus/label
	./fuzz/fuzz_stream $(FUZZ_FLAGS) -max_len=$(FUZZ_MAXLEN) \
		fuzz/artifacts/stream fuzz/corpus/stream
	./fuzz/fuzz_stream_small $(FUZZ_FLAGS) -max_len=$(FUZZ_MAXLEN) \
		fuzz/artifacts/stream_small fuzz/corpus/stream

# One target at a time, for the nightly matrix:
#   make fuzz-one TARGET=fuzz_stream FUZZ_SECONDS=600
FUZZ_ONE_CORPUS = \
	$(if $(filter fuzz_stream_small,$(TARGET)),stream,$(TARGET:fuzz_%=%))

fuzz-one:
	@test -n "$(TARGET)" || { \
		echo "fuzz-one: set TARGET to one of:" \
			"$(notdir $(FUZZ_TARGETS))" >&2; \
		exit 2; }
	$(MAKE) fuzz/$(TARGET)
	mkdir -p fuzz/artifacts/$(TARGET)
	./fuzz/$(TARGET) $(FUZZ_FLAGS) -max_len=$(FUZZ_MAXLEN) \
		fuzz/artifacts/$(TARGET) fuzz/corpus/$(FUZZ_ONE_CORPUS)

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin

install: hayasum
	install -d "$(DESTDIR)$(BINDIR)"
	install -m 755 hayasum "$(DESTDIR)$(BINDIR)/hayasum"

clean:
	rm -f hayasum tests/refhash $(FUZZ_TARGETS) $(REPLAY_TARGETS)
	rm -rf fuzz/artifacts
