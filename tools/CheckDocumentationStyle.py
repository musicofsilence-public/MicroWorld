#!/usr/bin/env python3
"""Validate the Motivation/Responsibilities/Example documentation contracts.

Every maintained C++ declaration carries an adjacent ``/** ... */`` block whose
labels state why it exists and what it owes. This scanner enforces the tiered
contract by declaration kind:

* ``class`` / ``struct`` / ``enum class``  -> Motivation, Responsibilities, Example
* function (free or member)                 -> Motivation, Responsibilities
* variable, enum value, ``using``/typedef   -> Motivation

A regex parser cannot understand arbitrary C++, so the scanner anchors on the
codebase's existing convention -- every declaration is already documented -- and
classifies the declaration that immediately follows each documented block. Type
definitions are additionally checked for comment *presence*, because anchoring on
comments alone cannot detect an undocumented type. Function versus variable
classification is approximate: template argument regions are stripped, the
declarator before any ``=`` is read, and ``operator`` is recognized as a function
name. The boundary this leaves out is the same one the previous
``CheckClassDocumentation.py`` stated: a regex-only parser must not pretend to
understand arbitrary C++.
"""

from __future__ import annotations

import argparse
import os
import re
import sys
import tempfile
from collections.abc import Iterator
from pathlib import Path


# Type definitions are recognized by their braced body so forward declarations
# and friend notes -- which own no contract -- stay out of the scan.
TYPE_PATTERN = re.compile(
    r"^\s*(?:class|struct)\s+([A-Za-z_]\w*)\b[^;{]*\{",
    re.MULTILINE,
)
SCOPED_ENUM_PATTERN = re.compile(
    r"^\s*enum\s+(?:class|struct)\s+([A-Za-z_]\w*)\b[^;{]*\{",
    re.MULTILINE,
)
# A documented block is any /** ... */; a trailing inline note is ///<.
BLOCK_PATTERN = re.compile(r"/\*\*[\s\S]*?\*/")
INLINE_PATTERN = re.compile(r"///<([^\n]*)")
FENCE_PATTERN = re.compile(
    r"```(?:cpp|c\+\+|cc|cxx)\s*\n(.*?)```",
    re.DOTALL | re.IGNORECASE,
)
LABEL_NAMES = ("Motivation", "Responsibilities", "Example")
LABEL_HEADER = re.compile(r"^\s*(Motivation|Responsibilities|Example)\s*:\s*(.*)$")
# Tokens that may follow a documented block without being a tiered declaration:
# access specifiers, namespace openings, preprocessor lines, other comments, and
# scope closes all carry documentation that the tiered rule does not describe.
SKIP_PREFIXES = (
    "#",
    "//",
    "/*",
    "/**",
    "namespace",
    "public",
    "private",
    "protected",
    "}",
    "using\n",
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
    """Take the scan roots, or run the isolated deterministic self-test."""
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", action="append", default=[], type=Path)
    parser.add_argument(
        "--exclude",
        action="append",
        default=[],
        help="Directory name to skip in addition to DEFAULT_EXCLUDED_DIRECTORY_NAMES.",
    )
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
    """Find only the adjacent block so unrelated earlier comments cannot satisfy policy."""
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


def strip_comment(comment: str) -> str:
    """Drop the /** */ wrapper and the leading ``*`` of each interior line."""
    inner = re.sub(r"^/\*\*", "", comment)
    inner = re.sub(r"\*/$", "", inner)
    inner = re.sub(r"^\s*\*\s?", "", inner, flags=re.MULTILINE)
    return inner.strip()


def parse_labels(inner: str) -> dict[str, bool]:
    """
    Report which labels carry content.

    A label is filled when text follows its colon on the same line or on any
    continuation line before the next label, so both the prose form
    (``Motivation: because ...``) and the code-block form (``Example:`` followed
    by indented lines) satisfy the rule.
    """
    filled = {name: False for name in LABEL_NAMES}
    current: str | None = None
    for line in inner.splitlines():
        match = LABEL_HEADER.match(line)
        if match is not None:
            current = match.group(1)
            if match.group(2).strip():
                filled[current] = True
        elif current is not None and line.strip():
            filled[current] = True
    return filled


def strip_balanced(text: str, open_ch: str, close_ch: str) -> str:
    """
    Remove balanced ``open_ch ... close_ch`` regions, leaving depth-0 text.

    Used to drop template arguments and parenthesized groups so a variable whose
    declarator contains ``sizeof(ElementType)``, ``alignas(T)``, or an array
    bound expression reads as a variable rather than being mistaken for a
    function. A real function declarator's parameter ``(`` sits at depth 0
    after the name, so it survives.
    """
    output: list[str] = []
    depth = 0
    for character in text:
        if character == open_ch:
            depth += 1
            continue
        if character == close_ch:
            if depth > 0:
                depth -= 1
            continue
        if depth == 0:
            output.append(character)
    return "".join(output)


def strip_template_args(text: str) -> str:
    """Remove balanced ``<...>`` regions so a ``TDelegate<void()>`` member reads as a variable."""
    return strip_balanced(text, "<", ">")


def classify_following(text: str, block_end: int) -> str:
    """
    Classify the declaration a documented block describes.

    Returns one of ``"type"``, ``"function"``, ``"variable"``, or ``"skip"``.
    ``"skip"`` covers access specifiers, namespaces, preprocessor lines, other
    comments, and scope closes -- contexts that carry documentation the tiered
    rule does not describe, and where misclassification would only produce noise.
    """
    rest = text[block_end:]
    stripped_rest = rest.lstrip()
    if not stripped_rest:
        return "skip"
    for prefix in SKIP_PREFIXES:
        if stripped_rest.startswith(prefix):
            return "skip"
    rest = re.sub(r"^template\s*<[^>]*>\s*", "", stripped_rest)
    declarator = re.match(r"[^;{}]*", rest).group(0).strip()
    if not declarator:
        return "skip"
    if re.match(r"^(?:class|struct|enum)\b", declarator):
        return "type"
    before_equals = strip_template_args(declarator.split("=", 1)[0])
    # sizeof(...), alignas(...), alignof(...), and [[...]]/__attribute__((...))
    # carry parentheses that must not flip a variable into a function. Strip
    # those known constructs before looking for the parameter '(' that follows a
    # function name; the raw ')' left after template-arg stripping is harmless.
    cleaned = re.sub(
        r"\b(?:sizeof|alignas|alignof|_Alignof|_Alignas)\s*\(",
        "",
        before_equals,
    )
    cleaned = re.sub(r"(?:__attribute__\s*\(\(|\[\[)", "", cleaned)
    if "operator" in cleaned or "(" in cleaned:
        return "function"
    return "variable"


def line_of(text: str, offset: int) -> int:
    """Return the 1-based line number of one offset for diagnostics."""
    return text.count("\n", 0, offset) + 1


def scan_cpp_text(display_path: str, text: str, base_line: int) -> list[str]:
    """Validate every documented declaration in one C++ text fragment."""
    errors: list[str] = []
    line_base = base_line - 1

    # Type definitions must carry an adjacent block with all three labels, and
    # anchoring on comments alone cannot detect one that is missing entirely.
    for pattern, kind in ((TYPE_PATTERN, "type"), (SCOPED_ENUM_PATTERN, "enum")):
        for match in pattern.finditer(text):
            name = match.group(1)
            line = base_line + text.count("\n", 0, match.start())
            contract = find_contract(text, match.start())
            if contract is None:
                errors.append(
                    f"{display_path}:{line}: {kind} {name} lacks an adjacent "
                    "/** ... */ contract"
                )
                continue
            labels = parse_labels(strip_comment(contract))
            for label in LABEL_NAMES:
                if not labels[label]:
                    errors.append(
                        f"{display_path}:{line}: {kind} {name} contract lacks "
                        f"required '{label}:' label"
                    )

    # Each documented block is validated for the labels its declaration kind
    # owes; type-adjacent blocks are owned by the loop above, so they are skipped
    # here to avoid restating their (stricter) requirement with a weaker message.
    for block in BLOCK_PATTERN.finditer(text):
        following = classify_following(text, block.end())
        if following in ("type", "skip"):
            continue
        labels = parse_labels(strip_comment(block.group(0)))
        line = base_line + text.count("\n", 0, block.start())
        required = ("Motivation", "Responsibilities") if following == "function" else ("Motivation",)
        for label in required:
            if not labels[label]:
                kind = "function" if following == "function" else "variable or alias"
                errors.append(
                    f"{display_path}:{line}: documented {kind} lacks required "
                    f"'{label}:' label"
                )

    # Trailing ///< notes document enum values (and occasionally members) and owe
    # the Motivation label on the first line of the note. clang-format's comment
    # reflow splits an overlong note into continuation ///< lines, so a note and
    # its immediately-following ///< continuations are treated as one group and
    # only the group's first line is checked for the Motivation label.
    notes = list(INLINE_PATTERN.finditer(text))
    for index, note in enumerate(notes):
        previous = notes[index - 1] if index > 0 else None
        is_continuation = previous is not None and text[previous.end():note.start()].strip() == ""
        if is_continuation:
            continue
        if not re.match(r"\s*Motivation:\s*\S", note.group(1)):
            line = base_line + text.count("\n", 0, note.start())
            errors.append(
                f"{display_path}:{line}: trailing '///<' comment lacks "
                "'Motivation:' label"
            )

    # line_base is retained for future offset-based diagnostics; the per-error
    # line numbers above already fold base_line in for fence fragments.
    _ = line_base
    return errors


def scan_file(path: Path, scan_markdown_fences: bool) -> list[str]:
    """Route maintained C++ and optional Markdown examples through one policy."""
    text = path.read_text(encoding="utf-8")
    if path.suffix.lower() != ".md":
        return scan_cpp_text(str(path), text, 1)
    if not scan_markdown_fences:
        return []

    errors: list[str] = []
    for fence in FENCE_PATTERN.finditer(text):
        fence_line = text.count("\n", 0, fence.start(1)) + 1
        errors.extend(scan_cpp_text(str(path), fence.group(1), fence_line))
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
        errors.extend(scan_file(path, arguments.scan_markdown_fences))
    return errors, scanned_files


def run_self_test() -> int:
    """Prove the tiered contract passes when met and fails for each drift it must block."""
    policy = argparse.Namespace(scan_markdown_fences=False)
    correct_header = (
        "/**\n"
        " * Motivation: Holds one scheduled callback and its identity.\n"
        " * Responsibilities: Track the deadline and expose the handle.\n"
        " * Example:\n"
        " *   FGood good;\n"
        " *   good.Fire();\n"
        " */\n"
        "class FGood\n"
        "{\n"
        "public:\n"
        "    /**\n"
        "     * Motivation: Lets an owner trigger the callback.\n"
        "     * Responsibilities: Run the callback exactly once.\n"
        "     */\n"
        "    void Fire() noexcept {}\n"
        "\n"
        "    /** Motivation: Last accepted clock sample, drives the cadence gate. */\n"
        "    std::uint32_t LastMs{0};\n"
        "};\n"
        "\n"
        "/**\n"
        " * Motivation: Names one schedule shape without borrowing lifecycle errors.\n"
        " * Responsibilities: Distinguish the one-shot and looping shapes.\n"
        " * Example: EMode Mode = EMode::Looping;\n"
        " */\n"
        "enum class EMode : std::uint8_t\n"
        "{\n"
        "    OneShot, ///< Motivation: Fires once then retires the handle.\n"
        "    Looping, ///< Motivation: Reschedules from the accepted time after each fire.\n"
        "};\n"
        "\n"
        "/**\n"
        " * Motivation: Confirms one more generation can publish without wrap.\n"
        " * Responsibilities: Report whether the slot may advance its generation.\n"
        " */\n"
        "bool CanAdvance() noexcept;\n"
    )
    with tempfile.TemporaryDirectory() as temporary_directory:
        root = Path(temporary_directory)
        excluded_names = set(DEFAULT_EXCLUDED_DIRECTORY_NAMES)

        # A tiered-correct header passes, a generated tree below the root is
        # skipped, and a forward declaration owns no contract to check.
        package = root / "package"
        package.mkdir()
        (package / "build").mkdir()
        (package / "Good.h").write_text(correct_header, encoding="utf-8")
        (package / "Forward.h").write_text("class FLater;\n", encoding="utf-8")
        (package / "build" / "Generated.h").write_text(
            "class FUndocumented\n{\n};\n", encoding="utf-8"
        )
        errors, scanned = scan_root(package, excluded_names, policy)
        if errors or scanned != 2:
            print(
                f"Tiered-correct self-test failed: {errors}, scanned {scanned}",
                file=sys.stderr,
            )
            return 1

        # A class contract missing the Example label is named.
        bad = package / "MissingExample.h"
        bad.write_text(
            "/**\n"
            " * Motivation: Holds state.\n"
            " * Responsibilities: Track one value.\n"
            " */\n"
            "class FBad\n{\n};\n",
            encoding="utf-8",
        )
        errors, _ = scan_root(package, excluded_names, policy)
        if not any("'Example:' label" in message and "FBad" in message for message in errors):
            print(f"Missing-Example self-test failed: {errors}", file=sys.stderr)
            return 1

        # A documented function missing Responsibilities is named.
        bad.write_text(
            "/**\n"
            " * Motivation: Starts work.\n"
            " */\n"
            "void Begin() noexcept;\n",
            encoding="utf-8",
        )
        errors, _ = scan_root(package, excluded_names, policy)
        if not any("'Responsibilities:' label" in message for message in errors):
            print(f"Missing-Responsibilities self-test failed: {errors}", file=sys.stderr)
            return 1

        # A /* single-asterisk opener does not satisfy the adjacent-/** */ rule.
        bad.write_text(
            "/*\n"
            " * Motivation: Holds state.\n"
            " * Responsibilities: Track one value.\n"
            " * Example: FBad bad;\n"
            " */\n"
            "class FBad\n{\n};\n",
            encoding="utf-8",
        )
        errors, _ = scan_root(package, excluded_names, policy)
        if not any("lacks an adjacent" in message and "FBad" in message for message in errors):
            print(f"Single-asterisk self-test failed: {errors}", file=sys.stderr)
            return 1

        # A trailing ///< note without the Motivation label is named.
        bad.write_text(
            "enum class EFlag : std::uint8_t\n"
            "{\n"
            "    On, ///< lit, not a motivation\n"
            "};\n",
            encoding="utf-8",
        )
        errors, _ = scan_root(package, excluded_names, policy)
        if not any("'///<' comment lacks" in message for message in errors):
            print(f"Inline-note self-test failed: {errors}", file=sys.stderr)
            return 1

        # A ///< note that clang-format split into continuation ///< lines is
        # one logical note; only the first line owes the Motivation label. The
        # enumerator below this one starts a new note, which must still be checked.
        enum_header = (
            "/**\n"
            " * Motivation: Names two flags.\n"
            " * Responsibilities: Distinguish them.\n"
            " * Example: EFlag F = EFlag::On;\n"
            " */\n"
        )
        bad.write_text(
            enum_header
            + "enum class EFlag : std::uint8_t\n"
            "{\n"
            "    On, ///< Motivation: an overlong note that clang-format splits\n"
            "        ///< across continuation lines must still pass as one note.\n"
            "    Off, ///< continuation that lacks its own motivation is rejected.\n"
            "};\n",
            encoding="utf-8",
        )
        errors, _ = scan_root(package, excluded_names, policy)
        continuation_errors = [
            message for message in errors if "'///<' comment lacks" in message
        ]
        if len(continuation_errors) != 1:
            print(
                f"Continuation-note self-test failed (expected 1 error): {errors}",
                file=sys.stderr,
            )
            return 1

        # An excluded name in an ancestor must not empty the scan, which a
        # whole-path match reported as a pass over zero files.
        buried_package = root / "build" / "package"
        buried_package.mkdir(parents=True)
        (buried_package / "Good.h").write_text(correct_header, encoding="utf-8")
        errors, scanned = scan_root(buried_package, excluded_names, policy)
        if errors or scanned != 1:
            print(
                f"Ancestor-exclusion self-test failed: {errors}, scanned {scanned}",
                file=sys.stderr,
            )
            return 1

    print("Documentation-style checker self-test passed.")
    return 0


def main() -> int:
    """Aggregate deterministic diagnostics and expose pass/fail through process status."""
    arguments = parse_arguments()
    if arguments.self_test:
        return run_self_test()
    if not arguments.root:
        print("At least one --root is required.", file=sys.stderr)
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
    print(f"Documentation style check passed ({scanned_files} files).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
