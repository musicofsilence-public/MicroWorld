#!/usr/bin/env python3
"""Validate scoped AGENTS.md coverage for package directories."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


# Architecture and concepts are the minimum context every scoped guide must own.
REQUIRED_GUIDE_SECTIONS = {
    "architecture": re.compile(r"^#{2,}\s+.*architecture.*$", re.IGNORECASE | re.MULTILINE),
    "concepts": re.compile(r"^#{2,}\s+.*concepts?.*$", re.IGNORECASE | re.MULTILINE),
}

# Generated trees and tool metadata own no architecture boundary, so every caller
# skipped the same names by hand and one forgotten name failed the whole scan.
# Matching is by exact directory name, so a build tree this list does not
# anticipate still fails loudly and is added here or passed with --exclude —
# preferred over inferring exclusions, which would skip real folders in silence.
DEFAULT_EXCLUDED_DIRECTORY_NAMES = frozenset(
    {
        ".git",
        ".idea",
        ".pio",
        ".vs",
        "__pycache__",
        "build",
        "build-final",
        "cmake-build-debug",
        "cmake-build-release",
    }
)


def parse_arguments() -> argparse.Namespace:
    """Require callers to declare the package roots; exclusions have working defaults."""
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", action="append", required=True, type=Path)
    parser.add_argument(
        "--exclude",
        action="append",
        default=[],
        help="Directory name to skip in addition to DEFAULT_EXCLUDED_DIRECTORY_NAMES.",
    )
    parser.add_argument("--require-file", action="append", default=[], type=Path)
    return parser.parse_args()


def is_excluded(path: Path, root: Path, excluded_names: set[str]) -> bool:
    """
    Apply the same directory-name exclusion at discovery and validation time.

    Only names below the scan root count: matching the whole path would let a
    checkout living under any excluded name skip its entire tree and still report
    success, which is the one answer a coverage gate must never give.
    """
    return any(part in excluded_names for part in path.relative_to(root).parts)


def find_missing_sections(guide: Path) -> list[str]:
    """Require local architecture and concept context rather than presence-only guides."""
    text = guide.read_text(encoding="utf-8")
    return [
        section
        for section, pattern in REQUIRED_GUIDE_SECTIONS.items()
        if pattern.search(text) is None
    ]


def main() -> int:
    """Report uncovered architecture boundaries without modifying the package tree."""
    arguments = parse_arguments()
    excluded_names = DEFAULT_EXCLUDED_DIRECTORY_NAMES | set(arguments.exclude)
    errors: list[str] = []
    scanned_directories = 0
    verified_required_files = 0

    for required_file in arguments.require_file:
        if not required_file.is_file():
            errors.append(f"{required_file}: required file is missing")
            continue
        verified_required_files += 1
        for section in find_missing_sections(required_file):
            errors.append(f"{required_file}: missing a {section} section")

    for root in arguments.root:
        if not root.is_dir():
            errors.append(f"{root}: scan root is not a directory")
            continue
        directories = [root]
        directories.extend(
            path
            for path in root.rglob("*")
            if path.is_dir() and not is_excluded(path, root, excluded_names)
        )
        for directory in sorted(directories):
            if is_excluded(directory, root, excluded_names):
                continue
            scanned_directories += 1
            guide = directory / "AGENTS.md"
            if not guide.is_file():
                errors.append(f"{directory}: missing AGENTS.md")
                continue
            for section in find_missing_sections(guide):
                errors.append(f"{guide}: missing a {section} section")

    for error in errors:
        print(error, file=sys.stderr)
    if errors:
        return 1
    verified_guide_count = scanned_directories + verified_required_files
    print(
        "Folder architecture/concepts check passed "
        f"({verified_guide_count} guides)."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
