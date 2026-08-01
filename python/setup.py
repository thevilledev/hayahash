#!/usr/bin/env python3
"""Build the hayahash C extension against the reference header."""

from __future__ import annotations

import shutil
from pathlib import Path

from setuptools import Extension, setup

ROOT = Path(__file__).resolve().parent
REPO_HEADER = ROOT.parent / "hayahash.h"
LOCAL_INCLUDE = ROOT / "include"
LOCAL_HEADER = LOCAL_INCLUDE / "hayahash.h"


def header_include_dir() -> str:
    """Prefer the monorepo reference header; keep the sdist copy in sync."""
    LOCAL_INCLUDE.mkdir(parents=True, exist_ok=True)
    if REPO_HEADER.is_file():
        shutil.copyfile(REPO_HEADER, LOCAL_HEADER)
        return str(ROOT.parent)
    if not LOCAL_HEADER.is_file():
        raise SystemExit(
            "hayahash.h not found; expected repository root or python/include/"
        )
    return str(LOCAL_INCLUDE)


setup(
    ext_modules=[
        Extension(
            "hayahash._hayahash",
            sources=["native/_hayahash.c"],
            include_dirs=[header_include_dir()],
            language="c",
        )
    ]
)
