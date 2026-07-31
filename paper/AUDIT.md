# Claim and evidence register

This register belongs to the working paper at source snapshot
`cf9690f67100225feb30fdb1cce7c5a2200ea32d`. The authoritative snapshot
identity lives in `paper/snapshot.tex`; `make -C paper check-snapshot`
verifies that this file, the checkout, and the header digest agree.

The register covers the paper's claims about the current snapshot. It does
not audit historical project artifacts; the few historical facts the paper
mentions are collected under "Historical notes" below.

Evidence states:

- **reproduced** - rerun from this worktree while preparing the draft;
- **archived** - raw output and method are stored under `paper/results/`;
- **code-derived** - follows directly from the named implementation;
- **reported** - stated by the project but not independently reproducible from
  the current worktree; and
- **open** - evidence is missing; the paper carries a TBD section for it.

| Claim | State | Evidence | Update rule |
|---|---|---|---|
| The input and seed map to a deterministic 64-bit result with canonical little-endian reads. | code-derived | `hayahash.h`, input loaders and `hayahash64` | Re-audit after any loader, API, or endian change. |
| The short path covers 0 through 16 bytes and uses two multiplied terms plus the `moremur` finalizer. | code-derived | `hayahash.h`, `hayahash64`, first length branch | Re-audit after any digest change. |
| The mid path uses four lanes and the bulk path uses eight lanes from 320 bytes. | code-derived | `hayahash.h`, `hayahash64_internal_bulk_min`, mid and bulk loops | The threshold is part of the digest and must not be treated as a tuning-only value. |
| The transformed word sequence `w_i + rotl(w_{i-1},27)` is injective for a fixed initial previous word. | code-derived | First-difference argument in `hayahash.h`; proof restated in the paper | This is not a collision-resistance proof for the complete hash. |
| The tail injection maps are bijections on 64-bit words. | code-derived | `hayahash64_internal_inj` and `inj2`; polynomial argument in the paper | Recheck if the word width or rotation counts change. |
| The known-answer vectors match the C reference. | reproduced | `make -C paper check-reference` | Update vectors intentionally whenever the digest changes. |
| The direct-call verification procedure returns `0xF3C4A9B4`. | reproduced | `paper/tools/reference_check.c`; language-port test files | This is a self-test value, not an SMHasher3-registered value; see the paper's SMHasher3 section. |
| The current snapshot passes SMHasher3. | open | None; the paper's SMHasher3 section is a TBD placeholder | Fill in only when the adapter, the exact SMHasher3 commit, the complete raw output, the host description, and the verification value are committed together. |
| The local quality harness passes, with worst observed input bias 0.0403, seed bias 0.0338, and 24 clean collision sets. | reproduced, archived | `make -C tests run-quality`; `paper/results/quality.txt` | Rerun after every digest change and record compiler and snapshot. |
| The local rotation-orbit control produces 512 colliding ChibiHash v2 pairs. | reproduced, archived | `./tests/quality v2`; `paper/results/quality-chibihash-v2.txt` | Keep this result scoped to the constructed set; it is not a general quality ranking. |
| The Apple M1 ChibiHash comparison uses the median of three idle process runs, each using nine calibrated samples per cell. | reproduced, archived | `tests/bench.c`; `paper/results/apple-m1-chibihash.txt` | Retain all nine raw samples in a future harness revision; the current harness retains one median per process and cell. |
| The Zen 5 ChibiHash comparison used GCC 16 on a core pinned near 5.16 GHz. | archived | `OPTIMIZATION_NOTES.md`; `paper/results/zen5-chibihash.txt` | Add an environment manifest and raw sample values before release. |
| Compiler-specific dispatch changes preserve output. | reported, partial | `OPTIMIZATION_NOTES.md` and `.optwork/exact.c`; current KATs cover boundaries but not the historical A/B candidates | Archive the A/B source pair and exact-check output for each optimization claim that remains in the paper. |
| Rust, Go, Zig, Java, WebAssembly, and pure JavaScript implementations pass their checked-in conformance vectors at this snapshot. | reproduced, archived | `paper/results/conformance.txt`; current tests in each port | A single generated machine-readable vector file is still needed. The present suites share copied tables. |

## Historical notes

These facts concern earlier digests or repository documentation, not the
construction specified by the paper.

- Release `v0.2.1` was reported to pass 188 of 188 SMHasher3 tests with
  verification value `0x6B558D9D`; the claim first appears no later than
  commit `565c949`. The adapter and raw run are not in this repository, and
  the digest has changed since `v0.2.1`, so the result does not transfer to
  the current snapshot.
- `README.md` and `js/README.md` still show the `v0.2.1` verification value.
  The current direct-interface self-test value is `0xF3C4A9B4`. Correct the
  package documentation separately; the paper does not rely on it.

## Deliberate exclusions

The paper does not claim cryptographic security, collision resistance
against an adversary, universality, a proof for the complete construction, or
a universal speed record. It also omits the broad SMHasher3 speed shootout
until the raw output and exact competitor revisions are archived.
