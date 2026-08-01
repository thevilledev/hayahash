# Build and test the MIPS64 assembly port under qemu-user.
#
# Requires: gcc-mips64el-linux-gnuabi64, qemu-user (qemu-mips64el)

CROSS   ?= mips64el-linux-gnuabi64-
CC      := $(CROSS)gcc
AS      := $(CROSS)gcc
QEMU    ?= qemu-mips64el

CFLAGS  ?= -O2 -std=c11 -Wall -Wextra -Werror
ASFLAGS ?= -O2
LDFLAGS ?= -static

.PHONY: all test clean

all: kat

hayahash.o: hayahash.S
	$(AS) $(ASFLAGS) -c -o $@ $<

kat.o: kat.c hayahash.h
	$(CC) $(CFLAGS) -c -o $@ $<

kat: kat.o hayahash.o
	$(CC) $(LDFLAGS) -o $@ $^

test: kat
	$(QEMU) ./kat

clean:
	rm -f kat kat.o hayahash.o
