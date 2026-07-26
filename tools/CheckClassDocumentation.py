#!/usr/bin/env python3
"""Validate concise adjacent Doxygen contracts on C++ class definitions."""

from __future__ import annotations

import argparse
import os
import re
import sys
import tempfile
from collections.abc import Iterator
from pathlib import Path


# These patterns deliberately recognize complete definitions rather than forward
# declarations so the check enforces ownership-bearing type contracts.
DEFINITION_PATTERN = re.compile(
    r"^\s*(?!enum\b)(?:class|struct)\s+([A-Za-z_]\w*)\b[^;{]*\{",
    re.MULTILINE,
)
FENCE_PATTERN = re.compile(
    r"```(?:cpp|c\+\+|cc|cxx)\s*\n(.*?)```",
    re.DOTALL | re.IGNORECASE,
)

# Generated trees and tool metadata hold no maintained source, so every caller
# skipped the same names by hand. Matching is by exact directory name, so a build
# tree this list does not anticipate still enters the scan loudly and is added
# here or passed with --exclude, rather than inferred and skipped in silence.
DEFAULT_EXCLUDED_DIRECTORY_NAMES = frozenset(
    {
        ".git",
        ".idea",
        ".pio",
        ".vs",
        "__pycache__",
        "build",
        "build-engine",
        "build-final",
        "build-integration",
        "build-integration-messaging",
        "build-messaging",
        "build-object",
        "cmake-build-debug",
        "cmake-build-release",
    }
)


def parse_arguments() -> argparse.Namespace:
    """Take the scan roots and contract policy, or run the isolated deterministic self-test."""
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", action="append", default=[], type=Path)
    parser.add_argument(
        "--exclude",
        action="append",
        default=[],
        help="Directory name to skip in addition to DEFAULT_EXCLUDED_DIRECTORY_NAMES.",
    )
    parser.add_argument("--require-doxygen", action="store_true")
    parser.add_argument("--max-sentences", type=int, default=3)
    parser.add_argument("--scan-markdown-fences", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    return parser.parse_args()


def iter_files_below(root: Path, excluded_names: set[str]) -> Iterator[Path]:
    """
    Walk below one root in deterministic order, pruning excluded directories.

    Pruning during the walk rather than filtering afterwards is what keeps a
    generated tree cheap: a .pio or build directory is never descended into, and
    only names below the root are ever tested, so a checkout living under an
    excluded name cannot scan nothing and still report success.
    """
    for directory_path, directory_names, file_names in os.walk(root):
        directory_names[:] = sorted(
            name for name in directory_names if name not in excluded_names
        )
        for file_name in sorted(file_names):
            yield Path(directory_path) / file_name


def find_contract(text: str, declaration_offset: int) -> str | None:
    """Find only the adjacent contract so unrelated earlier comments cannot satisfy policy."""
    prefix = text[:declaration_offset].rstrip()
    template_match = re.search(r"template\s*<[^>]*>\s*$", prefix, re.DOTALL)
    if template_match is not None:
        prefix = prefix[: template_match.start()].rstrip()
    if not prefix.endswith("*/"):
        return None
    comment_start = prefix.rfind("/**")
    if comment_start < 0:
        return None
    comment = prefix[comment_start:]
    if "*/" in comment[:-2]:
        return None
    return comment


def sentence_count(comment: str) -> int:
    """Bound contract length so comments explain intent without becoming design essays."""
    content = re.sub(r"^/\*\*|\*/$", "", comment.strip(), flags=re.DOTALL)
    content = re.sub(r"^\s*\*\s?", "", content, flags=re.MULTILINE).strip()
    return len(re.findall(r"[.!?](?:\s|$)", content))


def scan_cpp_text(
    display_path: str,
    text: str,
    base_line: int,
    require_doxygen: bool,
    maximum_sentences: int,
) -> list[str]:
    """Validate every recognized type definition in one C++ text fragment."""
    errors: list[str] = []
    for match in DEFINITION_PATTERN.finditer(text):
        type_name = match.group(1)
        line = base_line + text.count("\n", 0, match.start())
        contract = find_contract(text, match.start())
        if contract is None:
            if require_doxygen:
                errors.append(
                    f"{display_path}:{line}: {type_name} lacks an adjacent "
                    "/** ... */ contract"
                )
            continue
        count = sentence_count(contract)
        if count < 1:
            errors.append(
                f"{display_path}:{line}: {type_name} contract has no sentence"
            )
        elif count > maximum_sentences:
            errors.append(
                f"{display_path}:{line}: {type_name} contract has {count} "
                f"sentences; maximum is {maximum_sentences}"
            )
    return errors


def scan_file(
    path: Path,
    scan_markdown_fences: bool,
    require_doxygen: bool,
    maximum_sentences: int,
) -> list[str]:
    """Route maintained C++ and optional Markdown examples through one policy."""
    text = path.read_text(encoding="utf-8")
    if path.suffix.lower() != ".md":
        return scan_cpp_text(
            str(path), text, 1, require_doxygen, maximum_sentences
        )
    if not scan_markdown_fences:
        return []

    errors: list[str] = []
    for fence in FENCE_PATTERN.finditer(text):
        fence_line = text.count("\n", 0, fence.start(1)) + 1
        errors.extend(
            scan_cpp_text(
                str(path),
                fence.group(1),
                fence_line,
                require_doxygen,
                maximum_sentences,
            )
        )
    return errors


def scan_root(
    root: Path,
    excluded_names: set[str],
    arguments: argparse.Namespace,
) -> tuple[list[str], int]:
    """Validate every maintained source below one root, reporting its errors and scanned count."""
    if not root.is_dir():
        return [f"{root}: scan root is not a directory"], 0

    errors: list[str] = []
    scanned_files = 0
    for path in iter_files_below(root, excluded_names):
        suffix = path.suffix.lower()
        if suffix not in {".h", ".hpp", ".cpp", ".cc", ".cxx", ".md"}:
            continue
        if suffix == ".md" and not arguments.scan_markdown_fences:
            continue
        scanned_files += 1
        errors.extend(
            scan_file(
                path,
                arguments.scan_markdown_fences,
                arguments.require_doxygen,
                arguments.max_sentences,
            )
        )
    return errors, scanned_files


def run_self_test() -> int:
    """Prove a documented type passes and that missing and overlong contracts each fail."""
    policy = argparse.Namespace(
        scan_markdown_fences=False, require_doxygen=True, max_sentences=3
    )
    documented_header = "/** Owns one thing. */\nclass FGood\n{\n};\n"
    with tempfile.TemporaryDirectory() as temporary_directory:
        root = Path(temporary_directory)
        excluded_names = set(DEFAULT_EXCLUDED_DIRECTORY_NAMES)

        # A documented definition passes, a generated tree below the root is
        # skipped, and a forward declaration owns no contract to check.
        package = root / "package"
        package.mkdir()
        (package / "build").mkdir()
        (package / "Good.h").write_text(documented_header, encoding="utf-8")
        (package / "Forward.h").write_text("class FLater;\n", encoding="utf-8")
        (package / "build" / "Generated.h").write_text(
            "class FUndocumented\n{\n};\n", encoding="utf-8"
        )
        errors, scanned = scan_root(package, excluded_names, policy)
        if errors or scanned != 2:
            print(
                f"Documented-source self-test failed: {errors}, scanned {scanned}",
                file=sys.stderr,
            )
            return 1

        # A definition with no adjacent contract must be named under --require-doxygen.
        bare_header = package / "Bare.h"
        bare_header.write_text("class FBare\n{\n};\n", encoding="utf-8")
        errors, _ = scan_root(package, excluded_names, policy)
        if len(errors) != 1 or "lacks an adjacent" not in errors[0]:
            print(f"Missing-contract self-test failed: {errors}", file=sys.stderr)
            return 1

        # A contract past the bound must be named together with its sentence count.
        bare_header.write_text(
            "/** One. Two. Three. Four. */\nclass FBare\n{\n};\n", encoding="utf-8"
        )
        errors, _ = scan_root(package, excluded_names, policy)
        if len(errors) != 1 or "maximum is 3" not in errors[0]:
            print(f"Overlong-contract self-test failed: {errors}", file=sys.stderr)
            return 1

        # An excluded name in an ancestor must not empty the scan, which a
        # whole-path match reported as a pass over zero files.
        buried_package = root / "build" / "package"
        buried_package.mkdir(parents=True)
        (buried_package / "Good.h").write_text(documented_header, encoding="utf-8")
        errors, scanned = scan_root(buried_package, excluded_names, policy)
        if errors or scanned != 1:
            print(
                f"Ancestor-exclusion self-test failed: {errors}, scanned {scanned}",
                file=sys.stderr,
            )
            return 1

    print("Class-documentation checker self-test passed.")
    return 0


def main() -> int:
    """Aggregate deterministic diagnostics and expose pass/fail through process status."""
    arguments = parse_arguments()
    if arguments.self_test:
        return run_self_test()
    if not arguments.root:
        print("At least one --root is required.", file=sys.stderr)
        return 2
    if arguments.max_sentences < 1:
        print("--max-sentences must be positive", file=sys.stderr)
        return 2

    excluded_names = DEFAULT_EXCLUDED_DIRECTORY_NAMES | set(arguments.exclude)
    errors: list[str] = []
    scanned_files = 0
    for root in arguments.root:
        root_errors, scanned = scan_root(root, excluded_names, arguments)
        errors.extend(root_errors)
        scanned_files += scanned

    for error in errors:
        print(error, file=sys.stderr)
    if errors:
        return 1
    print(f"Class documentation check passed ({scanned_files} files).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
