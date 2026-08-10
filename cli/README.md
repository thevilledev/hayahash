# hayasum

Small CLI that hashes files or stdin with the C reference implementation.

```sh
make -C cli
./cli/hayasum README.md
./cli/hayasum -b 128 -s 0x9E3779B97F4A7C15 data.bin
printf 'hello world' | ./cli/hayasum
```

Options:

| Flag | Meaning |
|---|---|
| `-s seed` | 64-bit seed (`0x` hex or decimal); default `0` |
| `-b 64\|128` | Output width; default `64` |
| `-h` | Help |

128-bit lines print `hi` then `lo` as 32 lowercase hex digits. Digests match
the one-shot and streaming APIs in `hayahash.h`.
