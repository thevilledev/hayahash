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
| `-s`, `--seed SEED` | 64-bit seed, decimal or `0x` hex; default `0` |
| `-b`, `--bits 64\|128` | Output width; default `64` |
| `-h`, `--help` | Help |
| `-V`, `--version` | Version, matching the root `VERSION` |

Values may be attached (`-b128`, `--bits=128`) or separate. Options are
recognized only before the first operand, POSIX style; `--` ends them,
so `hayasum -- -b` hashes a file named `-b`. A `FILE` of `-`, or no
`FILE` at all, reads stdin.

128-bit lines print `hi` then `lo` as 32 lowercase hex digits. Digests
match the one-shot and streaming APIs in `hayahash.h`.

Exit status is `0` on success, `1` on an I/O error, and `2` on a usage
error. A digest that fails to reach stdout — a full disk, a closed
descriptor — is reported and exits `1` rather than being dropped
silently.

Seeds are parsed strictly: no leading sign, no surrounding whitespace,
no octal reading of a leading zero, and no wraparound past 2^64-1. A
seed that quietly means something other than what was typed would change
every digest.

One input always produces exactly one output line. A file name holding a
backslash, newline, or carriage return is printed with those bytes
escaped and the line prefixed with `\`, the same convention GNU
coreutils checksum tools use:

```
$ hayasum -- "$(printf 'two\nlines')"
\e8aac707d9be1a37  two\nlines
```

## Tests

```sh
make -C cli check
```

builds hayasum and the one-shot oracle in `tests/refhash.c`, replays the
committed fuzz corpus, and runs `tests/run.sh`: option parsing, exit
statuses, diagnostics, escaping, read and write errors, and a
differential of the chunked reader against the oracle at every size
where its buffering behaviour changes. `tests/run.sh` is POSIX shell and
takes the two binaries as optional arguments.

See [`fuzz/README.md`](fuzz/README.md) for the fuzz targets, which run
on every pull request and nightly.
