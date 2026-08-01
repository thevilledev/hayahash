# Running SMHasher3

The harness lives in [`tests/smhasher3/`](../tests/smhasher3/). It clones
SMHasher3 at a pinned upstream commit, drops in an adapter that `#include`s
`hayahash.h` directly, builds, and runs. Nothing is vendored: the clone is
fetched at test time and is gitignored.

There are two separate jobs, and they have different standards of care:

- **Conformance** - does the current digest pass the suite? Fast to get right,
  deterministic, and the result is the same on every host.
- **Speed comparison** - the shootout table in `README.md`. Much easier to get
  wrong; read [Measuring speed](#measuring-speed) before touching those numbers.

## Quick start

```bash
make -C tests/smhasher3 run
```

That is the whole conformance run for the current header. First invocation
clones and builds (about 30 s on an M1); the suite itself takes roughly 4 min
on a Zen 5 core and 10 min on an M1.

Targets:

| target | what it does |
|---|---|
| `build` | clone if needed, apply patches, install the adapter, configure, compile |
| `verify` | print the verification values SMHasher3 computes for hayahash |
| `run` | the full default suite (188 tests for a 64-bit hash) |
| `clean` | drop the build directory, keep the clone |
| `distclean` | drop the clone entirely |

Useful variables:

```bash
make -C tests/smhasher3 run CXX=clang++ BUILD=build-clang   # separate build tree
make -C tests/smhasher3 run HASH=ChibiHash2                 # some other hash
```

Always pair a different `CXX` with a different `BUILD`, or CMake reuses the
cached compiler from the existing tree and silently ignores you.

## Covering both dispatch shapes

`hayahash.h` compiles two different programs. `HAYAHASH64_INTERNAL_TIERS` is 1
on AArch64 and on x86-64 under GCC, and 0 on x86-64 under Clang, which
disables the straight-line length tiers. They are specified to produce
identical output, so a conformance run should cover both:

```bash
make -C tests/smhasher3 run                              # gcc on x86-64 -> tiers 1
make -C tests/smhasher3 run CXX=clang++ BUILD=build-clang # clang on x86-64 -> tiers 0
```

To confirm which shape you actually built:

```bash
printf '#include <stdio.h>\n#include "hayahash.h"\nint main(void){printf("TIERS=%%d\\n", HAYAHASH64_INTERNAL_TIERS);}\n' > /tmp/t.c
cc -I. -o /tmp/t /tmp/t.c && /tmp/t
```

The two shapes should agree on every line of output that does not report a
time. That is a much stronger exactness check than comparing boundary vectors,
because the suite hashes millions of keys:

```bash
strip() { grep -vE "cycles/hash|cycles/op|bytes/cycle|GiB/sec|MiB/sec|Testing took|ghz" "$1"; }
diff <(strip run-gcc.txt) <(strip run-clang.txt) && echo "identical"
```

## After the digest changes

The registered verification values in `tests/smhasher3/hayahash.cpp` are tied
to the digest, so a digest change makes the build fail its own self-test. To
re-derive them:

1. Set both `$.verification_LE` and `$.verification_BE` to `0x00000000`.
2. `make -C tests/smhasher3 verify`. The values print as
   `SKIP (unverifiable)`; `CE` is the little-endian one, `NE` the byte-swapped.
3. Write them back into the registration and re-run `verify`. Both must now
   say `PASS`.

`CE` should equal the value `paper/tools/reference_check.c` prints, because
that self-test reimplements SMHasher3's verification procedure over the direct
interface. If they disagree, one of the two is wrong - investigate before
recording anything.

## Measuring speed

Read this before regenerating the shootout table. Three traps, all of which
produced wrong numbers in this repository at some point.

### The small-key column is latency, not throughput

`timehash_small()` feeds each hash's output back into the next key, so hashes
cannot overlap. It measures dependent latency over 1-31-byte keys. It is *not*
comparable to the independent-hash figures from `tests/bench.c`, and reading it
as throughput will look like the two harnesses contradict each other.

### The overhead calibration is noisy and must be corrected

SMHasher3 measures the cost of calling a do-nothing hash **once per process**
and subtracts it from every small-key timing. That single calibration varied
1.55-3.62 cycles across 50 runs on Zen 5. Because it is subtracted from all 31
lengths alike, a bad calibration shifts an entire run by a constant, which on a
host where hashes cost 6-18 cycles is a 10-40% error. It once produced a 4-byte
rapidhash reading of 1.59 cycles/hash, below the latency of a single multiply.

So: always pass `--verbose`, which prints `Short Overhead`. Add each run's own
overhead back to recover the raw measurement, then subtract one common baseline
for every hash on that host. Use the smallest calibration observed on the host,
since interference can only inflate the do-nothing hash's cost:

```
corrected = reported + that_run_overhead - min_overhead_on_host
```

After this correction replicates agree to within about 0.1 cycles, against
±2 before. Bulk figures need no correction; they use a long-hash overhead that
is negligible against a 262144-byte hash.

### Competitors must be built at full width

Upstream at the pinned commit does not enable NEON on Apple Silicon: its
`platform/family.cmake` matches `arm` and `aarch64`, but macOS reports `arm64`,
so no hash gets NEON and SIMD competitors are silently understated. Patch 0002
fixes this. Check before trusting any comparison:

```bash
cd tests/smhasher3/smhasher3/build && ./SMHasher3 --list | grep -E "XXH3-64|gxhash"
```

`XXH3-64` should report `neon` on ARM and `avx512`/`avx2` on a modern x86-64,
never `scalar`. `gxhash` only takes its hardware path under x86 AES, so its ARM
number describes a software-AES fallback rather than gxhash; measure it on
x86-64 only.

### Sweep procedure

Run replicates **round-robin** across hashes rather than N-in-a-row, so thermal
drift hits every hash equally instead of penalising whichever ran while the
machine was hot. Take the median. Keep the machine otherwise idle - a replicate
taken while the same box was doing `scp`/`ssh`/parsing carried the only large
outliers in an earlier sweep and had to be discarded.

```bash
B=tests/smhasher3/smhasher3/build
HASHES="rapidhash wyhash a5hash komihash XXH3-64 HayaHash64 ChibiHash2 mx3.v3 SpookyHash2-64"
for rep in 1 2 3 4 5; do
  for h in $HASHES; do
    (cd $B && ./SMHasher3 --test=Speed --verbose "$h") > "speed-$h-rep$rep.txt" 2>&1
  done
done
```

Budget about 45 s per hash per replicate on Zen 5 and 90 s on an M1. Report
`Average - N cycles/hash` for small keys and the first `Average - N
bytes/cycle` for bulk.

## Pass/fail for competitors

The suite's statistical tests are deterministic given the hash, so one host
settles pass/fail for all of them. Do not copy failure counts from upstream's
published tables: those come from a different invocation and disagree with a
default run at this pin.

```bash
for h in $HASHES; do
  (cd $B && ./SMHasher3 "$h") > "full-$h.txt" 2>&1
  echo "$h: $(grep 'Overall result' full-$h.txt)"
done
```

## Gotchas

- `--test=VerifyAll` **ignores the hashname argument** and walks every
  registered hash, and exits non-zero if any of them fails its self-test. That
  says nothing about hayahash. Filter its output; the `verify` target does.
- The tool reports its version as `-dirty` because the harness adds the adapter
  and applies patches. That is expected, and each archived record says so.
- `make -B` does not help here; the Makefile drives CMake, so use `clean` or a
  separate `BUILD` tree.

## Upstream patches

`tests/smhasher3/` carries two patches on top of the pin, applied on every host
so all runs build identical source:

| patch | fixes | upstream |
|---|---|---|
| `0001-macos-size_t-reference-binding.patch` | `util/Random.cpp` does not compile under Clang on macOS | [!31](https://gitlab.com/fwojcik/smhasher3/-/merge_requests/31) |
| `0002-cmake-detect-apple-silicon-as-arm.patch` | Apple Silicon not recognised as the Arm family, disabling NEON | [!32](https://gitlab.com/fwojcik/smhasher3/-/merge_requests/32) |

Both were open as of 2026-08-01. **When one merges**, advance
`SMHASHER3_COMMIT` in `tests/smhasher3/Makefile` past it and delete the
corresponding entry from `PATCHES` and the file itself. Once both are gone the
harness builds from unmodified upstream, and the licensing note below shrinks
to just the adapter.

Advancing the pin for any other reason means re-running everything: the archived
records name the exact commit they were produced against.

## Licensing

The adapter and the patches are GPL-3.0-or-later, not Unlicense, because they
are written against SMHasher3's headers and macros or modify its sources
directly. See [`tests/smhasher3/COPYING`](../tests/smhasher3/COPYING) and the
exception note in [`LICENSE`](../LICENSE). This does not affect `hayahash.h`,
which stays public domain; the adapter merely includes it. SMHasher3 is not
part of any release artifact, but a redistributed combined executable would
carry the GPL.

## Archiving a run

Records live in [`paper/results/`](../paper/results/) and back claims in the
paper and `README.md`, so they carry a provenance header before the raw output:
the snapshot commit and `hayahash.h` digest, the SMHasher3 upstream URL and
commit, the applied modifications, host and compiler, the exact command,
thread count, and date. Follow an existing file, and state the method's limits
in the record rather than only in the prose that cites it.

If a recorded digest later drifts because a file gained a license header or
comment, note both digests in the record instead of silently rewriting it - the
digest that produced a run is part of that run's evidence.
