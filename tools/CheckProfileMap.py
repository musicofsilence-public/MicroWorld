#!/usr/bin/env python3
"""Verify that a MicroWorld profile map contains no unselected modules."""

from __future__ import annotations

import argparse
import sys
import tempfile
from pathlib import Path


# Profile names describe package bundles. Transport is an independent Core-only
# overlay; Networking depends on Core and Messaging, never Transport.
PROFILE_MODULES = {
    "Core": {"Core"},
    "Core+Messaging": {"Core", "Messaging"},
    "Core+Messaging+Networking": {"Core", "Messaging", "Networking"},
    "Core+Transport": {"Core", "Transport"},
    "Application": {"Core", "Messaging", "Networking", "Engine", "Application"},
}

# Markers cover planned CMake target/archive names, PlatformIO package archives,
# public include paths, and characteristic public symbols. Serialization,
# Integration, and Messaging stay listed so any accidental linkage is still
# detected even though no active profile selects them. Memory's former markers (fmemoryresource,
# tfixedarena, tsharedptr) are intentionally absent: those symbols now live in
# the Core archive after the fold.
MODULE_MARKERS = {
    "Engine": (
        "microworld_engine",
        "microworld-engine",
        "/microworld/engine/",
        "fengine",
        "uworld",
        "aactor",
        "uactorcomponent",
    ),
    "Messaging": (
        "microworld_messaging",
        "microworld-messaging",
        "/microworld/messaging/",
        "tmessagerouter",
        "treliablechannel",
    ),
    "Serialization": (
        "microworld_serialization",
        "microworld-serialization",
        "/microworld/serialization/",
        "fbytearchive",
    ),
    "Transport": (
        "microworld_transport",
        "microworld-transport",
        "/microworld/transport/",
        "ttransportmanager",
        "idevice",
    ),
    "Networking": (
        "microworld_networking",
        "microworld-networking",
        "/microworld/networking/",
        "fnetworksystem",
    ),
    "Application": (
        "microworld_application",
        "microworld-application",
        "/microworld/application/",
        "fapplication",
        "tapplicationrunner",
    ),
}

# A map must prove that the released physical Core archive participated, not
# merely contain the executable target's MicroWorld-shaped name.
CORE_ARCHIVE_MARKERS = (
    "microworld:",
    "microworld.lib",
    "libmicroworld.a",
)

# Transport profiles must link their separate package archive. Header-only byte I/O
# evidence does not prove that the compiled device sources participated.
TRANSPORT_ARCHIVE_MARKERS = (
    "microworld_transport:",
    "microworld_transport.lib",
    "libmicroworld_transport.a",
    "libmicroworld-transport.a",
    "libmicroworldtransport.a",
)

# Application profiles must link their separate package archive. The engine-
# binding header alone does not prove that the Application lifecycle state
# machine participated.
APPLICATION_ARCHIVE_MARKERS = (
    "microworld_application:",
    "microworld_application.lib",
    "libmicroworld_application.a",
    "libmicroworld-application.a",
    "libmicroworldapplication.a",
)

NETWORKING_ARCHIVE_MARKERS = (
    "microworld_networking:",
    "microworld_networking.lib",
    "libmicroworld_networking.a",
    "libmicroworld-networking.a",
    "libmicroworldnetworking.a",
)

MESSAGING_ARCHIVE_MARKERS = (
    "microworld_messaging:",
    "microworld_messaging.lib",
    "libmicroworld_messaging.a",
    "libmicroworld-messaging.a",
    "libmicroworldmessaging.a",
)

ENGINE_ARCHIVE_MARKERS = (
    "microworld_engine:",
    "microworld_engine.lib",
    "libmicroworld_engine.a",
    "libmicroworld-engine.a",
    "libmicroworldengine.a",
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
    if "Messaging" in selected_modules and not any(
        marker in normalized_text for marker in MESSAGING_ARCHIVE_MARKERS
    ):
        errors.append(
            "map does not contain the MicroWorld Messaging archive "
            f"({', '.join(MESSAGING_ARCHIVE_MARKERS)})"
        )

    if "Engine" in selected_modules and not any(
        marker in normalized_text for marker in ENGINE_ARCHIVE_MARKERS
    ):
        errors.append(
            "map does not contain the MicroWorld Engine archive "
            f"({', '.join(ENGINE_ARCHIVE_MARKERS)})"
        )

    if "Transport" in selected_modules and not any(
        marker in normalized_text for marker in TRANSPORT_ARCHIVE_MARKERS
    ):
        errors.append(
            "map does not contain the MicroWorld Transport archive "
            f"({', '.join(TRANSPORT_ARCHIVE_MARKERS)})"
        )

    if "Networking" in selected_modules and not any(
        marker in normalized_text for marker in NETWORKING_ARCHIVE_MARKERS
    ):
        errors.append(
            "map does not contain the MicroWorld Networking archive "
            f"({', '.join(NETWORKING_ARCHIVE_MARKERS)})"
        )

    if "Application" in selected_modules and not any(
        marker in normalized_text for marker in APPLICATION_ARCHIVE_MARKERS
    ):
        errors.append(
            "map does not contain the MicroWorld Application archive "
            f"({', '.join(APPLICATION_ARCHIVE_MARKERS)})"
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
    # Memory folded into Core, so a Core profile map links the Core archive only.
    valid_core_map = "libmicroworld.a(TickFunction.o)\n"
    valid_errors = analyze_map(valid_core_map, "Core", [], [])
    if valid_errors:
        for error in valid_errors:
            print(f"Valid Core-map self-test failed: {error}", file=sys.stderr)
        return 1

    invalid_map = (
        "microworld.lib\n"
        "libmicroworld_transport.a(TransportManager.o)\n"
        "MicroWorld::Transport::TTransportManager\n"
    )
    invalid_errors = analyze_map(invalid_map, "Core", [], [])
    if not any("unselected Transport" in error for error in invalid_errors):
        print(
            "Self-test did not detect an unselected Transport module.",
            file=sys.stderr,
        )
        return 1

    valid_messaging_map = (
        f"{valid_core_map}"
        "libmicroworld_messaging.a(MessagingSystem.o)\n"
        "MicroWorld::Messaging::FMessagingSystem\n"
    )
    valid_messaging_errors = analyze_map(valid_messaging_map, "Core+Messaging", [], [])
    if valid_messaging_errors:
        for error in valid_messaging_errors:
            print(f"Valid Core+Messaging-map self-test failed: {error}", file=sys.stderr)
        return 1

    missing_messaging_errors = analyze_map(valid_core_map, "Core+Messaging", [], [])
    if not any("Messaging archive" in error for error in missing_messaging_errors):
        print("Self-test did not detect missing Messaging archive evidence.", file=sys.stderr)
        return 1

    valid_networking_map = (
        f"{valid_messaging_map}"
        "libmicroworld_networking.a(NetworkSystem.o)\n"
        "MicroWorld::Networking::FNetworkSystem\n"
    )
    valid_networking_errors = analyze_map(valid_networking_map, "Core+Messaging+Networking", [], [])
    if valid_networking_errors:
        for error in valid_networking_errors:
            print(f"Valid Networking-map self-test failed: {error}", file=sys.stderr)
        return 1

    missing_networking_errors = analyze_map(valid_messaging_map, "Core+Messaging+Networking", [], [])
    if not any("Networking archive" in error for error in missing_networking_errors):
        print("Self-test did not detect missing Networking archive evidence.", file=sys.stderr)
        return 1

    valid_application_map = (
        f"{valid_networking_map}"
        "libmicroworld_engine.a(EngineHost.o)\n"
        "libmicroworld_application.a(Application.o)\n"
        "MicroWorld::Application::FApplication\n"
    )
    valid_application_errors = analyze_map(valid_application_map, "Application", [], [])
    if valid_application_errors:
        for error in valid_application_errors:
            print(f"Valid Application-map self-test failed: {error}", file=sys.stderr)
        return 1

    missing_application_errors = analyze_map(valid_networking_map, "Application", [], [])
    if not any("Engine archive" in error for error in missing_application_errors) or not any(
        "Application archive" in error for error in missing_application_errors
    ):
        print("Self-test did not detect missing Application profile archive evidence.", file=sys.stderr)
        return 1

    missing_errors = analyze_map("consumer.exe\n", "Core", [], [])
    if not any("Core archive" in error for error in missing_errors):
        print(
            "Self-test did not detect missing Core archive evidence.",
            file=sys.stderr,
        )
        return 1

    # A valid Core+Transport map links the Core and Transport archives without pulling
    # Object or Engine, proving the Transport overlay is independent of them.
    valid_core_transport_map = (
        "libmicroworld.a(TickFunction.o)\n"
        "libmicroworld_transport:Device.obj\n"
        "MicroWorld::Transport::TTransportManager\n"
    )
    valid_core_transport_errors = analyze_map(valid_core_transport_map, "Core+Transport", [], [])
    if valid_core_transport_errors:
        for error in valid_core_transport_errors:
            print(
                f"Valid Core+Transport-map self-test failed: {error}",
                file=sys.stderr,
            )
        return 1

    # A Core+Transport map lacking the Transport archive must fail, proving header-only
    # evidence cannot satisfy the Transport profile.
    missing_transport_errors = analyze_map(valid_core_map, "Core+Transport", [], [])
    if not any("Transport archive" in error for error in missing_transport_errors):
        print(
            "Self-test did not detect missing Transport archive evidence.",
            file=sys.stderr,
        )
        return 1

    # A Core+Transport map pulling Engine code must fail as an unselected module,
    # proving Transport stays independent of the managed runtime.
    outward_core_transport_map = (
        f"{valid_core_transport_map}"
        "libmicroworld_engine.a(World.o)\n"
        "MicroWorld::Engine::UWorld\n"
    )
    outward_core_transport_errors = analyze_map(outward_core_transport_map, "Core+Transport", [], [])
    if not any(
        "unselected Engine" in error for error in outward_core_transport_errors
    ):
        print(
            "Self-test did not detect Engine code in a Core+Transport profile.",
            file=sys.stderr,
        )
        return 1

    with tempfile.TemporaryDirectory() as temporary_directory:
        map_path = Path(temporary_directory) / "valid.map"
        map_path.write_text(valid_core_map, encoding="utf-8")
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
