"""Behavior tests for the native Pico build and upload command helpers."""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))

import pico


class PicoBuildCommandTests(unittest.TestCase):
    """Proves selector-to-target mapping without requiring a local Pico toolchain."""

    def setUp(self) -> None:
        """Isolates generated-artifact checks inside one temporary build directory."""

        self.temporary_directory = tempfile.TemporaryDirectory()
        self.original_build_directory = pico.BUILD_DIRECTORY
        pico.BUILD_DIRECTORY = Path(self.temporary_directory.name)
        self.tools = pico.FBuildTools(
            Path("cmake"),
            Path("git"),
            Path("ninja"),
            Path("arm-none-eabi-g++"),
            Path("elf2uf2"),
        )

    def tearDown(self) -> None:
        """Restores the production artifact location after each isolated test."""

        pico.BUILD_DIRECTORY = self.original_build_directory
        self.temporary_directory.cleanup()

    def test_build_example_targets_only_the_example_firmware(self) -> None:
        """Proves the example selector cannot accidentally build a different firmware target."""

        (pico.BUILD_DIRECTORY / "microworld_pico_core_tick_example.uf2").touch()
        with mock.patch.object(pico, "discover_build_tools", return_value=self.tools), mock.patch.object(
            pico, "configure_build", return_value=0
        ), mock.patch.object(pico, "run_command", return_value=0) as run_command:
            result = pico.build("example")

        self.assertEqual(0, result)
        self.assertEqual(
            ["cmake", "--build", str(pico.BUILD_DIRECTORY), "--target", "microworld_pico_core_tick_example"],
            list(run_command.call_args.args[0]),
        )

    def test_build_lora_targets_only_the_lora_firmware(self) -> None:
        """Proves the LoRa selector requests only its dedicated Pico target."""

        (pico.BUILD_DIRECTORY / "microworld_pico_lora_interop.uf2").touch()
        with mock.patch.object(pico, "discover_build_tools", return_value=self.tools), mock.patch.object(
            pico, "configure_build", return_value=0
        ), mock.patch.object(pico, "run_command", return_value=0) as run_command:
            result = pico.build("lora")

        self.assertEqual(0, result)
        self.assertEqual(
            ["cmake", "--build", str(pico.BUILD_DIRECTORY), "--target", "microworld_pico_lora_interop"],
            list(run_command.call_args.args[0]),
        )

    def test_build_all_requires_each_expected_uf2(self) -> None:
        """Proves the default command fails when its new LoRa artifact is absent."""

        for target in (
            "microworld_pico_freertos_consumer",
            "microworld_pico_core_tick_example",
            "microworld_pico_core_tests",
        ):
            (pico.BUILD_DIRECTORY / f"{target}.uf2").touch()

        with mock.patch.object(pico, "discover_build_tools", return_value=self.tools), mock.patch.object(
            pico, "configure_build", return_value=0
        ), mock.patch.object(pico, "run_command", return_value=0):
            result = pico.build("all")

        self.assertEqual(1, result)


class PicoUploadSafetyTests(unittest.TestCase):
    """Proves upload rejects ambiguous targets and drives before copying any UF2."""

    def setUp(self) -> None:
        """Creates isolated source and destination directories for safe copy-path checks."""

        self.temporary_directory = tempfile.TemporaryDirectory()
        self.temporary_path = Path(self.temporary_directory.name)
        self.original_build_directory = pico.BUILD_DIRECTORY
        pico.BUILD_DIRECTORY = self.temporary_path / "build"
        pico.BUILD_DIRECTORY.mkdir()
        (pico.BUILD_DIRECTORY / "microworld_pico_freertos_consumer.uf2").write_bytes(b"UF2")

    def tearDown(self) -> None:
        """Restores the production build location after each upload safety test."""

        pico.BUILD_DIRECTORY = self.original_build_directory
        self.temporary_directory.cleanup()

    def test_normalize_drive_root_rejects_relative_and_unc_paths(self) -> None:
        """Proves only canonical drive roots reach BOOTSEL validation."""

        self.assertIsNone(pico.normalize_drive_root("E:\\firmware"))
        self.assertIsNone(pico.normalize_drive_root("\\\\server\\share"))
        self.assertEqual(Path("E:\\"), pico.normalize_drive_root("e:"))
        self.assertEqual(Path("E:\\"), pico.normalize_drive_root("E:\\"))

    def test_upload_rejects_the_compile_only_test_selector_before_build(self) -> None:
        """Proves the stack-heavy test image cannot reach tool or drive operations."""

        with mock.patch.object(pico, "build") as build:
            result = pico.upload("tests", None)

        self.assertEqual(2, result)
        build.assert_not_called()

    def test_upload_copies_only_the_selected_probe_uf2_after_validation(self) -> None:
        """Proves an accepted upload performs exactly one copy to the validated destination."""

        destination_directory = self.temporary_path / "RPI-RP2"
        destination_directory.mkdir()
        with mock.patch.object(pico, "build", return_value=0), mock.patch.object(
            pico, "find_bootsel_drive", return_value=destination_directory
        ), mock.patch.object(pico.shutil, "copyfile") as copyfile:
            result = pico.upload("probe", "E:")

        self.assertEqual(0, result)
        copyfile.assert_called_once_with(
            pico.BUILD_DIRECTORY / "microworld_pico_freertos_consumer.uf2",
            destination_directory / "microworld_pico_freertos_consumer.uf2",
        )

    def test_upload_copies_only_the_selected_lora_uf2_after_validation(self) -> None:
        """Proves an accepted LoRa upload copies exactly its own UF2 artifact."""

        lora_uf2 = pico.BUILD_DIRECTORY / "microworld_pico_lora_interop.uf2"
        lora_uf2.write_bytes(b"UF2")
        destination_directory = self.temporary_path / "RPI-RP2"
        destination_directory.mkdir()
        with mock.patch.object(pico, "build", return_value=0), mock.patch.object(
            pico, "find_bootsel_drive", return_value=destination_directory
        ), mock.patch.object(pico.shutil, "copyfile") as copyfile:
            result = pico.upload("lora", "E:")

        self.assertEqual(0, result)
        copyfile.assert_called_once_with(lora_uf2, destination_directory / lora_uf2.name)

    def test_upload_lora_rejects_an_invalid_drive_without_copying(self) -> None:
        """Proves a rejected BOOTSEL drive cannot receive the LoRa image."""

        with mock.patch.object(pico, "build", return_value=0), mock.patch.object(
            pico, "find_bootsel_drive", return_value=None
        ), mock.patch.object(pico.shutil, "copyfile") as copyfile:
            result = pico.upload("lora", "E:")

        self.assertEqual(1, result)
        copyfile.assert_not_called()

    def test_automatic_discovery_rejects_multiple_validated_drives(self) -> None:
        """Proves automatic upload refuses to guess between multiple BOOTSEL volumes."""

        first_drive = self.temporary_path / "first"
        second_drive = self.temporary_path / "second"
        with mock.patch.object(pico, "iter_drive_roots", return_value=(first_drive, second_drive)), mock.patch.object(
            pico, "validate_bootsel_drive", side_effect=(first_drive, second_drive)
        ):
            discovered_drive = pico.find_bootsel_drive(None)

        self.assertIsNone(discovered_drive)


if __name__ == "__main__":
    unittest.main()
