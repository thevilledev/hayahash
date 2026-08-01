# hayahash for Python

CPython C extension over [`hayahash.h`](../hayahash.h): a small, fast,
portable 64-bit non-cryptographic hash for targets with ordinary
wrapping 64×64→64 multiply.

```python
from hayahash import hayahash64

h = hayahash64(b"hello world", 0)
h = hayahash64(data, seed=0x9E3779B97F4A7C15)
```

Requires CPython 3.9+. Package version tracks the shared algorithm
version across every language port in this repository.

PyPI releases ship manylinux/musllinux (and Windows/macOS) wheels built
with [cibuildwheel](https://cibuildwheel.pypa.io/), so `pip install`
does not need a local compiler. The sdist remains available as a
fallback and still builds the extension from source.

```sh
python -m pip install -e ".[test]"
pytest
```

The extension compiles the reference header directly (not a reimplementation).
When building from this monorepo, `../hayahash.h` is preferred; the copy
under `include/` is kept for sdists and checked against the root header in CI.
