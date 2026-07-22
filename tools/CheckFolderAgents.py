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


def parse_arguments() -> argparse.Namespace:
    """Require callers to declare the package roots and generated-name exclusions."""
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", action="append", required=True, type=Path)
    parser.add_argument("--exclude", action="append", default=[])
    parser.add_argument("--require-file", action="append", default=[], type=Path)
    return parser.parse_args()


def is_excluded(path: Path, excluded_names: set[str]) -> bool:
    """Apply the same directory-name exclusion at discovery and validation time."""
    return any(part in excluded_names for part in path.parts)


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
    excluded_names = set(arguments.exclude)
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
            if path.is_dir() and not is_excluded(path, excluded_names)
        )
        for directory in sorted(directories):
            if is_excluded(directory, excluded_names):
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
