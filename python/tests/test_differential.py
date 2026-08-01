"""Nightly differential conformance against a randomized C-reference corpus."""

from __future__ import annotations

import os
import struct
import sys

from hayahash import hayahash64


def test_randomized_c_reference_corpus() -> None:
    path = os.environ.get("HAYAHASH_CORPUS")
    if not path:
        print(
            "HAYAHASH_CORPUS is unset; skipping nightly differential corpus",
            file=sys.stderr,
        )
        return

    data = open(path, "rb").read()
    cursor = 0

    def require_remaining(nbytes: int) -> None:
        nonlocal cursor
        if nbytes < 0 or len(data) - cursor < nbytes:
            raise AssertionError(f"truncated differential corpus at byte {cursor}")

    require_remaining(8)
    magic = data[cursor : cursor + 8]
    cursor += 8
    assert magic == b"HAYAFZ01"

    require_remaining(12)
    (case_count,) = struct.unpack_from("<I", data, cursor)
    cursor += 4
    (prng_seed,) = struct.unpack_from("<Q", data, cursor)
    cursor += 8

    for case_index in range(case_count):
        require_remaining(20)
        (length,) = struct.unpack_from("<I", data, cursor)
        cursor += 4
        (hash_seed,) = struct.unpack_from("<Q", data, cursor)
        cursor += 8
        (expected,) = struct.unpack_from("<Q", data, cursor)
        cursor += 8
        require_remaining(length)
        actual = hayahash64(memoryview(data)[cursor : cursor + length], hash_seed)
        cursor += length
        assert actual == expected, (
            f"case={case_index} len={length} "
            f"hash_seed=0x{hash_seed:016x} corpus_prng_seed=0x{prng_seed:016x}"
        )

    assert cursor == len(data), "trailing bytes in differential corpus"
    print(
        f"Python matched {case_count} C-reference cases "
        f"(corpus PRNG seed=0x{prng_seed:016x})",
        file=sys.stderr,
    )
