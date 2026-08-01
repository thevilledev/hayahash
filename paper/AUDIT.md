# Claim and evidence register

This register belongs to the working paper at source snapshot
`2a4ef58e3d2e18b01ae67dd1ed4af207e3a79130`, a development snapshot after
release `v0.4.0`. The authoritative
snapshot identity lives in `paper/snapshot.tex`; `make -C paper
check-snapshot` verifies that this file, the checkout, and the header digest
agree.

What identifies the audited artifact is the header digest, not the commit. A
commit that leaves `hayahash.h` byte-identical carries every result here
forward. That applies to the whole range `cf9690f..v0.4.0`, over which the
digest never moved despite the header being rewritten repeatedly: the
intermediate commits are output-identical optimizations, new language ports,
documentation, and release commits. `v0.4.0`'s fifth optimization pass changed
83 lines of `hayahash.h` and rewrote its per-compiler dispatch, and the
verification value is still `0xF3C4A9B4`.

Since `v0.4.0` the header has moved again, from `d7c67c1e...` to
`ede45489...`, by two commits that narrow when the bulk loop is offered to
the vectorizer. Every condition they change is false on every host in the
evaluation set, so the compiled code is byte-identical before and after on
all of them. That was verified by comparing object code on the M1 under
Apple Clang, and on the Zen 5 and Zen 3 hosts under both GCC and Clang; the
remaining recorded builds target Zen 5, where the new conditions reduce to
the old ones by inspection. The archived runs therefore still describe the
current header. The commits do change code generation for AVX-512 targets
that are not Zen 4 or Zen 5, and no host in this set is one.

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
| The direct-call verification procedure returns `0xF3C4A9B4`. | reproduced, archived | `paper/tools/reference_check.c`; language-port test files; SMHasher3 reports the same value as its canonical-endian verification value in all three archived runs | The self-test reimplements SMHasher3's procedure over the direct interface. Their agreement was an assumption until the runs archived under `paper/results/` checked it; recheck on every digest change. |
| The current snapshot passes SMHasher3's default suite, 188 of 188, on nine builds spanning four hosts, two architectures, five compilers, and all three dispatch shapes the header compiles, with verification values `0xF3C4A9B4` (LE) and `0x01E3C68D` (BE). | reproduced, archived | `tests/smhasher3` adapter and Makefile at upstream commit `6ab43433`; `paper/results/apple-m1-smhasher3.txt`, `zen5-smhasher3-gcc.txt`, `zen5-smhasher3-gcc-novec.txt`, `zen5-smhasher3-clang.txt`, `epyc7b13-zen3-smhasher3.txt`, `epyc9655-turin-smhasher3.txt`. The last two are consolidated records rather than full raw dumps: they carry provenance, the verbatim sanity and summary sections, and a checksum of the normalized output matching the others, since all nine outputs are identical and further verbatim copies would add bulk without evidence | Rerun on every digest change; the verification values change with the digest. Also rerun when either dispatch condition moves, and re-derive which builds are needed rather than reusing this list: `v0.4.0` changed the tier condition from architecture-keyed to compiler-keyed, which silently flipped the Apple M1 from the wide shape to the compact one. |
| The local quality harness passes, with worst observed input bias 0.0403, seed bias 0.0338, and 24 clean collision sets. | reproduced, archived | `make -C tests run-quality`; `paper/results/quality.txt` | Rerun after every digest change and record compiler and snapshot. |
| The local rotation-orbit control produces 512 colliding ChibiHash v2 pairs. | reproduced, archived | `./tests/quality v2`; `paper/results/quality-chibihash-v2.txt` | Keep this result scoped to the constructed set; it is not a general quality ranking. |
| The Apple M1 ChibiHash comparison uses the median of three idle process runs, each using nine calibrated samples per cell. | reproduced, archived | `tests/bench.c`; `paper/results/apple-m1-chibihash.txt` | Retain all nine raw samples in a future harness revision; the current harness retains one median per process and cell. |
| The Zen 5 ChibiHash comparison used GCC 16 on a core pinned near 5.16 GHz. | archived | `docs/optimization/pass-3-zen5.md`; `paper/results/zen5-chibihash.txt` | Add an environment manifest and raw sample values before release. |
| Compiler-specific dispatch changes preserve output. | reproduced, archived | The nine SMHasher3 runs under `paper/results/` cover every shape the header compiles (`TIERS` 1 and 0, `VECGCC` 1 and 0) across four hosts, two architectures and five compilers, and agree on both verification values and on every one of 2187 non-timing output lines | The strongest case is the vectorized build: disassembly confirms it issues packed 64-bit multiplies (`vpmullq`) where the others are scalar, so the agreement is between genuinely different machine code, not two spellings the compiler collapsed. Does not cover the historical A/B candidates in the optimization log (`docs/optimization/`); archive those source pairs separately if the paper keeps citing them. |
| Rust, Go, Zig, Java, C#, Python, Swift, WebAssembly, and pure JavaScript implementations pass their checked-in conformance vectors at this snapshot. | reproduced, archived | `paper/results/conformance.txt`; current tests in each port | A single generated machine-readable vector file is still needed. The present suites share copied tables. |

## Historical notes

These facts concern earlier digests or repository documentation, not the
construction specified by the paper.

- Release `v0.2.1` was reported to pass 188 of 188 SMHasher3 tests with
  verification value `0x6B558D9D`; the claim first appears no later than
  commit `565c949`. No adapter or raw run for it exists in this repository,
  and the digest changed after `v0.2.1`, so the value is superseded. Every
  release since has independently reached 188 of 188 with verification value
  `0xF3C4A9B4`; the test count matching is a coincidence of the default
  suite's size, not evidence that the old run transfers.
- `README.md` and `js/README.md` showed the `v0.2.1` verification value
  `0x6B558D9D` as if it applied to current code until after `v0.4.0`, and
  `js/README.md` contradicted the test file it cites, which asserts
  `0xF3C4A9B4`. Both files have since been corrected to `0xF3C4A9B4`; the
  paper never relied on them.

## Deliberate exclusions

The paper does not claim cryptographic security, collision resistance
against an adversary, universality, a proof for the complete construction, or
a universal speed record. The Speed and Hashmap sections inside the archived
SMHasher3 runs are the tool's own timings and are not used for any
performance claim in the paper.

The broad SMHasher3 speed shootout now has archived raw output and pinned
competitor revisions, under `paper/results/*-smhasher3-shootout.txt`, and it
backs the table in the top-level `README.md`. The paper still makes no
competitor-speed claim from it. Promoting it would need the two records to
meet this paper's benchmark standard rather than the README's: SMHasher3's
own timing harness measures dependent latency for small keys, its
once-per-process overhead calibration required an explicit correction, and
each host ran a single compiler.
