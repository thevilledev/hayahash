# Contributing to hayahash

Thanks for helping. hayahash is a multi-language hash library with one
authoritative reference and many bit-exact ports. The rules below keep
digests and ports aligned.

## Authoritative source

- [`hayahash.h`](hayahash.h) is the reference implementation and the
  design commentary of record.
- Every language port must be bit-exact against that header for the same
  `(input, seed)`.
- Dispatch shapes (length tiers, unrolling, auto-vectorization) may differ
  by compiler or CPU, but outputs must not.

## Before you open a PR

1. Say whether the change is digest-affecting. If it is, call that out in
   the PR title/body (`DIGEST` / breaking) and update
   [`CHANGELOG.md`](CHANGELOG.md) under `[Unreleased]`. Pre-1.0 digests may
   still change; see [`docs/stability.md`](docs/stability.md).
2. Run the relevant local checks (below).
3. Keep ports in lockstep when the public digest or API changes: C
   reference, every maintained port, SMHasher3 adapter, website simulator,
   known-answer tables, and [`test_vectors/`](test_vectors/).

## Local checks

| Change touches | Minimum local gate |
|---|---|
| `hayahash.h` | `make -C tests run-quality` |
| Streaming / 128-bit | `make -C tests run-hash128` |
| Published KATs | `make -C test_vectors check` |
| `cli/` | `make -C cli check` |
| A language port | that port's unit/KAT + differential tests |
| Docs only | render/link sanity; no algorithm edits |

Useful commands:

```sh
make -C tests run-quality
make -C test_vectors check
make -C cli check
make -C rust test          # or: cd rust && cargo test
cd go && go test ./...
# see docs/ports.md for Java, C#, Python, Swift, Zig, JS, MIPS
```

Nightly differential conformance (`tests/differential/`, workflow
`differential.yml`) is the cross-port fuzz gate. Reproduce a failure with
the logged PRNG seed from that workflow.

`hayasum` is fuzzed separately, since argv, its chunked reader, and the
file names it echoes are its own attack surface rather than the hash's:
`make -C cli fuzz-run` locally, one minute per target on every pull
request, longer nightly runs in `fuzz.yml`. See
[`cli/fuzz/README.md`](cli/fuzz/README.md). A digest change has to
update the known answers in `cli/tests/run.sh` alongside
[`test_vectors/`](test_vectors/).

## When to re-run SMHasher3

Re-run the full suite (both widths, every documented dispatch shape) when
a change can alter digests or the set of dispatch shapes:

- absorb, finalizer, constants, bulk threshold, or length handling
- new compiler/CPU defines that change which code path is compiled

See [`docs/smhasher3.md`](docs/smhasher3.md). Pure docs, packaging, and
bit-identical refactors do not require a full suite rerun.

## Adding or updating a port

1. Implement both widths and streaming where the language can express them.
2. Preserve `hayahash128.lo == hayahash64` for the same input and seed.
3. Import the shared KAT lengths/seeds used by existing ports (see
   `rust/tests/kat.rs` and [`test_vectors/`](test_vectors/)).
4. Add the port to CI and, when practical, to the differential corpus
   consumers.
5. Keep the lockstep version in the port manifest; use
   [`scripts/bump-version.sh`](scripts/bump-version.sh) for releases.

## Release versioning

All ports share one SemVer. `scripts/bump-version.sh X.Y.Z` updates every
manifest (including root [`VERSION`](VERSION) for the C/pkg-config
package); the release workflow refuses to publish on disagreement. Digest
breaks should be called out clearly in [`CHANGELOG.md`](CHANGELOG.md).

## Security-sensitive reports

Do not open a public issue for vulnerabilities or silent cross-port
divergence that could be abused. Follow [`SECURITY.md`](SECURITY.md) or
use GitHub Security Advisories.

## Style

- Match the surrounding code and comments; prefer clarity over cleverness.
- C stays C99-friendly, freestanding-capable, and free of undefined
  behavior (CI runs ASan/UBSan on the reference paths).
- Do not vendor SMHasher3; the adapter clones a pinned upstream commit at
  test time.
