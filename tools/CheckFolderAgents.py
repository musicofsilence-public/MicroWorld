#!/usr/bin/env python3
"""Validate scoped AGENTS.md coverage for package directories."""

from __future__ import annotations

import argparse
import re
import sys
import tempfile
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

# One complete guide, used by the self-test as the shape a passing directory owns.
SELF_TEST_COMPLETE_GUIDE = "# Guide\n\n## Architecture\n\nwhy.\n\n## Concepts\n\nhow.\n"


def parse_arguments() -> argparse.Namespace:
    """Take the package roots to scan, or run the isolated deterministic self-test."""
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", action="append", default=[], type=Path)
    parser.add_argument(
        "--exclude",
        action="append",
        default=[],
        help="Directory name to skip in addition to DEFAULT_EXCLUDED_DIRECTORY_NAMES.",
    )
    parser.add_argument("--require-file", action="append", default=[], type=Path)
    parser.add_argument("--self-test", action="store_true")
    return parser.parse_args()


def is_excluded(path: Path, root: Path, excluded_names: set[str]) -> bool:
    """
    Apply directory-name exclusion to the part of a path below the scan root.

    Only names below the root count: matching the whole path would let a checkout
    living under any excluded name skip its entire tree and still report success,
    which is the one answer a coverage gate must never give. Because the root's own
    relative path is empty, the root is always scanned and a pass is never empty.
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


def scan_root(root: Path, excluded_names: set[str]) -> tuple[list[str], int]:
    """Validate every guide at or below one root, reporting its errors and scanned count."""
    if not root.is_dir():
        return [f"{root}: scan root is not a directory"], 0

    directories = [root]
    directories.extend(
        path
        for path in root.rglob("*")
        if path.is_dir() and not is_excluded(path, root, excluded_names)
    )

    errors: list[str] = []
    for directory in sorted(directories):
        guide = directory / "AGENTS.md"
        if not guide.is_file():
            errors.append(f"{directory}: missing AGENTS.md")
            continue
        errors.extend(
            f"{guide}: missing a {section} section"
            for section in find_missing_sections(guide)
        )
    return errors, len(directories)


def run_self_test() -> int:
    """Prove a covered tree passes and that gaps, thin guides, and ancestors all fail correctly."""
    with tempfile.TemporaryDirectory() as temporary_directory:
        root = Path(temporary_directory)
        excluded_names = set(DEFAULT_EXCLUDED_DIRECTORY_NAMES)

        # A covered package passes, and the default-excluded generated trees inside
        # it are skipped rather than reported as missing guides.
        package = root / "package"
        (package / "include").mkdir(parents=True)
        (package / "__pycache__").mkdir()
        (package / "cmake-build-debug").mkdir()
        (package / "AGENTS.md").write_text(SELF_TEST_COMPLETE_GUIDE, encoding="utf-8")
        (package / "include" / "AGENTS.md").write_text(
            SELF_TEST_COMPLETE_GUIDE, encoding="utf-8"
        )
        errors, scanned = scan_root(package, excluded_names)
        if errors or scanned != 2:
            print(
                f"Covered-tree self-test failed: {errors}, scanned {scanned}",
                file=sys.stderr,
            )
            return 1

        # A maintained directory with no guide at all must be named, not skipped.
        (package / "src").mkdir()
        errors, _ = scan_root(package, excluded_names)
        if len(errors) != 1 or "missing AGENTS.md" not in errors[0]:
            print(f"Missing-guide self-test failed: {errors}", file=sys.stderr)
            return 1

        # A guide present but without architecture and concepts must fail on both.
        (package / "src" / "AGENTS.md").write_text("# Guide\n", encoding="utf-8")
        errors, _ = scan_root(package, excluded_names)
        if len(errors) != 2:
            print(f"Thin-guide self-test failed: {errors}", file=sys.stderr)
            return 1

        # An excluded name in an ancestor must not hide the tree. Matching the whole
        # path reported this covered root as an empty pass, which is the regression
        # this case exists to keep fixed.
        buried_package = root / "build" / "package"
        buried_package.mkdir(parents=True)
        (buried_package / "AGENTS.md").write_text(
            SELF_TEST_COMPLETE_GUIDE, encoding="utf-8"
        )
        errors, scanned = scan_root(
            buried_package, set(DEFAULT_EXCLUDED_DIRECTORY_NAMES)
        )
        if errors or scanned != 1:
            print(
                f"Ancestor-exclusion self-test failed: {errors}, scanned {scanned}",
                file=sys.stderr,
            )
            return 1

    print("Folder-guide checker self-test passed.")
    return 0


def main() -> int:
    """Report uncovered architecture boundaries without modifying the package tree."""
    arguments = parse_arguments()
    if arguments.self_test:
        return run_self_test()
    if not arguments.root:
        print("At least one --root is required.", file=sys.stderr)
        return 2

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
        root_errors, scanned = scan_root(root, excluded_names)
        errors.extend(root_errors)
        scanned_directories += scanned

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
