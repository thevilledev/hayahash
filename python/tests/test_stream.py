"""Streaming conformance for the Hasher type.

Every split of an input must produce the one-shot digest, and
finalizing must not consume the state.
"""

from __future__ import annotations

import pytest

from hayahash import Hasher, hayahash64, hayahash128

K = 0x9E3779B97F4A7C15
MASK = (1 << 64) - 1


def pattern_a(n: int) -> bytes:
    """The shared portable input fill used by the KAT tables."""
    return bytes(((i * K + 0x2545F4914F6CDD1D) & MASK) >> 56 for i in range(n))


# Chunk sizes that straddle the 448-byte buffer, the 128-byte keep floor
# and the 64-byte block.
SPLITS = [1, 7, 64, 127, 448, 449, None]


def feed(h: Hasher, data: bytes, chunk: int | None) -> None:
    if chunk is None:
        h.update(data)
        return
    for i in range(0, len(data), chunk):
        h.update(data[i : i + chunk])


LENGTHS = [*range(0, 641), 895, 896, 897, 1023, 1024, 1025, 4096, 20000, 131073]


@pytest.mark.parametrize("seed", [0, K, 0xDEADBEEFCAFEBABE])
def test_streaming_matches_one_shot(seed: int) -> None:
    for n in LENGTHS:
        data = pattern_a(n)
        want64 = hayahash64(data, seed)
        want128 = hayahash128(data, seed)
        for chunk in SPLITS:
            h = Hasher(seed)
            feed(h, data, chunk)
            assert h.digest64() == want64, f"len={n} seed={seed:#x} chunk={chunk}"
            assert h.digest128() == want128, f"len={n} seed={seed:#x} chunk={chunk}"
            assert h.digest128()[0] == want64


def test_digest_is_non_destructive() -> None:
    total = 2000
    data = pattern_a(total)
    for cut in (0, 1, 63, 64, 447, 448, 449, 1000, total):
        h = Hasher(7)
        h.update(data[:cut])
        first = h.digest64()
        assert first == h.digest64(), f"repeated digest differs at cut={cut}"
        assert first == hayahash64(data[:cut], 7), f"cut={cut}"
        assert h.digest128()[0] == first
        # Continue absorbing from the same state.
        h.update(data[cut:])
        assert h.digest64() == hayahash64(data, 7), f"continued at cut={cut}"


def test_empty_and_zero_length_updates() -> None:
    h = Hasher()
    assert h.digest64() == hayahash64(b"", 0)
    h.update(b"")
    assert h.digest64() == hayahash64(b"", 0)
    assert h.length == 0

    data = pattern_a(500)
    h.update(data[:200])
    h.update(b"")
    h.update(data[200:])
    assert h.digest64() == hayahash64(data, 0)
    assert h.length == 500


def test_copy_is_independent() -> None:
    data = pattern_a(1000)
    h = Hasher(3)
    h.update(data[:400])
    c = h.copy()
    assert c.seed == h.seed
    assert c.length == h.length
    h.update(data[400:])
    c.update(data[400:600])
    assert h.digest64() == hayahash64(data, 3)
    assert c.digest64() == hayahash64(data[:600], 3)


def test_reset() -> None:
    data = pattern_a(1000)
    h = Hasher(0xABCD)
    h.update(data)
    h.reset()
    assert h.length == 0
    h.update(data[:10])
    assert h.digest64() == hayahash64(data[:10], 0xABCD)
    h.reset(1)
    assert h.seed == 1
    h.update(data[:10])
    assert h.digest64() == hayahash64(data[:10], 1)


def test_memoryview_and_bytearray() -> None:
    data = pattern_a(700)
    h = Hasher()
    h.update(bytearray(data[:300]))
    h.update(memoryview(data)[300:])
    assert h.digest64() == hayahash64(data, 0)


def test_rejects_non_buffer() -> None:
    h = Hasher()
    with pytest.raises(TypeError):
        h.update("not bytes")  # type: ignore[arg-type]


def test_rejects_negative_seed() -> None:
    with pytest.raises(OverflowError):
        Hasher(-1)


def test_repr() -> None:
    h = Hasher(7)
    h.update(b"abc")
    assert repr(h) == "<hayahash.Hasher seed=7 length=3>"


# Pins the "streaming equality samples" section of
# test_vectors/v0.5.0.txt, which until now no port consumed.
PUBLISHED_STREAMING = [
    (0, 0x68AC507CF298CA3F),
    (5, 0x37EE1F8B5A98B84B),
    (10, 0xE28B66FB1E4CB4EA),
    (15, 0x9A8920A57F119D6B),
    (20, 0xC311E14FF31FB2BF),
    (25, 0xC27FDE4AC86CCE54),
    (30, 0x16CC1E65CA2CB4F3),
    (35, 0x1C6522BDC246DA12),
    (40, 0xD110128D567CB9F8),
]


@pytest.mark.parametrize(("length", "want"), PUBLISHED_STREAMING)
def test_published_streaming_vectors(length: int, want: int) -> None:
    h = Hasher(0)
    for byte in pattern_a(length):
        h.update(bytes([byte]))
    assert h.digest64() == want
