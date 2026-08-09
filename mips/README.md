# hayahash64 and hayahash128 (MIPS64 assembly)

Bit-exact MIPS64 assembly port of the reference C implementation
(`hayahash.h` at the repository root). Output matches every other port
for all inputs and seeds, and little-endian byte loads make the digest
independent of host endianness.

## ABI

```c
uint64_t hayahash64(const void *key, ptrdiff_t len, uint64_t seed);
hayahash128_t hayahash128(const void *key, ptrdiff_t len, uint64_t seed);
```

MIPS64 n64 calling convention: `key` in `$a0`, `len` in `$a1`, `seed`
in `$a2`. The 64-bit result is returned in `$v0`; the 128-bit result
uses `$v0` for `lo` and `$v1` for `hi`, as specified by the n64 struct-return
ABI. The implementation is in [`hayahash.S`](hayahash.S);
[`hayahash.h`](hayahash.h) is the C declaration for linking.

The compact dispatch shape is used (same absorb sequence as the
Go/Zig/Java/wasm ports). The C header's architecture-specific length
tiers are omitted on purpose; they do not change digests.

## Build and test

Cross-compile for `mips64el` and run under qemu-user:

```bash
sudo apt-get install gcc-mips64el-linux-gnuabi64 qemu-user
make test
```

`make test` builds a static `kat` binary, runs the shared known-answer
vectors plus the SMHasher3 verification value (`0x65F2AC15`), and
prints `ok` on success.
