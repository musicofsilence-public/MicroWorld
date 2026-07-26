"""Build and safely upload native Raspberry Pi Pico MicroWorld artifacts."""

from __future__ import annotations

import ctypes
import os
import re
import shutil
import string
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Mapping, Optional, Sequence


SCRIPT_DIRECTORY = Path(__file__).resolve().parent
BUILD_DIRECTORY = SCRIPT_DIRECTORY / "build"
DRIVE_ROOT_PATTERN = re.compile(r"^[A-Za-z]:(?:\\)?$")


@dataclass(frozen=True)
class FArtifactTarget:
    """Connects one command selector to its exact CMake target and UF2 artifact."""

    cmake_target: str
    uf2_stem: str
    b_uploadable: bool


ARTIFACT_TARGETS: Mapping[str, FArtifactTarget] = {
    "probe": FArtifactTarget("microworld_pico_freertos_consumer", "microworld_pico_freertos_consumer", True),
    "example": FArtifactTarget("microworld_pico_core_tick_example", "microworld_pico_core_tick_example", True),
    "tests": FArtifactTarget("microworld_pico_core_tests", "microworld_pico_core_tests", False),
    "lora": FArtifactTarget("microworld_pico_lora_interop", "microworld_pico_lora_interop", True),
}


@dataclass(frozen=True)
class FBuildTools:
    """Owns the resolved host executables needed for one native Pico build."""

    cmake: Path
    git: Path
    ninja: Path
    compiler: Path
    elf2uf2: Path


def print_usage() -> None:
    """Explains the bounded command surface without performing any build or drive I/O."""

    print("Usage:")
    print("  pico.bat build [probe|example|tests|lora|all]")
    print("  pico.bat upload <probe|example|lora> [--drive X:]")


def normalize_selector(selector: str, *, allow_all: bool) -> Optional[str]:
    """Validates one artifact selector before any tool or filesystem work begins."""

    if selector in ARTIFACT_TARGETS or (allow_all and selector == "all"):
        return selector

    print(f"Unknown artifact selector: {selector}", file=sys.stderr)
    return None


def package_candidates(package_name: str, relative_paths: Sequence[str]) -> Iterable[Path]:
    """Yields PlatformIO-local tool locations without committing user-specific paths."""

    package_root = Path.home() / ".platformio" / "packages" / package_name
    for relative_path in relative_paths:
        yield package_root / relative_path


def find_tool(path_names: Sequence[str], package_name: str, package_relative_paths: Sequence[str]) -> Optional[Path]:
    """Finds one executable through PATH first, then the normal PlatformIO package cache."""

    for path_name in path_names:
        resolved_path = shutil.which(path_name)
        if resolved_path is not None:
            return Path(resolved_path)

    for candidate_path in package_candidates(package_name, package_relative_paths):
        if candidate_path.is_file():
            return candidate_path

    return None


def discover_build_tools() -> Optional[FBuildTools]:
    """Resolves all host tools before CMake mutates the consumer-local build tree."""

    cmake = find_tool(("cmake.exe", "cmake"), "tool-cmake", ("bin/cmake.exe",))
    git = find_tool(("git.exe", "git"), "tool-git", ("bin/git.exe",))
    ninja = find_tool(("ninja.exe", "ninja"), "tool-ninja", ("ninja.exe", "bin/ninja.exe"))
    compiler = find_tool(
        ("arm-none-eabi-g++.exe", "arm-none-eabi-g++"),
        "toolchain-gccarmnoneeabi",
        ("bin/arm-none-eabi-g++.exe",),
    )
    elf2uf2 = find_tool(("elf2uf2.exe", "elf2uf2"), "tool-rp2040tools", ("elf2uf2.exe", "bin/elf2uf2.exe"))

    missing_tools = [
        tool_name
        for tool_name, tool_path in (
            ("CMake", cmake),
            ("Git", git),
            ("Ninja", ninja),
            ("GNU Arm C++", compiler),
            ("elf2uf2", elf2uf2),
        )
        if tool_path is None
    ]
    if missing_tools:
        print(f"Missing required tools: {', '.join(missing_tools)}", file=sys.stderr)
        return None

    return FBuildTools(cmake=cmake, git=git, ninja=ninja, compiler=compiler, elf2uf2=elf2uf2)


def run_command(arguments: Sequence[str], working_directory: Path) -> int:
    """Runs one explicit tool invocation and preserves its process result."""

    completed_process = subprocess.run(list(arguments), cwd=working_directory, check=False)
    return completed_process.returncode


def configure_build(tools: FBuildTools) -> int:
    """Configures the consumer-local Ninja build with the resolved Arm toolchain."""

    BUILD_DIRECTORY.mkdir(parents=True, exist_ok=True)
    toolchain_root = tools.compiler.parent.parent
    arguments = (
        str(tools.cmake),
        "-S",
        str(SCRIPT_DIRECTORY),
        "-B",
        str(BUILD_DIRECTORY),
        "-G",
        "Ninja",
        f"-DCMAKE_MAKE_PROGRAM={tools.ninja}",
        f"-DCMAKE_PROGRAM_PATH={tools.git.parent}",
        f"-DMICROWORLD_ELF2UF2={tools.elf2uf2}",
        f"-DPICO_TOOLCHAIN_PATH={toolchain_root}",
    )
    return run_command(arguments, SCRIPT_DIRECTORY)


def artifact_path(selector: str) -> Path:
    """Returns the unique UF2 produced for one already-validated selector."""

    return BUILD_DIRECTORY / f"{ARTIFACT_TARGETS[selector].uf2_stem}.uf2"


def selected_build_targets(selector: str) -> Sequence[FArtifactTarget]:
    """Expands the build-only all selector without permitting it for upload."""

    if selector == "all":
        return tuple(ARTIFACT_TARGETS.values())

    return (ARTIFACT_TARGETS[selector],)


def build(selector: str) -> int:
    """Configures and builds the requested artifact set, then proves each UF2 exists."""

    validated_selector = normalize_selector(selector, allow_all=True)
    if validated_selector is None:
        return 2

    tools = discover_build_tools()
    if tools is None:
        return 1

    configure_result = configure_build(tools)
    if configure_result != 0:
        return configure_result

    build_arguments = [str(tools.cmake), "--build", str(BUILD_DIRECTORY)]
    if validated_selector != "all":
        build_arguments.extend(("--target", ARTIFACT_TARGETS[validated_selector].cmake_target))

    build_result = run_command(build_arguments, SCRIPT_DIRECTORY)
    if build_result != 0:
        return build_result

    missing_artifacts = [
        f"{target.uf2_stem}.uf2"
        for target in selected_build_targets(validated_selector)
        if not (BUILD_DIRECTORY / f"{target.uf2_stem}.uf2").is_file()
    ]
    if missing_artifacts:
        print(f"Missing expected UF2 artifact(s): {', '.join(missing_artifacts)}", file=sys.stderr)
        return 1

    return 0


def normalize_drive_root(value: str) -> Optional[Path]:
    """Accepts only a Windows drive root before any BOOTSEL validation reads occur."""

    if DRIVE_ROOT_PATTERN.fullmatch(value) is None:
        return None

    return Path(f"{value[0].upper()}:\\")


def get_volume_label(drive_root: Path) -> Optional[str]:
    """Reads one Windows drive label through the Win32 API without writing to the volume."""

    if os.name != "nt":
        return None

    volume_name = ctypes.create_unicode_buffer(261)
    success = ctypes.windll.kernel32.GetVolumeInformationW(
        str(drive_root), volume_name, len(volume_name), None, None, None, None, 0
    )
    if success == 0:
        return None

    return volume_name.value


def validate_bootsel_drive(drive_root: Path) -> Optional[Path]:
    """Accepts only an existing RPI-RP2 volume with matching UF2 board identity."""

    if not drive_root.exists() or get_volume_label(drive_root) != "RPI-RP2":
        return None

    board_id_path = drive_root / "INFO_UF2.TXT"
    try:
        board_id_contents = board_id_path.read_text(encoding="utf-8")
    except OSError:
        return None

    if "Board-ID: RPI-RP2" not in board_id_contents:
        return None

    return drive_root


def iter_drive_roots() -> Iterable[Path]:
    """Enumerates drive-letter roots for read-only automatic BOOTSEL detection."""

    for drive_letter in string.ascii_uppercase:
        yield Path(f"{drive_letter}:\\")


def find_bootsel_drive(explicit_drive: Optional[str]) -> Optional[Path]:
    """Selects one validated BOOTSEL root or rejects ambiguity without copying a UF2."""

    if explicit_drive is not None:
        normalized_drive = normalize_drive_root(explicit_drive)
        if normalized_drive is None:
            print("--drive must be a drive root such as E: or E:\\", file=sys.stderr)
            return None

        validated_drive = validate_bootsel_drive(normalized_drive)
        if validated_drive is None:
            print(f"{normalized_drive} is not a validated RPI-RP2 BOOTSEL volume", file=sys.stderr)
        return validated_drive

    validated_drives = [
        validated_drive
        for drive_root in iter_drive_roots()
        if (validated_drive := validate_bootsel_drive(drive_root)) is not None
    ]
    if len(validated_drives) != 1:
        print(f"Expected exactly one RPI-RP2 BOOTSEL volume; found {len(validated_drives)}", file=sys.stderr)
        return None

    return validated_drives[0]


def upload(selector: str, explicit_drive: Optional[str]) -> int:
    """Builds one runnable UF2 and copies it only after read-only BOOTSEL validation."""

    validated_selector = normalize_selector(selector, allow_all=False)
    if validated_selector is None or not ARTIFACT_TARGETS[validated_selector].b_uploadable:
        print("upload accepts only the probe, example, or lora selector", file=sys.stderr)
        return 2

    build_result = build(validated_selector)
    if build_result != 0:
        return build_result

    uf2_path = artifact_path(validated_selector)
    upload_drive = find_bootsel_drive(explicit_drive)
    if upload_drive is None:
        return 1

    try:
        shutil.copyfile(uf2_path, upload_drive / uf2_path.name)
    except OSError as error:
        print(f"UF2 copy failed: {error}", file=sys.stderr)
        return 1

    print(f"Copied {uf2_path.name} to {upload_drive}")
    return 0


def parse_upload_arguments(arguments: Sequence[str]) -> Optional[tuple[str, Optional[str]]]:
    """Validates upload syntax before the command can configure, build, or inspect drives."""

    if not arguments:
        return None

    selector = arguments[0]
    if len(arguments) == 1:
        return selector, None
    if len(arguments) == 3 and arguments[1] == "--drive":
        return selector, arguments[2]

    return None


def main(arguments: Sequence[str]) -> int:
    """Dispatches one bounded command and leaves all errors as explicit exit codes."""

    if not arguments:
        print_usage()
        return 2

    command = arguments[0]
    command_arguments = arguments[1:]
    if command == "build":
        if len(command_arguments) > 1:
            print_usage()
            return 2

        return build(command_arguments[0] if command_arguments else "all")

    if command == "upload":
        upload_arguments = parse_upload_arguments(command_arguments)
        if upload_arguments is None:
            print_usage()
            return 2

        return upload(*upload_arguments)

    print(f"Unknown command: {command}", file=sys.stderr)
    print_usage()
    return 2


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
