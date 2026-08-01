"""Build the hayahash C extension against the reference header."""

from __future__ import annotations

import shutil
import sys
import sysconfig
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


def extra_compile_args() -> list[str]:
    """Match the C CI warning gates on both MSVC and gcc/clang."""
    if sys.platform == "win32":
        # Keep /W4 /WX for our sources. CPython headers are not clean under
        # /W4 with current MSVC (e.g. C4115 in pytime.h on 3.9), so mark the
        # interpreter include tree as external and silence those warnings.
        args = ["/W4", "/WX", "/external:W0"]
        py_include = sysconfig.get_path("include")
        if py_include:
            args.append(f"/external:I{py_include}")
        return args
    return ["-Wall", "-Wextra", "-Werror"]


setup(
    ext_modules=[
        Extension(
            "hayahash._hayahash",
            sources=["native/_hayahash.c"],
            include_dirs=[header_include_dir()],
            language="c",
            extra_compile_args=extra_compile_args(),
        )
    ]
)
