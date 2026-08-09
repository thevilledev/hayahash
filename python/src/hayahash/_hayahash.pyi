"""Type stub for the hayahash C extension."""

def hayahash64(
    data: bytes | bytearray | memoryview,
    seed: int = 0,
) -> int:
    """Hash bytes-like data with an optional 64-bit seed.

    Returns the unsigned 64-bit digest as a Python int. Bit-exact with
    the C reference hayahash64().
    """

def hayahash128(
    data: bytes | bytearray | memoryview,
    seed: int = 0,
) -> tuple[int, int]:
    """Hash bytes-like data and return ``(lo, hi)``.

    The low word is exactly :func:`hayahash64` for the same input and seed.
    """
