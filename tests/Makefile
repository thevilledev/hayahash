CC      ?= cc
CFLAGS  ?= -O3 -std=c11 -Wall -Wextra
# Native tuning; harmless to drop if the flag is unsupported.
ARCH    ?= -mcpu=native

# hayahash.h lives at the repository root.
INC     = -I..

# ChibiHash v1/v2 reference sources are vendored here (public domain,
# https://github.com/N-R-K/ChibiHash) so the comparison benchmarks are
# self-contained.
V1SRC   = chibihash_v1.c

.PHONY: all run-bench run-quality clean

all: bench quality

bench: bench.c ../hayahash.h chibihash_v2.c $(V1SRC)
	$(CC) $(CFLAGS) $(ARCH) $(INC) -o $@ bench.c $(V1SRC)

quality: quality.c ../hayahash.h chibihash_v2.c $(V1SRC)
	$(CC) $(CFLAGS) $(ARCH) $(INC) -o $@ quality.c $(V1SRC)

run-bench: bench
	./bench

run-quality: quality
	./quality haya

clean:
	rm -f bench quality
