#!/usr/bin/env python3
"""Verify that a MicroWorld profile map contains no unselected modules."""

from __future__ import annotations

import argparse
import sys
import tempfile
from pathlib import Path


# Profile names describe package bundles. Net is an independent overlay above
# Memory: it never pulls Object or Engine, and no Engine-Net Integration profile
# is retained because that coupling is deferred until a real application needs it.
PROFILE_MODULES = {
    "Core": {"Core"},
    "Memory": {"Core", "Memory"},
    "Object": {"Core", "Memory", "Object"},
    "Core+Net": {"Core", "Memory", "Net"},
    "Managed": {"Core", "Memory", "Object", "Engine"},
    "Managed+Net": {"Core", "Memory", "Object", "Engine", "Net"},
}

# Markers cover planned CMake target/archive names, PlatformIO package archives,
# public include paths, and characteristic public symbols. Serialization and
# Integration stay listed so any accidental linkage is still detected even though
# no active profile selects them.
MODULE_MARKERS = {
    "Memory": (
        "microworld_memory",
        "microworld-memory",
        "/microworld/memory/",
        "fmemoryresource",
        "tfixedarena",
        "tsharedptr",
    ),
    "Object": (
        "microworld_object",
        "microworld-object",
        "/microworld/object/",
        "fobjectstore",
        "fgarbagecollector",
        "tobjectptr",
        "uobject",
    ),
    "Engine": (
        "microworld_engine",
        "microworld-engine",
        "/microworld/engine/",
        "fengine",
        "uworld",
        "aactor",
        "uactorcomponent",
    ),
    "Serialization": (
        "microworld_serialization",
        "microworld-serialization",
        "/microworld/serialization/",
        "fbytearchive",
    ),
    "Net": (
        "microworld_net",
        "microworld-net",
        "/microworld/net/",
        "fnetmanager",
        "inetdriver",
    ),
    "Integration": (
        "microworld_integration",
        "microworld-integration",
        "/microworld/integration/",
        "fnetenginesubsystem",
    ),
}

# A map must prove that the released physical Core archive participated, not
# merely contain the executable target's MicroWorld-shaped name.
CORE_ARCHIVE_MARKERS = (
    "microworld:",
    "microworld.lib",
    "libmicroworld.a",
)

# Memory profiles must prove that the adjacent physical archive participated;
# public template symbols alone do not establish package linkage.
MEMORY_ARCHIVE_MARKERS = (
    "microworld_memory:",
    "microworld_memory.lib",
    "libmicroworld_memory.a",
    "libmicroworld-memory.a",
    "libmicroworldmemory.a",
)

# Object profiles must link their separate package archive. Public header-only
# evidence does not prove that object-store or collector code participated.
OBJECT_ARCHIVE_MARKERS = (
    "microworld_object:",
    "microworld_object.lib",
    "libmicroworld_object.a",
    "libmicroworld-object.a",
    "libmicroworldobject.a",
)

# Net profiles must link their separate package archive. Header-only byte I/O
# evidence does not prove that the INetDriver out-of-line destructor participated.
NET_ARCHIVE_MARKERS = (
    "microworld_net:",
    "microworld_net.lib",
    "libmicroworld_net.a",
    "libmicroworld-net.a",
    "libmicroworldnet.a",
)


def parse_arguments() -> argparse.Namespace:
    """Define one map/profile gate or run the checker's isolated self-test."""
    parser = argparse.ArgumentParser()
    parser.add_argument("--map", type=Path)
    parser.add_argument("--profile", choices=PROFILE_MODULES)
    parser.add_argument("--require", action="append", default=[])
    parser.add_argument("--forbid", action="append", default=[])
    parser.add_argument("--self-test", action="store_true")
    return parser.parse_args()


def normalize_map(text: str) -> str:
    """Make archive and path checks independent of host slash and case rules."""
    return text.casefold().replace("\\", "/")


def analyze_map(
    text: str,
    profile: str,
    required_markers: list[str],
    forbidden_markers: list[str],
) -> list[str]:
    """Report missing Core evidence and every unselected-module marker."""
    normalized_text = normalize_map(text)
    errors: list[str] = []

    if not any(marker in normalized_text for marker in CORE_ARCHIVE_MARKERS):
        errors.append(
            "map does not contain the MicroWorld Core archive "
            f"({', '.join(CORE_ARCHIVE_MARKERS)})"
        )

    selected_modules = PROFILE_MODULES[profile]
    if "Memory" in selected_modules and not any(
        marker in normalized_text for marker in MEMORY_ARCHIVE_MARKERS
    ):
        errors.append(
            "map does not contain the MicroWorld Memory archive "
            f"({', '.join(MEMORY_ARCHIVE_MARKERS)})"
        )

    if "Object" in selected_modules and not any(
        marker in normalized_text for marker in OBJECT_ARCHIVE_MARKERS
    ):
        errors.append(
            "map does not contain the MicroWorld Object archive "
            f"({', '.join(OBJECT_ARCHIVE_MARKERS)})"
        )

    if "Net" in selected_modules and not any(
        marker in normalized_text for marker in NET_ARCHIVE_MARKERS
    ):
        errors.append(
            "map does not contain the MicroWorld Net archive "
            f"({', '.join(NET_ARCHIVE_MARKERS)})"
        )

    for module, markers in MODULE_MARKERS.items():
        if module in selected_modules:
            continue
        for marker in markers:
            if marker in normalized_text:
                errors.append(
                    f"{profile} map contains unselected {module} "
                    f"marker: {marker}"
                )

    for marker in required_markers:
        if normalize_map(marker) not in normalized_text:
            errors.append(f"map lacks required marker: {marker}")
    for marker in forbidden_markers:
        if normalize_map(marker) in normalized_text:
            errors.append(f"map contains forbidden marker: {marker}")
    return errors


def run_self_test() -> int:
    """Prove profile evidence passes and absent or outward modules fail."""
    valid_map = ".pio/build/lib123/libmicroworld.a(Actor.o)\n"
    valid_errors = analyze_map(valid_map, "Core", [], [])
    if valid_errors:
        for error in valid_errors:
            print(f"Valid-map self-test failed: {error}", file=sys.stderr)
        return 1

    valid_memory_map = (
        "libmicroworld.a(TickFunction.o)\n"
        "libMicroWorldMemory.a(MemoryResource.o)\n"
    )
    valid_memory_errors = analyze_map(valid_memory_map, "Memory", [], [])
    if valid_memory_errors:
        for error in valid_memory_errors:
            print(
                f"Valid Memory-map self-test failed: {error}",
                file=sys.stderr,
            )
        return 1

    missing_memory_errors = analyze_map(valid_map, "Memory", [], [])
    if not any("Memory archive" in error for error in missing_memory_errors):
        print(
            "Self-test did not detect missing Memory archive evidence.",
            file=sys.stderr,
        )
        return 1

    valid_object_map = (
        f"{valid_memory_map}"
        "libMicroWorldObject.a(ObjectStore.o)\n"
        "MicroWorld::FObjectStore\n"
    )
    valid_object_errors = analyze_map(
        valid_object_map,
        "Object",
        ["MicroWorld::FObjectStore"],
        [],
    )
    if valid_object_errors:
        for error in valid_object_errors:
            print(
                f"Valid Object-map self-test failed: {error}",
                file=sys.stderr,
            )
        return 1

    missing_object_errors = analyze_map(valid_memory_map, "Object", [], [])
    if not any("Object archive" in error for error in missing_object_errors):
        print(
            "Self-test did not detect missing Object archive evidence.",
            file=sys.stderr,
        )
        return 1

    outward_object_map = (
        f"{valid_object_map}"
        "libmicroworld_engine.a(Engine.o)\n"
        "MicroWorld::FEngine\n"
    )
    outward_object_errors = analyze_map(outward_object_map, "Object", [], [])
    if not any(
        "unselected Engine" in error for error in outward_object_errors
    ):
        print(
            "Self-test did not detect Engine code in an Object profile.",
            file=sys.stderr,
        )
        return 1

    outward_memory_map = (
        f"{valid_memory_map}"
        "libmicroworld_object.a(ObjectStore.o)\n"
        "MicroWorld::FObjectStore\n"
    )
    outward_memory_errors = analyze_map(outward_memory_map, "Memory", [], [])
    if not any(
        "unselected Object" in error for error in outward_memory_errors
    ):
        print(
            "Self-test did not detect Object code in a Memory profile.",
            file=sys.stderr,
        )
        return 1

    invalid_map = (
        "microworld.lib\n"
        "libmicroworld_net.a(NetManager.o)\n"
        "MicroWorld::FNetManager\n"
    )
    invalid_errors = analyze_map(invalid_map, "Core", [], [])
    if not any("unselected Net" in error for error in invalid_errors):
        print(
            "Self-test did not detect an unselected Net module.",
            file=sys.stderr,
        )
        return 1

    missing_errors = analyze_map("consumer.exe\n", "Core", [], [])
    if not any("Core archive" in error for error in missing_errors):
        print(
            "Self-test did not detect missing Core archive evidence.",
            file=sys.stderr,
        )
        return 1

    # A valid Core+Net map links the Core, Memory, and Net archives without
    # pulling Object or Engine, proving the Net overlay is independent of them.
    valid_core_net_map = (
        "libmicroworld.a(TickFunction.o)\n"
        "libmicroworld_memory:MemoryResource.obj\n"
        "libmicroworld_net:NetDriver.obj\n"
        "MicroWorld::FNetManager\n"
    )
    valid_core_net_errors = analyze_map(valid_core_net_map, "Core+Net", [], [])
    if valid_core_net_errors:
        for error in valid_core_net_errors:
            print(
                f"Valid Core+Net-map self-test failed: {error}",
                file=sys.stderr,
            )
        return 1

    # A Core+Net map lacking the Net archive must fail, proving header-only
    # evidence cannot satisfy the Net profile.
    missing_net_errors = analyze_map(valid_memory_map, "Core+Net", [], [])
    if not any("Net archive" in error for error in missing_net_errors):
        print(
            "Self-test did not detect missing Net archive evidence.",
            file=sys.stderr,
        )
        return 1

    # A Core+Net map pulling Engine code must fail as an unselected module,
    # proving Net stays independent of the managed runtime.
    outward_core_net_map = (
        f"{valid_core_net_map}"
        "libmicroworld_engine.a(World.o)\n"
        "MicroWorld::UWorld\n"
    )
    outward_core_net_errors = analyze_map(outward_core_net_map, "Core+Net", [], [])
    if not any(
        "unselected Engine" in error for error in outward_core_net_errors
    ):
        print(
            "Self-test did not detect Engine code in a Core+Net profile.",
            file=sys.stderr,
        )
        return 1

    with tempfile.TemporaryDirectory() as temporary_directory:
        map_path = Path(temporary_directory) / "valid.map"
        map_path.write_text(valid_map, encoding="utf-8")
        if not map_path.is_file():
            print("Self-test could not create its map fixture.", file=sys.stderr)
            return 1

    print("Profile-map checker self-test passed.")
    return 0


def main() -> int:
    """Validate arguments, inspect one linker map, and report profile evidence."""
    arguments = parse_arguments()
    if arguments.self_test:
        return run_self_test()
    if arguments.map is None or arguments.profile is None:
        print("--map and --profile are required.", file=sys.stderr)
        return 2
    if not arguments.map.is_file():
        print(f"{arguments.map}: map file does not exist", file=sys.stderr)
        return 2

    text = arguments.map.read_text(encoding="utf-8", errors="replace")
    errors = analyze_map(
        text,
        arguments.profile,
        arguments.require,
        arguments.forbid,
    )
    for error in errors:
        print(f"{arguments.map}: {error}", file=sys.stderr)
    if errors:
        return 1

    print(
        f"{arguments.profile} profile map check passed "
        f"({arguments.map.stat().st_size} bytes)."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
