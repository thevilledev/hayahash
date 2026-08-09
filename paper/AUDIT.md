# Claim and evidence register

This register belongs to the working paper at release `v0.5.0`. Its header
is the one introduced by commit `e5e54840bc2a24c6af796e5d1537fdf0f4f270e7`,
which is the identity `paper/snapshot.tex` records: a release commit cannot
name its own hash, and the header is what matters. The authoritative
snapshot identity lives in `paper/snapshot.tex`; `make -C paper
check-snapshot` verifies that this file, the checkout, and the header digest
agree.

What identifies the audited artifact is the header digest, not the commit. A
commit that leaves `hayahash.h` byte-identical carries every result here
forward. The `v0.5` release changed every digest value: the length term
moved from the initial-state premix to the finalizer. Every evaluation
record produced for an earlier digest is therefore historical; the ones this
register cites for the current snapshot were produced at the `v0.5` digest.

The register covers the paper's claims about the current snapshot. It does
not audit historical project artifacts; the few historical facts the paper
mentions are collected under "Historical notes" below.

Evidence states:

- **reproduced** - rerun from this worktree while preparing the draft;
- **archived** - raw output and method are stored under `paper/results/`;
- **code-derived** - follows directly from the named implementation;
- **reported** - stated by the project but not independently reproducible from
  the current worktree; and
- **open** - evidence is missing; the paper's open-work list carries it.

| Claim | State | Evidence | Update rule |
|---|---|---|---|
| The input and seed map to deterministic 64- and 128-bit results with canonical little-endian reads, and `hayahash128.lo == hayahash64` for every input and seed. | code-derived, reproduced | `hayahash.h`; every vector of `paper/tools/reference_check.c` asserts the width invariant | Re-audit after any loader, API, or endian change. |
| The short path covers 0 through 16 bytes with two multiplied terms and the `moremur` finalizer; the mid path uses four lanes for 17 through 319 bytes; the bulk path uses eight lanes from 320 bytes. | code-derived | `hayahash.h`, length dispatch and `hayahash64_internal_bulk_min` | The threshold is part of the digest and must not be treated as a tuning-only value. |
| The absorb chain `t_i = w_i + rotl(w_{i-1},27)` is bijective on stripe sequences; the tail injections and all three finalizers are bijections; equal-state inputs of different lengths produce different digests in both widths; the short 128-bit path is injective per length and seed; the streaming interface reproduces the one-shot digests. | code-derived | Proofs in the paper (Section "Structural properties"); streaming equivalence additionally checked by `tests/hash128.c`, rerun for this draft | These are structural statements, not collision-resistance claims for the complete hash. Recheck each proof if rotation counts, constants, load rules, or buffer constants change. |
| The known-answer vectors match the C reference for both widths. | reproduced | `make -C paper check-reference`; vectors regenerated from the header at this snapshot | Regenerate vectors intentionally whenever the digest changes. |
| The direct-call verification procedure returns `0x65F2AC15` for hayahash64 and `0x3F0411F4` for hayahash128. | reproduced | `paper/tools/reference_check.c`, which reimplements SMHasher3's procedure over the direct interface; the language-port suites assert the 64-bit value | Recheck on every digest change; the values change with the digest. |
| The SMHasher3 translation registers verification values `0x65F2AC15`/`0x805DE5C0` (64, LE/BE) and `0x3F0411F4`/`0x46140A64` (128, LE/BE). | code-derived | `tests/smhasher3/hayahash.cpp` registration blocks; the LE values independently reproduced from the header as above | The BE values are registered and exercised by SMHasher3's own self-test in any suite run; they were not independently recomputed from the header for this draft. |
| hayahash128 passes SMHasher3's full default suite, 188 of 188, at pinned upstream commit `51d3cd1a`. | archived | `paper/results/epyc9655-smhasher3-128-conformance.txt`: EPYC 9655 KVM guest, GCC 13.3.0, raw-output checksum retained; the record also runs eight other 128-bit functions and audits each one's instruction class from source | Rerun on every digest change and whenever the pin advances. VM timings in that record are deliberately excluded from any speed claim. |
| Both hayahash widths pass the full default suite on an Apple M1 Pro at the `v0.5` digest; the 128-bit run took 839.5 seconds. | reported | `docs/quality.md`; no raw full-suite dump for that host is archived under `paper/results/` | Promote by archiving the raw output with provenance, or drop on the next digest change. The multi-host, multi-compiler sweep that release `v0.4.0` had is open work for the current digest. |
| The local quality harness passes at this snapshot, with worst input-bit bias 0.0360 (length 129), worst seed-bit bias 0.0330 (length 4), and 24 clean collision sets; the streaming differential passes in the same run. | reproduced, archived | `paper/results/quality.txt`, drafting host, clang 18.1.3; the harness generator is fixed-seeded, so the numbers depend on the digest, not the host | Rerun after every digest change and record compiler and snapshot. |
| The local rotation-orbit control finds 512 colliding ChibiHash v2 pairs while its other 23 sets stay clean. | archived | `./tests/quality v2`; `paper/results/quality-chibihash-v2.txt` (2026-07-31 record; it measures vendored ChibiHash v2, which did not change in `v0.5`) | Keep this result scoped to the constructed set; it is not a general quality ranking. |
| The M1 Pro and Zen 5 benchmark records back the paper's performance summary; hayahash64 leads ChibiHash v2 in every cell of both records. | archived | `paper/results/apple-m1-pro-hayahash128.txt`, `paper/results/ryzen-ai9-hayahash128.txt`; the every-cell statement was verified mechanically over both files while preparing this draft | The paper carries no benchmark tables; its performance placeholder states the completion criteria for restoring them. The Zen 5 record's header checksum matches the audited header exactly. The M1 Pro record documents a late sixth-pass worktree header whose difference from the release is the documented output-identical dispatch follow-up; retake that record at the released header to remove the caveat. The EPYC dual-width record (`epyc9655-hayahash128.txt`) is a VM: within-host ratios only. |
| The WebAssembly comparison backs the paper's wasm statement: rapidhash sustains 6.43 GB/s where hayahash64 sustains 23.55 at 1 MiB in the same runtime. | archived | `paper/results/apple-m1-pro-wasm-hayahash128.txt`: baseline wasm32 via `zig cc`, Node 26, timing loops inside wasm | Within-run comparison only. The record's header (`1c2c107f...`) is the `v0.5`-digest header before the output-identical sixth pass; digest identity across that range is asserted by the unchanged port vectors, which were rerun for this draft. |
| Rust, Go, and Python pass their suites against this snapshot on the drafting host; the C reference checks pass. | reproduced, archived | `paper/results/conformance.txt` | Zig, Java, C#, Swift, JavaScript, MIPS64, and the native/wasm comparison rest on repository CI (nightly differential corpus, s390x big-endian KAT, MSVC x64, qemu-mips64el) and were not rerun in this environment. |

## Historical notes

These facts concern earlier digests or repository documentation, not the
construction specified by the paper.

- Release `v0.4.0` (previous digest, verification value `0xF3C4A9B4`) passed
  SMHasher3's default suite 188 of 188 on nine builds spanning four hosts,
  two architectures, five compilers, and every dispatch shape the header
  compiled, with all 2,187 non-timing output lines identical, including a
  build whose bulk loop was auto-vectorized. The records remain under
  `paper/results/` (`*-smhasher3*.txt` at pin `6ab43433`). They demonstrate
  the verification pipeline and the exactness of dispatch-shape claims for
  that digest; they say nothing about the current digest.
- Release `v0.2.1` was reported to pass 188 of 188 with verification value
  `0x6B558D9D`. No adapter or raw run for it exists in this repository, and
  the digest has changed twice since; the value is superseded.
- The `v0.4`-era paper carried the claim that ChibiHash v2 led hayahash64 at
  320 and 512 bytes on Zen 5. The current records post-date the fifth and
  sixth optimization passes and do not reproduce that inversion; the claim
  is retired rather than carried forward.

## Deliberate exclusions

The paper does not claim cryptographic security, collision resistance
against an adversary, universality, a proof for the complete construction,
or a universal speed record. The Speed and Hashmap sections inside archived
SMHasher3 runs are the tool's own timings and are not used for any
performance claim in the paper.

The native SMHasher3 competitive sweeps under
`paper/results/*-smhasher3-shootout.txt` and the 128-bit sweep records back
the tables in the top-level `README.md`; the paper makes no
competitor-speed claim from them. The paper's only cross-function speed
statements are within-run comparisons from two archived records: the
WebAssembly table, and ChibiHash v2 as the same-executable baseline of the
native benchmark records.
