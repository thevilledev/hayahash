# hayasum fuzzing

Three parts of hayasum take input it does not control: `argv`, the byte
stream it reads, and the file name it writes back onto the digest line.
Each has a libFuzzer target here. The targets `#include "../hayasum.c"`
with `HAYASUM_NO_MAIN` defined, so they drive the shipped code, not a
copy of it.

Every target asserts invariants rather than only waiting for a crash: a
digest that disagrees with the one-shot reference, or an output line
that could be mistaken for two lines, aborts exactly like a
use-after-free would.

| Target | Drives | Key invariants |
|---|---|---|
| `fuzz_args` | `hayasum_parse_args`, `hayasum_quote` | terminates with one of four statuses; never writes to `argv`; a `RUN` result has a valid width and operand index; an error yields a bounded, single-line, printable-ASCII diagnostic; parsing is deterministic; quoting respects a caller buffer of any size, including 0 |
| `fuzz_stream` | `hayasum_hash_stream` | matches `hayahash64` / `hayahash128` over the same bytes and seed, across in-memory and short-read streams; `h128.lo == h64`; a mid-stream read failure is reported, never digested |
| `fuzz_stream_small` | same, built with `HAYASUM_CHUNK=61` | reaches the multi-iteration and full-buffer paths without 64 KiB inputs |
| `fuzz_label` | `hayasum_print_digest` | exactly one newline, at the end; no carriage return; the escape is lossless and injective; the leading-backslash flag matches whether anything was escaped |

## Running

```sh
make -C cli fuzz-replay                      # corpus as a regression suite
make -C cli fuzz-run FUZZ_SECONDS=60         # all targets, libFuzzer
make -C cli fuzz-one TARGET=fuzz_stream FUZZ_SECONDS=600
```

`fuzz-replay` needs no libFuzzer: `standalone.c` provides a `main` that
feeds the corpus through the same entry point, so every compiler in CI
runs it, including under ASan/UBSan. `fuzz` and `fuzz-run` need a clang
whose compiler-rt ships libFuzzer (`libclang-rt-<version>-dev` on
Debian and Ubuntu).

Runs write new inputs and any crashers under `cli/fuzz/artifacts/`,
which is gitignored; the committed corpus is passed read-only behind
it and never edited by a run.

## Corpus layout

- `corpus/args/` — the argv tokens after `argv[0]`, NUL-separated.
- `corpus/label/` — raw file-name bytes, truncated at the first NUL.
- `corpus/stream/` — 8 bytes of seed, one byte capping how much a single
  underlying read may return, then the message.

The committed entries are seeds, not a coverage set: one per interesting
shape (each buffer boundary, each rejected seed spelling, each escape),
plus a regression entry for anything a run has found.
