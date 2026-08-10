# Differential conformance corpus

`generate.c` creates randomized inputs, random 64-bit hash seeds, and the
expected 64- and 128-bit digests produced by the C reference in
`hayahash.h`. The Rust, Go, Zig, Java, C#, Python, Swift, and JavaScript
test suites read that same
binary corpus when `HAYAHASH_CORPUS` points to it. JavaScript checks both
its WebAssembly and pure-JS engines.

The `HAYAFZ02` corpus stores the 128-bit result as little-endian `lo`, then
`hi`; the low word is also checked against every port's 64-bit API. The first
385 cases exhaustively cover input lengths 0 through 384, including
every short, tail, mid-loop, tier, and 320-byte bulk dispatch boundary. Larger
fixed edges and boundary-biased random lengths are followed by broad random
lengths around the 128 KiB edge. All input bytes and per-hash seeds are
randomized. The default case count is 32768 (minimum 406): the first 406
cases are the fixed length/edge prefix, and the rest are i.i.d. random
draws. For a latent port bug that fails a random draw with probability
`p`, the per-run miss chance is about `e^(-n p)` with
`n = case_count - 406`, so the larger default strengthens detection of
rare content- or seed-dependent divergences that published KATs do not
sample.

Generate and replay a corpus from the repository root:

```sh
cc -O2 -std=c11 -Wall -Wextra -Werror \
  tests/differential/generate.c -o /tmp/hayahash-generate
/tmp/hayahash-generate /tmp/hayahash-corpus.bin 0x0123456789abcdef 32768

HAYAHASH_CORPUS=/tmp/hayahash-corpus.bin \
  cargo test --manifest-path rust/Cargo.toml --test differential -- --nocapture
HAYAHASH_CORPUS=/tmp/hayahash-corpus.bin go test -C go -run Differential -v
(cd zig && HAYAHASH_CORPUS=/tmp/hayahash-corpus.bin \
  zig build test --summary all)
HAYAHASH_CORPUS=/tmp/hayahash-corpus.bin \
  mvn --batch-mode --no-transfer-progress -f java/pom.xml -Dtest=DifferentialTest test
HAYAHASH_CORPUS=/tmp/hayahash-corpus.bin \
  dotnet test csharp/Hayahash.sln --filter RandomizedCReferenceCorpus --verbosity minimal
(cd python && python -m pip install -e ".[test]" >/dev/null)
HAYAHASH_CORPUS=/tmp/hayahash-corpus.bin \
  pytest -q python/tests/test_differential.py
HAYAHASH_CORPUS=/tmp/hayahash-corpus.bin \
  (cd swift && swift test --filter DifferentialTests)
npm --prefix js run build
HAYAHASH_CORPUS=/tmp/hayahash-corpus.bin \
  node --test js/test/differential.test.mjs
```

The nightly workflow prints its corpus PRNG seed and accepts the same seed as
a manual-dispatch input. A failing run uploads its corpus as an artifact, so
either the seed or the exact bytes can be used for local replay.
