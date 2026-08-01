# Hayahash paper

This directory contains the working paper for the current development
snapshot of Hayahash64.

The draft is intentionally tied to a source revision. The audited commit,
header digest, verification value, and date are defined once, in
`snapshot.tex`; the LaTeX source and the Makefile checks both read from it.
Sections marked TBD in the paper are placeholders with stated completion
criteria; fill them in only together with their artifacts.

## Build

The preferred build uses [Tectonic](https://tectonic-typesetting.github.io/):

```sh
make -C paper
```

The PDF is written to `output/pdf/hayahash-paper.pdf`.

## Check

Run the checks that are available in this repository:

```sh
make -C paper check
```

This performs the following operations:

1. compiles and runs `tools/reference_check.c` against `hayahash.h`;
2. runs the local quality harness; and
3. confirms that the snapshot named in `snapshot.tex` is an ancestor of the
   checkout, that `hayahash.h` still has the audited digest, and that
   `AUDIT.md` references the same snapshot.

The SMHasher3 run is not part of `make check`, because it clones and builds an
external tool and takes minutes per host. Reproduce it separately:

```sh
make -C tests/smhasher3 run
```

That clones SMHasher3 at the pinned upstream commit, applies the adapter and
its portability patches, builds, and runs the default suite. See
`docs/smhasher3.md` for the full procedure, including how to re-derive the
verification values after a digest change and the corrections the speed
figures depend on, and `AUDIT.md` for the claim-by-claim evidence status.

When running a benchmark directly, use `make -B` or compile to a new output
path. The test Makefile does not know that a changed `CC`, `CFLAGS`, or `ARCH`
value should invalidate an existing binary.

## Updating the snapshot

When the algorithm or the header changes, update everything as one
reviewable change:

1. set the new commit, header SHA-256, verification value, and date in
   `snapshot.tex`;
2. regenerate the known-answer vectors and the expected verification value
   in `tools/reference_check.c`;
3. rerun `make -C paper check`, refresh the records under `results/`, and
   rerun the benchmarks the paper transcribes;
4. update the affected tables, the claim register in `AUDIT.md`, and the
   revision history in `main.tex`.

## Files

- `main.tex` - paper source
- `snapshot.tex` - snapshot register (commit, header digest, verification
  value, date)
- `references.bib` - bibliography
- `AUDIT.md` - claim and evidence register
- `tools/reference_check.c` - known-answer and direct-interface verification
  check for the C reference
- `results/` - raw outputs and provenance used by the evaluation section
