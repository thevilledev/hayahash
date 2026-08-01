# hayahash64 (MIPS64 assembly)

Bit-exact MIPS64 assembly port of the reference C implementation
(`hayahash.h` at the repository root). Output matches every other port
for all inputs and seeds, and little-endian byte loads make the digest
independent of host endianness.

## ABI

```c
uint64_t hayahash64(const void *key, ptrdiff_t len, uint64_t seed);
```

MIPS64 n64 calling convention: `key` in `$a0`, `len` in `$a1`, `seed`
in `$a2`, result in `$v0`. The implementation is in [`hayahash.S`](hayahash.S);
[`hayahash.h`](hayahash.h) is the C declaration for linking.

The compact absorb sequence is used (same as the Go/Zig/Java/wasm
ports). The C header's architecture-specific length tiers are omitted
on purpose; they do not change digests.

## Build and test

Cross-compile for `mips64el` and run under qemu-user:

```bash
sudo apt-get install gcc-mips64el-linux-gnuabi64 qemu-user
make test
```

`make test` builds a static `kat` binary, runs the shared known-answer
vectors plus the SMHasher3 verification value (`0xF3C4A9B4`), and
prints `ok` on success.
