# Hayahash paper

The working paper for the current snapshot of Hayahash. It is a
specification and a structural analysis: it states both digest widths and
the streaming interface exactly, proves the properties the construction
relies on, and derives the bulk loop's cost regime from its dependency
graph. It carries no benchmark tables; measured throughput lives in
[`../docs/benchmarks.md`](../docs/benchmarks.md).

The draft is tied to a source revision. The audited commit, header digest,
verification values, and date are defined once, in `snapshot.tex`; the LaTeX
source and the Makefile checks both read from it. What identifies the
audited artifact is the header's SHA-256, not the commit: any commit that
leaves `hayahash.h` byte-identical carries every result forward.

## Build

The preferred build uses [Tectonic](https://tectonic-typesetting.github.io/):

```sh
make -C paper
```

The PDF is written to `output/pdf/hayahash-paper.pdf`.

## Check

```sh
make -C paper check
```

This does three things:

1. compiles and runs `tools/reference_check.c` against `hayahash.h`;
2. runs the local quality harness; and
3. confirms that the snapshot named in `snapshot.tex` is an ancestor of the
   checkout, that `hayahash.h` still has the audited digest, and that
   `AUDIT.md` references the same snapshot.

The SMHasher3 run is not part of `make check`, because it clones and builds
an external tool and takes minutes per host. Reproduce it separately:

```sh
make -C tests/smhasher3 run
```

That clones SMHasher3 at the pinned upstream commit, installs the
self-contained translation, builds, and runs the default suite. See
[`../docs/smhasher3.md`](../docs/smhasher3.md) for the full procedure,
including how to re-derive the verification values after a digest change.

When running a benchmark directly, use `make -B` or compile to a new output
path. The test Makefile does not know that a changed `CC`, `CFLAGS`, or
`ARCH` value should invalidate an existing binary.

## Updating the snapshot

When the algorithm or the header changes, update everything as one
reviewable change:

1. set the new commit, header SHA-256, verification values, and date in
   `snapshot.tex`;
2. regenerate the known-answer vectors and the expected verification values
   in `tools/reference_check.c`;
3. re-verify every proof in "Structural properties" whose constants moved -
   rotation amounts, multipliers, block sizes, thresholds, and buffer
   constants all appear in proof arguments;
4. rerun `make -C paper check` and refresh the records under `results/`,
   deleting the ones the change supersedes;
5. update the claim register in `AUDIT.md`.

## Files

- `main.tex` - paper source
- `snapshot.tex` - snapshot register (commit, header digest, verification
  values, date)
- `references.bib` - bibliography
- `AUDIT.md` - claim and evidence register
- `tools/reference_check.c` - known-answer and direct-interface verification
  check for the C reference
- `results/` - raw outputs and provenance for the current digest
