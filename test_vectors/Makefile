CC      ?= cc
CFLAGS  ?= -O2 -std=c11 -Wall -Wextra
INC     = -I..
CURRENT = v0.5.0.txt

.PHONY: all check regenerate clean

all: generate

generate: generate.c ../hayahash.h
	$(CC) $(CFLAGS) $(INC) -o $@ generate.c

# Regenerate the committed vector file for the current digest series.
regenerate: generate
	./generate > $(CURRENT)

# Recompute from hayahash.h and require an exact match to the committed file.
check: generate
	./generate > .generated.txt
	diff -u $(CURRENT) .generated.txt
	rm -f .generated.txt
	@echo "test_vectors/$(CURRENT) matches hayahash.h"

clean:
	rm -f generate .generated.txt
