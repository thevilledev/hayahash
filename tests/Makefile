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

.PHONY: all run-bench run-quality run-hash128 clean

all: bench quality hash128

bench: bench.c ../hayahash.h chibihash_v2.c $(V1SRC)
	$(CC) $(CFLAGS) $(ARCH) $(INC) -o $@ bench.c $(V1SRC)

quality: quality.c ../hayahash.h chibihash_v2.c $(V1SRC)
	$(CC) $(CFLAGS) $(ARCH) $(INC) -o $@ quality.c $(V1SRC)

hash128: hash128.c ../hayahash.h
	$(CC) $(CFLAGS) $(ARCH) $(INC) -o $@ hash128.c

run-bench: bench
	./bench

run-quality: quality hash128
	./quality haya
	./hash128

run-hash128: hash128
	./hash128

clean:
	rm -f bench quality hash128
