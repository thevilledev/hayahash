"""Small, fast, portable 64- and 128-bit non-cryptographic hashes."""

from __future__ import annotations

from importlib.metadata import PackageNotFoundError, version

from hayahash._hayahash import hayahash64, hayahash128

try:
    __version__ = version("hayahash")
except PackageNotFoundError:  # pragma: no cover - editable/uninstalled tree
    __version__ = "0.4.6"

__all__ = ["__version__", "hayahash64", "hayahash128"]
