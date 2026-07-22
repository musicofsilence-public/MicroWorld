#!/usr/bin/env python3
"""Validate concise adjacent Doxygen contracts on C++ class definitions."""

from __future__ import annotations

import argparse
import re
import sys
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


def parse_arguments() -> argparse.Namespace:
    """Define explicit scan boundaries so generated trees never enter by accident."""
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", action="append", required=True, type=Path)
    parser.add_argument("--exclude", action="append", default=[])
    parser.add_argument("--require-doxygen", action="store_true")
    parser.add_argument("--max-sentences", type=int, default=3)
    parser.add_argument("--scan-markdown-fences", action="store_true")
    return parser.parse_args()


def is_excluded(path: Path, excluded_names: set[str]) -> bool:
    """Keep caller-selected build/cache directory names out of maintained-source results."""
    return any(part in excluded_names for part in path.parts)


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


def main() -> int:
    """Aggregate deterministic diagnostics and expose pass/fail through process status."""
    arguments = parse_arguments()
    if arguments.max_sentences < 1:
        print("--max-sentences must be positive", file=sys.stderr)
        return 2

    excluded_names = set(arguments.exclude)
    errors: list[str] = []
    scanned_files = 0
    for root in arguments.root:
        if not root.is_dir():
            errors.append(f"{root}: scan root is not a directory")
            continue
        for path in sorted(root.rglob("*")):
            if not path.is_file() or is_excluded(path, excluded_names):
                continue
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

    for error in errors:
        print(error, file=sys.stderr)
    if errors:
        return 1
    print(f"Class documentation check passed ({scanned_files} files).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
