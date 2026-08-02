"""Enforce the MicroWorld source-tree namespace contract."""

from __future__ import annotations

import argparse
import re
import sys
import tempfile
from pathlib import Path


# Maps each direct engine-system folder to the namespace its source must own.
SYSTEM_NAMESPACE_RULES = {
    "Core": "MicroWorld::Core",
    "Engine": "MicroWorld::Engine",
    "Messaging": "MicroWorld::Messaging",
    "Transport": "MicroWorld::Transport",
    "Application": "MicroWorld::Application",
}

# Keeps non-portable edges distinct so platform dependencies remain visible.
PLATFORM_NAMESPACE_RULES = {
    "Host": "MicroWorld::Platform::Host",
    "Esp32": "MicroWorld::Platform::Esp32",
    "Pico": "MicroWorld::Platform::Pico",
}

# Namespaces that are intentionally deeper than their Transport parent.
TRANSPORT_NESTED_NAMESPACE_RULES = {
    "FrameCodec.h": "MicroWorld::Transport::FrameCodec",
}

# RawSlot moved into Core's storage boundary without changing its public name.
RAW_STORAGE_RELOCATION = "Modules/MicroWorld/Core/Containers/RawSlot.h"
RAW_STORAGE_NAMESPACE = "MicroWorld::Core::RawStorage"

SOURCE_EXTENSIONS = {".h", ".cpp"}
NAMESPACE_DECLARATION = re.compile(
    r"^\s*namespace\s+([A-Za-z_]\w*(?:::[A-Za-z_]\w*)*)\s*(?:\{\s*)?$"
)

# A using-directive at namespace scope re-exports everything the nominated
# namespace holds into the enclosing one, and a header carries that leak into
# every consumer that includes it. One directive is enough to make the flat
# namespace resolve again, so the system boundary must be crossed by explicit
# qualification instead.
USING_DIRECTIVE = re.compile(r"^\s*using\s+namespace\b")


def infer_repository_root() -> Path:
    """Resolve the repository root relative to this checker script."""
    return Path(__file__).resolve().parent.parent


def parse_arguments() -> argparse.Namespace:
    """Define the repository root and the isolated self-test switch."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=infer_repository_root())
    parser.add_argument("--self-test", action="store_true")
    return parser.parse_args()


def discover_sources(root: Path) -> list[Path]:
    """Find C++ source and header files under the modeled MicroWorld tree."""
    source_root = root / "Modules" / "MicroWorld"
    if not source_root.is_dir():
        return []
    return sorted(
        path for path in source_root.rglob("*") if path.is_file() and path.suffix in SOURCE_EXTENSIONS
    )


def extract_namespaces(text: str) -> list[str]:
    """Extract named namespace declarations while ignoring anonymous scopes."""
    namespaces: list[str] = []
    for line in text.splitlines():
        match = NAMESPACE_DECLARATION.match(line)
        if match:
            namespaces.append(match.group(1))
    return namespaces


def expected_namespace(relative_path: Path) -> str | None:
    """Return the exact namespace required for one modeled source path."""
    normalized_path = relative_path.as_posix()
    if normalized_path == RAW_STORAGE_RELOCATION:
        return RAW_STORAGE_NAMESPACE

    parts = relative_path.parts
    if len(parts) < 4 or parts[:2] != ("Modules", "MicroWorld"):
        return None

    system_name = parts[2]
    if system_name == "Transport" and len(parts) == 4:
        nested_namespace = TRANSPORT_NESTED_NAMESPACE_RULES.get(parts[3])
        if nested_namespace is not None:
            return nested_namespace

    if system_name == "Platform":
        if len(parts) < 5:
            return None
        return PLATFORM_NAMESPACE_RULES.get(parts[3])

    return SYSTEM_NAMESPACE_RULES.get(system_name)


def namespace_errors(root: Path) -> list[str]:
    """Return one diagnostic for each source that violates the namespace model."""
    errors: list[str] = []
    for source_path in discover_sources(root):
        relative_path = source_path.relative_to(root).as_posix()
        source_text = source_path.read_text(encoding="utf-8")

        for line_number, line in enumerate(source_text.splitlines(), start=1):
            if USING_DIRECTIVE.match(line):
                errors.append(f"{relative_path}:{line_number}: using-directive re-exports a namespace; qualify the names instead")

        expected = expected_namespace(Path(relative_path))
        if expected is None:
            errors.append(f"{relative_path}: no namespace rule exists for this source tree")
            continue

        actual = extract_namespaces(source_text)
        unique_actual = list(dict.fromkeys(actual))
        if unique_actual != [expected]:
            actual_text = ", ".join(unique_actual) if unique_actual else "<missing>"
            errors.append(f"{relative_path}: expected {expected}, found {actual_text}")
    return errors


def write_fixture(root: Path, relative_path: str, source: str) -> None:
    """Create one minimal source fixture for the isolated checker self-test."""
    fixture_path = root / relative_path
    fixture_path.parent.mkdir(parents=True, exist_ok=True)
    fixture_path.write_text(source, encoding="utf-8")


def run_fixture_case(name: str, relative_path: str, source: str, should_pass: bool) -> str | None:
    """Run one isolated fixture and describe a mismatch between expected and actual status."""
    with tempfile.TemporaryDirectory() as temporary_directory:
        root = Path(temporary_directory)
        write_fixture(root, relative_path, source)
        failed = bool(namespace_errors(root))
        if failed != (not should_pass):
            expected_status = "pass" if should_pass else "fail"
            actual_status = "fail" if failed else "pass"
            return f"{name}: expected {expected_status}, got {actual_status}"
    return None


def run_self_test() -> int:
    """Prove valid, missing, wrong, forbidden, nested, and relocated namespaces are checked."""
    cases = [
        (
            "valid direct system",
            "Modules/MicroWorld/Core/Runtime.h",
            "namespace MicroWorld::Core\n{\n}\n",
            True,
        ),
        (
            "valid nested Transport frame codec",
            "Modules/MicroWorld/Transport/FrameCodec.h",
            "namespace MicroWorld::Transport::FrameCodec\n{\n}\n",
            True,
        ),
        (
            "valid RawStorage relocation",
            RAW_STORAGE_RELOCATION,
            "namespace MicroWorld::Core::RawStorage\n{\n}\n",
            True,
        ),
        (
            "missing namespace",
            "Modules/MicroWorld/Engine/World.h",
            "struct UWorld {};\n",
            False,
        ),
        (
            "wrong namespace",
            "Modules/MicroWorld/Engine/World.h",
            "namespace MicroWorld::Core\n{\n}\n",
            False,
        ),
        (
            "forbidden nested Transport namespace",
            "Modules/MicroWorld/Transport/Wifi/Link.h",
            "namespace MicroWorld::Transport::Wifi\n{\n}\n",
            False,
        ),
        (
            "forbidden using-directive in an otherwise correct source",
            "Modules/MicroWorld/Transport/Host.h",
            "namespace MicroWorld::Transport\n{\nusing namespace ::MicroWorld::Core;\n}\n",
            False,
        ),
    ]
    for case in cases:
        error = run_fixture_case(*case)
        if error:
            print(f"Namespace checker self-test failed: {error}", file=sys.stderr)
            return 1

    print("Namespace checker self-test passed.")
    return 0


def main() -> int:
    """Run the self-test or enforce the namespace contract for the repository."""
    arguments = parse_arguments()
    if arguments.self_test:
        return run_self_test()

    root = arguments.root.resolve()
    errors = namespace_errors(root)
    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1

    print(f"Namespace checker passed for {len(discover_sources(root))} source files.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
