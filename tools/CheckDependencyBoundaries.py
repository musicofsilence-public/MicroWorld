#!/usr/bin/env python3
"""Reject portable system dependencies that violate MicroWorld module direction."""

from __future__ import annotations

import argparse
import re
import sys
import tempfile
from pathlib import Path


# The folder tree under Modules/MicroWorld/ names the six contract-defined
# systems directly. Each system may include itself plus only these inward
# portable dependencies. Object folded into Engine (so Engine owns identity and
# lifetime); Net and RadioE32 folded into Transport (so one byte-I/O system owns
# the device contract and every medium). Engine and Transport never name each
# other: that separation is the invariant the whole shape protects. Integration
# became Networking.
MODULE_DEPENDENCIES = {
    "Core": set(),
    "Engine": {"Core"},
    "Messaging": {"Core"},
    "Transport": {"Core"},
    "Networking": {"Core", "Messaging", "Transport"},
    "Application": {"Core", "Engine"},
}

# Platform systems remain outside portable-system ownership enforcement, but
# their public include namespaces still identify forbidden outward edges from
# portable code (a portable system must not include a platform header). Each
# lives under MicroWorld/Platform/<Family>/ and is named by its family.
PLATFORM_MODULE_NAMES = {"Host", "Esp32", "Pico"}

# Platform-facing APIs are intentionally absent: portable systems may use only
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
# These sub-namespaces under include <MicroWorld/Core/<Seg>/ are owned by Core
# (they were the Memory package's public surface before it folded in). Resolving
# them to Core keeps a Core header that includes <MicroWorld/Core/Containers/Span.h>
# legal and a Core-owned sub-folder recognized as Core.
CORE_PUBLIC_SEGMENTS = {"Containers", "Delegates", "Memory", "IO"}
INCLUDE_PATTERN = re.compile(
    r'^\s*#\s*include\s*([<"])([^>"]+)[>"]',
    re.MULTILINE,
)


def parse_arguments() -> argparse.Namespace:
    """Require explicit system ownership or an isolated deterministic self-test."""
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--package",
        action="append",
        default=[],
        metavar="MODULE=PATH",
        help="Declare one portable system and the directory it owns.",
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


def is_excluded(path: Path, root: Path, excluded_names: set[str]) -> bool:
    """
    Keep generated directory names outside the maintained dependency graph.

    Only names below the scan root count: matching the whole path would let a
    checkout living under an excluded name scan nothing and still report success.
    """
    return any(part in excluded_names for part in path.relative_to(root).parts)


def discover_sources(
    package_root: Path,
    excluded_names: set[str],
) -> list[Path]:
    """
    Find maintained headers and sources in one flat system directory.

    The merged layout places headers and sources side by side under the system
    directory (e.g. Modules/MicroWorld/Core/Time.h), with sub-namespaces like
    Containers/ and Detail/ nested below. Scan the whole tree recursively so
    public headers, sources, and Detail implementation headers are all governed.
    """
    if not package_root.is_dir():
        return []
    return sorted(
        path
        for path in package_root.rglob("*")
        if path.is_file()
        and path.suffix.lower() in SOURCE_SUFFIXES
        and not is_excluded(path, package_root, excluded_names)
    )


def declared_path_module(path: Path, package_root: Path) -> str | None:
    """
    Detect a logical system whose include namespace conflicts with its owner.

    A source file's first directory segment below the system root is either a
    header name (flat) or a sub-namespace (Containers, Delegates, Memory, IO,
    Detail). Core owns the folded sub-namespaces; Detail belongs to its owner.
    Because the system directory IS the system now, a misplaced header is one
    placed under the wrong system folder, which the include analysis catches
    instead — this guard stays to flag a sub-folder that names another system.
    """
    relative_parts = path.relative_to(package_root).parts
    if not relative_parts:
        return None
    first = relative_parts[0]
    # A Core-owned sub-namespace folder is always Core.
    if first in CORE_PUBLIC_SEGMENTS:
        return "Core"
    # Detail is an implementation sub-folder of its owner, not a foreign system.
    if first == "Detail":
        return None
    return None


def included_module(header: str) -> str | None:
    """Map a public MicroWorld include path to its logical dependency owner."""
    normalized_header = header.replace("\\", "/")
    prefix = "MicroWorld/"
    if not normalized_header.startswith(prefix):
        return None
    remainder = normalized_header[len(prefix):]
    segments = remainder.split("/")
    if not segments:
        return "Core"
    first_segment = segments[0]
    # The Platform family nests one level deeper: MicroWorld/Platform/<Family>/.
    if first_segment == "Platform" and len(segments) >= 2:
        family = segments[1]
        return family if family in PLATFORM_MODULE_NAMES else None
    # A Core header with no system segment (should not happen post-rewrite, but
    # resolves safely to Core).
    if first_segment not in MODULE_DEPENDENCIES and first_segment not in PLATFORM_MODULE_NAMES:
        # Core's folded sub-namespaces are reached as <MicroWorld/Core/<Seg>/...>,
        # so a bare unrecognized first segment is treated as Core (the previous
        # behavior for unrecognized prefixes).
        return "Core"
    return first_segment


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
    """Validate system ownership, folder placement, and dependency direction."""
    errors: list[str] = []
    scanned_files = 0

    for owner, package_root in packages:
        if not package_root.is_dir():
            errors.append(f"{package_root}: {owner} system root is not a directory")
            continue

        sources = discover_sources(package_root, excluded_names)
        if not sources:
            errors.append(
                f"{package_root}: {owner} system has no maintained sources"
            )
            continue

        for path in sources:
            scanned_files += 1
            path_module = declared_path_module(path, package_root)
            if path_module is not None and path_module != owner:
                errors.append(
                    f"{path}: {owner} system contains a "
                    f"{path_module} module path"
                )
            errors.extend(analyze_source(path, owner))

    return errors, scanned_files


def run_self_test() -> int:
    """Prove valid edges pass and package, backward, and vendor violations fail."""
    with tempfile.TemporaryDirectory() as temporary_directory:
        root = Path(temporary_directory)
        core = root / "core"
        engine = root / "engine"
        transport = root / "transport"
        # Core owns a folded Containers sub-namespace and a stray Transport-bucket
        # leak fixture (its files resolve to Core, not a foreign system).
        (core / "Containers").mkdir(parents=True)
        (core / "Transport").mkdir(parents=True)
        engine.mkdir(parents=True)
        (transport / "Detail").mkdir(parents=True)

        # A Core header may reach Core; a Containers header using Core/Time passes.
        (core / "Time.h").write_text("#pragma once\n", encoding="utf-8")
        (core / "Containers" / "Span.h").write_text(
            "#include <MicroWorld/Core/Time.h>\n",
            encoding="utf-8",
        )
        # A Core header must not reach Engine (Object is folded into Engine now).
        (core / "BadDirection.h").write_text(
            "#include <MicroWorld/Engine/Actor.h>\n",
            encoding="utf-8",
        )
        # A Core source must not include a vendor SDK header.
        (core / "BadVendor.h").write_text(
            "#include <esp_log.h>\n",
            encoding="utf-8",
        )
        # A Core file placed under a stray Transport folder is flagged as a
        # foreign module path even though it lives in Core's tree.
        (core / "Transport" / "Leak.h").write_text("#pragma once\n", encoding="utf-8")

        # Engine may reach Core but nothing else; it must never reach Transport.
        (engine / "World.h").write_text(
            "#include <MicroWorld/Core/Time.h>\n",
            encoding="utf-8",
        )
        (engine / "TransportLeak.h").write_text(
            "#include <MicroWorld/Transport/NetDriver.h>\n",
            encoding="utf-8",
        )

        # Transport may reach Core but never Engine — the core invariant.
        (transport / "NetDriver.h").write_text(
            "#include <MicroWorld/Core/Time.h>\n",
            encoding="utf-8",
        )
        (transport / "EngineLeak.h").write_text(
            "#include <MicroWorld/Engine/World.h>\n",
            encoding="utf-8",
        )
        # Transport Detail headers are governed as part of Transport.
        (transport / "Detail" / "State.h").write_text(
            "#include <MicroWorld/Core/Time.h>\n",
            encoding="utf-8",
        )
        # A portable system must not reach any platform family.
        for family in sorted(PLATFORM_MODULE_NAMES):
            (transport / f"{family}Leak.h").write_text(
                f"#include <MicroWorld/Platform/{family}/Driver.h>\n",
                encoding="utf-8",
            )

        errors, _ = analyze_packages(
            [("Core", core), ("Engine", engine), ("Transport", transport)],
            {"build", ".pio", "__pycache__"},
        )
        expected_fragments = (
            "Core must not depend on Engine",
            "external header esp_log.h",
            "Engine must not depend on Transport",
            "Transport must not depend on Engine",
            "Transport must not depend on Host",
            "Transport must not depend on Esp32",
            "Transport must not depend on Pico",
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

        # The valid edges must NOT be flagged.
        unexpected_valid_edge_fragments = (
            "Engine must not depend on Core",
            "Transport must not depend on Core",
            "Containers.h: Core must not depend",  # sub-namespace self-reach
        )
        rejected_valid_edges = [
            fragment
            for fragment in unexpected_valid_edge_fragments
            if any(fragment in error for error in errors)
        ]
        if rejected_valid_edges:
            for fragment in rejected_valid_edges:
                print(
                    f"Self-test rejected valid edge: {fragment}",
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
        f"({len(packages)} systems, {scanned_files} files)."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
