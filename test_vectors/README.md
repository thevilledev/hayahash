# Test vectors

Language-agnostic known-answer tests (KATs) for the hayahash digests.
External implementers and release auditors should treat these files as the
public conformance artifact; per-language test tables are expected to match
them.

## Current digest

| File | Digest series | Release |
|---|---|---|
| [`v0.5.0.txt`](v0.5.0.txt) | current: length absorbed in the finalizer, streaming-compatible | 0.5.0 |

When a future release changes digests, add a new `vX.Y.Z.txt` and leave
older files in place so historical digests remain reproducible.

## File format

Lines beginning with `#` are comments. Blank lines separate sections.

### Pattern-A table

```text
len seed_hex h64_hex h128_hi_hex
```

Input bytes use the shared portable formula also used by the language-port
KAT suites:

```text
byte(i) = (i * 0x9E3779B97F4A7C15 + 0x2545F4914F6CDD1D) >> 56
```

`h128.lo` is omitted because it must equal `h64` for the same input and seed.

### Named vectors

```text
name len seed_hex h64_hex h128_hi_hex input_hex
```

`input_hex` is `-` for the empty input. These cover literal strings and the
`byte(i) = i` boundary buffer used by `tests/hash128.c`.

### Pattern-B table

Same columns as Pattern-A. Input is the 32-bit xorshift fill used by the
port `hash128` boundary tests (`seed = 0x1234`).

### Streaming samples

```text
len split_pattern h64_hex
```

`bytewise` means the Pattern-A prefix was absorbed one byte at a time and
must match the one-shot digest.

## Verify against the C reference

```sh
make -C test_vectors check
```

This rebuilds digests from [`hayahash.h`](../hayahash.h) and diffs them
against `v0.5.0.txt`. CI runs the same target from the C job matrix.

## Regenerating after a digest change

1. Update `hayahash.h` and every port together.
2. Edit `test_vectors/generate.c` only if lengths, seeds, or sections change.
3. Run `make -C test_vectors regenerate` and commit the new `vX.Y.Z.txt`.
4. Record the digest break in [`CHANGELOG.md`](../CHANGELOG.md) with a
   `DIGEST` marker.
