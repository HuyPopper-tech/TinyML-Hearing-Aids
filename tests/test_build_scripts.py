"""Static contract tests for the cross-platform build-script interface."""

from __future__ import annotations

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


class BuildScriptTests(unittest.TestCase):
    def test_windows_script_supports_release_and_debug_profiles(self):
        script = (REPO_ROOT / "build.bat").read_text(encoding="utf-8")

        self.assertIn("[release|debug|pf01_uart] [build|config|only|clean]", script)
        self.assertIn('set "BUILD_DIR=build-debug"', script)
        self.assertIn('set "BUILD_TYPE=Debug"', script)
        self.assertIn('set "BUILD_DIR=build"', script)
        self.assertIn('set "BUILD_TYPE=Release"', script)

    def test_windows_script_builds_the_pf01_uart_firmware_in_its_own_directory(self):
        script = (REPO_ROOT / "build.bat").read_text(encoding="utf-8")

        self.assertIn('"pf01_uart"', script)
        self.assertIn('set "BUILD_DIR=build-pf01-uart"', script)
        self.assertIn('set "FIRMWARE_MODE=pf01_uart"', script)
        self.assertIn('-DM55M1_FIRMWARE_MODE=%FIRMWARE_MODE%', script)

    def test_windows_script_removes_the_trailing_script_directory_separator_before_cmake(self):
        script = (REPO_ROOT / "build.bat").read_text(encoding="utf-8")

        self.assertIn('if "%PROJ_ROOT:~-1%"=="\\" set "PROJ_ROOT=%PROJ_ROOT:~0,-1%"', script)
        self.assertIn('set "BUILD_PATH=%PROJ_ROOT%\\%BUILD_DIR%"', script)
        self.assertIn('set "TC=%PROJ_ROOT%\\cmake\\armclang.cmake"', script)

    def test_posix_script_supports_release_and_debug_profiles(self):
        script = (REPO_ROOT / "build.sh").read_text(encoding="utf-8")

        self.assertIn("[release|debug|pf01_uart] [build|config|only|clean]", script)
        self.assertIn('BUILD_DIR="build-debug"', script)
        self.assertIn('BUILD_TYPE="Debug"', script)
        self.assertIn('BUILD_DIR="build"', script)
        self.assertIn('BUILD_TYPE="Release"', script)

    def test_posix_script_builds_the_pf01_uart_firmware_in_its_own_directory(self):
        script = (REPO_ROOT / "build.sh").read_text(encoding="utf-8")

        self.assertIn('release|debug|pf01_uart', script)
        self.assertIn('BUILD_DIR="build-pf01-uart"', script)
        self.assertIn('FIRMWARE_MODE="pf01_uart"', script)
        self.assertIn('-DM55M1_FIRMWARE_MODE="${FIRMWARE_MODE}"', script)

    def test_pf01_uart_build_directory_is_ignored(self):
        gitignore = (REPO_ROOT / ".gitignore").read_text(encoding="utf-8")

        self.assertIn("build-pf01-uart/", gitignore)


if __name__ == "__main__":
    unittest.main()
