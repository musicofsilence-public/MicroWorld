#!/usr/bin/env python3
"""Reject C++ sources that drift from the tracked clang-format policy."""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path


# The style file has no leading dot, so callers must pass it explicitly as
# --style=file:<path>; a bare --style=file would fall back to LLVM style and
# falsely flag every file. dry-run --Werror turns any drift into a non-zero
# status without rewriting files, which is what makes the check a usable gate.
SOURCE_SUFFIXES = {".h", ".hpp", ".cpp", ".cc", ".cxx"}

# clang-format emits diagnostics as "<path>:<line>:<col>: <severity>: ...".
# The path may contain a Windows drive-letter colon (e.g. C:\repo\...), so a
# naive split on the first ':' misreads "C" as the path. This pattern anchors
# on the trailing ":<digits>:<digits>:" tail that follows the real path.
DIAGNOSTIC_PATH_PATTERN = re.compile(r"^(.*?):\d+:\d+:\s")


def infer_repository_root() -> Path:
    """Locate the policy file relative to this script so the default root is unambiguous."""
    return Path(__file__).resolve().parent.parent


def parse_arguments() -> argparse.Namespace:
    """Define the scan root and optional clang-format override for portable invocation."""
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--root",
        type=Path,
        default=infer_repository_root(),
        help="Repository root holding the clang-format file and Modules/ tree.",
    )
    parser.add_argument(
        "--clang-format",
        default="clang-format",
        help="clang-format executable to invoke (default: from PATH).",
    )
    return parser.parse_args()


def discover_sources(root: Path) -> list[Path]:
    """Find tracked *.h/*.cpp under Modules/, preferring git when available."""
    root = root.resolve()
    try:
        result = subprocess.run(
            ["git", "-C", str(root), "ls-files", "--", "*.h", "*.cpp"],
            capture_output=True,
            text=True,
            check=True,
        )
        tracked = [
            (root / line).resolve()
            for line in result.stdout.splitlines()
            if line
        ]
        if tracked:
            return sorted(tracked)
    except (subprocess.CalledProcessError, FileNotFoundError):
        pass

    modules = root / "Modules"
    if not modules.is_dir():
        return []
    sources = [
        path
        for path in modules.rglob("*")
        if path.is_file()
        and path.suffix.lower() in SOURCE_SUFFIXES
        and "build" not in path.parts
    ]
    return sorted(sources)


def check_files(
    sources: list[Path], style_file: Path, clang_format: str
) -> tuple[list[Path], list[str]]:
    """Run one batched dry-run and split offending files from invocation errors."""
    if not sources:
        return [], []

    command = [
        clang_format,
        f"--style=file:{style_file}",
        "--dry-run",
        "--Werror",
        *[str(path) for path in sources],
    ]
    try:
        result = subprocess.run(
            command, capture_output=True, text=True, check=False
        )
    except FileNotFoundError:
        return [], [f"{clang_format}: executable not found on PATH"]

    offenders = []
    for line in result.stdout.splitlines() + result.stderr.splitlines():
        match = DIAGNOSTIC_PATH_PATTERN.match(line)
        if match is None:
            continue
        candidate = Path(match.group(1)).resolve()
        if candidate in sources and candidate not in offenders:
            offenders.append(candidate)
    return sorted(offenders), []


def main() -> int:
    """Aggregate offending files and expose pass/fail through process status."""
    arguments = parse_arguments()
    root = arguments.root.resolve()
    style_file = root / "clang-format"
    if not style_file.is_file():
        print(f"{style_file}: clang-format policy file not found", file=sys.stderr)
        return 2

    sources = discover_sources(root)
    if not sources:
        print(f"{root}: no tracked C++ sources found under Modules/", file=sys.stderr)
        return 2

    offenders, invocation_errors = check_files(
        sources, style_file, arguments.clang_format
    )
    for error in invocation_errors:
        print(error, file=sys.stderr)
    if invocation_errors:
        return 2

    for path in offenders:
        print(str(path), file=sys.stderr)
    if offenders:
        print(f"{len(offenders)} file(s) violate the clang-format policy.", file=sys.stderr)
        return 1

    print(f"Formatting check passed ({len(sources)} files).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
