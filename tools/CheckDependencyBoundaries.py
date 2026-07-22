#!/usr/bin/env python3
"""Reject portable package dependencies that violate MicroWorld module direction."""

from __future__ import annotations

import argparse
import re
import sys
import tempfile
from pathlib import Path


# Each module may include itself plus only these inward portable dependencies.
# Serialization and Integration are intentionally absent: those packages do not
# exist and predeclaring them would authorize package paths that are not built.
MODULE_DEPENDENCIES = {
    "Core": set(),
    "Memory": {"Core"},
    "Object": {"Core", "Memory"},
    "Engine": {"Core", "Memory", "Object"},
    "Net": {"Core", "Memory"},
}

# Platform-facing APIs are intentionally absent: portable packages may use only
# MicroWorld and the conservative C++17 standard library at compile time.
STANDARD_LIBRARY_HEADERS = {
    "algorithm",
    "array",
    "atomic",
    "bitset",
    "cassert",
    "cctype",
    "cerrno",
    "cfloat",
    "chrono",
    "cinttypes",
    "climits",
    "clocale",
    "cmath",
    "complex",
    "condition_variable",
    "csetjmp",
    "csignal",
    "cstdarg",
    "cstddef",
    "cstdint",
    "cstdio",
    "cstdlib",
    "cstring",
    "ctime",
    "cwchar",
    "cwctype",
    "deque",
    "exception",
    "forward_list",
    "fstream",
    "functional",
    "future",
    "initializer_list",
    "iomanip",
    "ios",
    "iosfwd",
    "iostream",
    "istream",
    "iterator",
    "limits",
    "list",
    "locale",
    "map",
    "memory",
    "mutex",
    "new",
    "numeric",
    "ostream",
    "queue",
    "random",
    "ratio",
    "regex",
    "scoped_allocator",
    "set",
    "shared_mutex",
    "sstream",
    "stack",
    "stdexcept",
    "streambuf",
    "string",
    "string_view",
    "system_error",
    "thread",
    "tuple",
    "type_traits",
    "typeindex",
    "typeinfo",
    "unordered_map",
    "unordered_set",
    "utility",
    "valarray",
    "variant",
    "vector",
}

# Quoted vendor headers also need rejection even though local private headers
# remain valid quoted includes.
VENDOR_HEADER_PREFIXES = (
    "arduino.h",
    "driver/",
    "esp_",
    "freertos/",
    "hardware/",
    "pico/",
    "stm32",
)

SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hpp"}
MEMORY_PUBLIC_SEGMENTS = {"Containers", "Delegates", "Memory"}
INCLUDE_PATTERN = re.compile(
    r'^\s*#\s*include\s*([<"])([^>"]+)[>"]',
    re.MULTILINE,
)


def parse_arguments() -> argparse.Namespace:
    """Require explicit package ownership or an isolated deterministic self-test."""
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--package",
        action="append",
        default=[],
        metavar="MODULE=PATH",
        help="Declare one portable package and the module it owns.",
    )
    parser.add_argument(
        "--exclude",
        action="append",
        default=["build", ".pio", "__pycache__"],
        help="Exclude any directory with this exact name.",
    )
    parser.add_argument("--self-test", action="store_true")
    return parser.parse_args()


def parse_package_specification(specification: str) -> tuple[str, Path] | None:
    """Convert one owner/path declaration without misreading drive-letter colons."""
    if "=" not in specification:
        return None
    raw_module, raw_path = specification.split("=", 1)
    canonical_modules = {
        module.casefold(): module for module in MODULE_DEPENDENCIES
    }
    module = canonical_modules.get(raw_module.strip().casefold())
    path = Path(raw_path.strip())
    if module is None or not raw_path.strip():
        return None
    return module, path


def is_excluded(path: Path, excluded_names: set[str]) -> bool:
    """Keep generated directory names outside the maintained dependency graph."""
    return any(part in excluded_names for part in path.parts)


def discover_sources(
    package_root: Path,
    excluded_names: set[str],
) -> list[Path]:
    """Find only maintained public headers and runtime sources in one package."""
    sources: list[Path] = []
    for source_root_name in ("include", "src"):
        source_root = package_root / source_root_name
        if not source_root.is_dir():
            continue
        sources.extend(
            path
            for path in source_root.rglob("*")
            if path.is_file()
            and path.suffix.lower() in SOURCE_SUFFIXES
            and not is_excluded(path, excluded_names)
        )
    return sorted(sources)


def declared_path_module(path: Path, package_root: Path) -> str | None:
    """Detect a logical module folder that conflicts with its owning package."""
    relative_parts = path.relative_to(package_root).parts
    if not relative_parts:
        return None

    if relative_parts[0] == "include" and "MicroWorld" in relative_parts:
        namespace_index = relative_parts.index("MicroWorld")
        if namespace_index + 1 >= len(relative_parts) - 1:
            return "Core"
        candidate = relative_parts[namespace_index + 1]
        if candidate in MEMORY_PUBLIC_SEGMENTS:
            return "Memory"
        return candidate if candidate in MODULE_DEPENDENCIES else "Core"

    if relative_parts[0] == "src" and len(relative_parts) > 2:
        candidate = relative_parts[1]
        if candidate in MODULE_DEPENDENCIES:
            return candidate
    return None


def included_module(header: str) -> str | None:
    """Map a public MicroWorld include path to its logical dependency owner."""
    normalized_header = header.replace("\\", "/")
    prefix = "MicroWorld/"
    if not normalized_header.startswith(prefix):
        return None
    remainder = normalized_header[len(prefix) :]
    first_segment = remainder.split("/", 1)[0]
    if first_segment in MEMORY_PUBLIC_SEGMENTS:
        return "Memory"
    return (
        first_segment
        if first_segment in MODULE_DEPENDENCIES
        else "Core"
    )


def is_external_header(header: str, delimiter: str) -> bool:
    """Reject SDK/third-party includes while allowing standard and local headers."""
    normalized_header = header.replace("\\", "/").casefold()
    if normalized_header.startswith("microworld/"):
        return False
    if normalized_header in STANDARD_LIBRARY_HEADERS:
        return False
    if any(
        normalized_header.startswith(prefix)
        for prefix in VENDOR_HEADER_PREFIXES
    ):
        return True
    return delimiter == "<"


def analyze_source(path: Path, owner: str) -> list[str]:
    """Validate all compile-time dependencies in one owned source file."""
    text = path.read_text(encoding="utf-8")
    allowed_modules = MODULE_DEPENDENCIES[owner] | {owner}
    errors: list[str] = []

    for match in INCLUDE_PATTERN.finditer(text):
        delimiter = match.group(1)
        header = match.group(2).strip()
        line = text.count("\n", 0, match.start()) + 1
        dependency = included_module(header)
        if dependency is not None and dependency not in allowed_modules:
            errors.append(
                f"{path}:{line}: {owner} must not depend on "
                f"{dependency} through <{header}>"
            )
        elif dependency is None and is_external_header(header, delimiter):
            errors.append(
                f"{path}:{line}: portable {owner} must not include "
                f"external header {header}"
            )
    return errors


def analyze_packages(
    packages: list[tuple[str, Path]],
    excluded_names: set[str],
) -> tuple[list[str], int]:
    """Validate package ownership, folder placement, and dependency direction."""
    errors: list[str] = []
    scanned_files = 0

    for owner, package_root in packages:
        if not package_root.is_dir():
            errors.append(f"{package_root}: {owner} package root is not a directory")
            continue

        sources = discover_sources(package_root, excluded_names)
        if not sources:
            errors.append(
                f"{package_root}: {owner} package has no maintained sources"
            )
            continue

        for path in sources:
            scanned_files += 1
            path_module = declared_path_module(path, package_root)
            if path_module is not None and path_module != owner:
                errors.append(
                    f"{path}: {owner} package contains a "
                    f"{path_module} module path"
                )
            errors.extend(analyze_source(path, owner))

    return errors, scanned_files


def run_self_test() -> int:
    """Prove valid edges pass and package, backward, and vendor violations fail."""
    with tempfile.TemporaryDirectory() as temporary_directory:
        root = Path(temporary_directory)
        core = root / "core"
        memory = root / "memory"
        net = root / "net"
        (core / "include" / "MicroWorld" / "Net").mkdir(parents=True)
        (memory / "include" / "MicroWorld" / "Memory").mkdir(parents=True)
        (memory / "include" / "MicroWorld" / "Containers").mkdir(parents=True)
        (net / "include" / "MicroWorld" / "Net").mkdir(parents=True)

        (core / "include" / "MicroWorld" / "Good.h").write_text(
            "#include <cstdint>\n",
            encoding="utf-8",
        )
        (core / "include" / "MicroWorld" / "BadVendor.h").write_text(
            "#include <esp_log.h>\n",
            encoding="utf-8",
        )
        (core / "include" / "MicroWorld" / "Net" / "Leak.h").write_text(
            "#pragma once\n",
            encoding="utf-8",
        )
        (
            memory
            / "include"
            / "MicroWorld"
            / "Memory"
            / "BadDirection.h"
        ).write_text(
            "#include <MicroWorld/Object/Object.h>\n",
            encoding="utf-8",
        )
        (
            memory
            / "include"
            / "MicroWorld"
            / "Containers"
            / "Good.h"
        ).write_text(
            "#include <MicroWorld/Time.h>\n",
            encoding="utf-8",
        )
        # A valid Net header may reach Core and Memory but nothing above it.
        (net / "include" / "MicroWorld" / "Net" / "Good.h").write_text(
            "#include <MicroWorld/Time.h>\n"
            "#include <MicroWorld/Containers/Span.h>\n",
            encoding="utf-8",
        )
        # Net must not depend on Object or Engine; both directions must fail.
        (net / "include" / "MicroWorld" / "Net" / "ObjectLeak.h").write_text(
            "#include <MicroWorld/Object/Object.h>\n",
            encoding="utf-8",
        )
        (net / "include" / "MicroWorld" / "Net" / "EngineLeak.h").write_text(
            "#include <MicroWorld/Engine/World.h>\n",
            encoding="utf-8",
        )

        errors, _ = analyze_packages(
            [("Core", core), ("Memory", memory), ("Net", net)],
            {"build", ".pio", "__pycache__"},
        )
        expected_fragments = (
            "external header esp_log.h",
            "Core package contains a Net module path",
            "Memory must not depend on Object",
            "Net must not depend on Object",
            "Net must not depend on Engine",
        )
        missing_fragments = [
            fragment
            for fragment in expected_fragments
            if not any(fragment in error for error in errors)
        ]
        if missing_fragments:
            for fragment in missing_fragments:
                print(
                    f"Self-test did not detect: {fragment}",
                    file=sys.stderr,
                )
            return 1

    print("Dependency-boundary checker self-test passed.")
    return 0


def main() -> int:
    """Expose deterministic dependency diagnostics through process status."""
    arguments = parse_arguments()
    if arguments.self_test:
        return run_self_test()
    if not arguments.package:
        print("At least one --package MODULE=PATH is required.", file=sys.stderr)
        return 2

    packages: list[tuple[str, Path]] = []
    invalid_specifications: list[str] = []
    for specification in arguments.package:
        package = parse_package_specification(specification)
        if package is None:
            invalid_specifications.append(specification)
        else:
            packages.append(package)

    if invalid_specifications:
        for specification in invalid_specifications:
            print(
                f"Invalid package specification: {specification}",
                file=sys.stderr,
            )
        return 2

    errors, scanned_files = analyze_packages(
        packages,
        set(arguments.exclude),
    )
    for error in errors:
        print(error, file=sys.stderr)
    if errors:
        return 1

    print(
        "Dependency-boundary check passed "
        f"({len(packages)} packages, {scanned_files} files)."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
