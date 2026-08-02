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
SOURCE_SUFFIXES = {".h", ".hpp", ".cpp", ".cc", ".cxx", ".inl"}

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
    """Find tracked *.h/*.cpp/*.inl under Modules/, preferring git when available."""
    root = root.resolve()
    try:
        result = subprocess.run(
            ["git", "-C", str(root), "ls-files", "--", "*.h", "*.cpp", "*.inl"],
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
        # Relative to Modules, so a checkout living under a directory named
        # "build" cannot empty this list and report a silent pass.
        and "build" not in path.relative_to(modules).parts
    ]
    return sorted(sources)


def check_files(
    sources: list[Path], style_file: Path, clang_format: str
) -> tuple[list[Path], list[str]]:
    """Run batched dry-runs and split offending files from invocation errors.

    On Windows, ``CreateProcess`` rejects any command line longer than 32 KiB, and a
    large source set with absolute paths can exceed that before clang-format is ever
    reached: ``subprocess.run`` then raises ``FileNotFoundError``, which is easily
    misread as "executable not found". Batching keeps every invocation well under the
    ceiling so a growing file count never turns into a false gate failure.
    """
    if not sources:
        return [], []

    # clang-format accepts the style and flags once per invocation; the variable cost
    # is the source list. 8 KiB of paths per batch leaves ample room for flags and for
    # the path-quoting inflation subprocess applies on Windows.
    max_batch_chars = 8192
    offenders: list[Path] = []
    batch: list[Path] = []
    batch_chars = 0
    for source in sources:
        source_chars = len(str(source)) + 1
        if batch and batch_chars + source_chars > max_batch_chars:
            new_offenders, _ = _run_one_batch(batch, style_file, clang_format, sources)
            offenders.extend(new_offenders)
            batch = []
            batch_chars = 0
        batch.append(source)
        batch_chars += source_chars
    if batch:
        new_offenders, invocation_errors = _run_one_batch(batch, style_file, clang_format, sources)
        offenders.extend(new_offenders)
        if invocation_errors:
            return [], invocation_errors

    # Deduplicate while preserving deterministic order.
    unique: list[Path] = []
    for source in offenders:
        if source not in unique:
            unique.append(source)
    return sorted(unique), []


def _run_one_batch(
    batch: list[Path], style_file: Path, clang_format: str, all_sources: list[Path]
) -> tuple[list[Path], list[str]]:
    """Run one clang-format dry-run over a bounded slice of the source list."""
    command = [
        clang_format,
        f"--style=file:{style_file}",
        "--dry-run",
        "--Werror",
        *[str(path) for path in batch],
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
        if candidate in all_sources and candidate not in offenders:
            offenders.append(candidate)
    return offenders, []


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
