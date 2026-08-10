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

class Hasher:
    """Incremental hayahash state.

    The digest equals :func:`hayahash64` / :func:`hayahash128` over the
    concatenation of every :meth:`update`, for any split of that input.
    Not safe for concurrent use from multiple threads.
    """

    def __init__(self, seed: int = 0) -> None: ...
    @property
    def seed(self) -> int:
        """The 64-bit seed."""

    @property
    def length(self) -> int:
        """Number of bytes absorbed so far."""

    def update(self, data: bytes | bytearray | memoryview) -> None:
        """Absorb bytes-like data."""

    def digest64(self) -> int:
        """Return the 64-bit digest so far, without consuming the state."""

    def digest128(self) -> tuple[int, int]:
        """Return both digest words, without consuming the state."""

    def copy(self) -> Hasher:
        """Return an independent hasher with the same absorbed state."""

    def reset(self, seed: int | None = None) -> None:
        """Discard absorbed input, optionally reseeding."""
