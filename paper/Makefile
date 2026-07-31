ROOT := ..
BUILD := build
OUT := $(ROOT)/output/pdf
PDF := $(OUT)/hayahash-paper.pdf
TECTONIC ?= tectonic
CC ?= cc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra -Werror
# The audited snapshot is defined once, in snapshot.tex.
SNAPSHOT := $(shell sed -n 's/^\\newcommand{\\SnapshotCommit}{\(.*\)}$$/\1/p' snapshot.tex)
HEADER_SHA256 := $(shell sed -n 's/^\\newcommand{\\SnapshotHeaderSha}{\(.*\)}$$/\1/p' snapshot.tex)

.PHONY: all clean check check-reference check-quality check-snapshot

all: $(PDF)

$(PDF): main.tex snapshot.tex references.bib
	mkdir -p $(BUILD) $(OUT)
	$(TECTONIC) --keep-logs --keep-intermediates --outdir $(BUILD) main.tex
	cp $(BUILD)/main.pdf $(PDF)

check: check-reference check-quality check-snapshot

check-reference: $(BUILD)/reference_check
	$(BUILD)/reference_check

$(BUILD)/reference_check: tools/reference_check.c $(ROOT)/hayahash.h
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -I$(ROOT) -o $@ tools/reference_check.c

check-quality:
	$(MAKE) -B -C $(ROOT)/tests CC="$(CC)" ARCH= CFLAGS="$(CFLAGS)" run-quality

check-snapshot:
	test -n "$(SNAPSHOT)"
	test -n "$(HEADER_SHA256)"
	git -C $(ROOT) merge-base --is-ancestor $(SNAPSHOT) HEAD
	test "$$(shasum -a 256 $(ROOT)/hayahash.h | awk '{print $$1}')" = "$(HEADER_SHA256)"
	grep -q "$(SNAPSHOT)" AUDIT.md

clean:
	rm -rf $(BUILD)
