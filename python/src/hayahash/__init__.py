"""hayahash64: small, fast, portable 64-bit non-cryptographic hash."""

from __future__ import annotations

from importlib.metadata import PackageNotFoundError, version

from hayahash._hayahash import hayahash64

try:
    __version__ = version("hayahash")
except PackageNotFoundError:  # pragma: no cover - editable/uninstalled tree
    __version__ = "0.4.5"

__all__ = ["__version__", "hayahash64"]
