"""Static contract tests for the cross-platform build-script interface."""

from __future__ import annotations

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


class BuildScriptTests(unittest.TestCase):
    def test_windows_script_supports_release_and_debug_profiles(self):
        script = (REPO_ROOT / "build.bat").read_text(encoding="utf-8")

        self.assertIn("[release|debug] [build|config|only|clean]", script)
        self.assertIn('set "BUILD_DIR=build-debug"', script)
        self.assertIn('set "BUILD_TYPE=Debug"', script)
        self.assertIn('set "BUILD_DIR=build"', script)
        self.assertIn('set "BUILD_TYPE=Release"', script)

    def test_posix_script_supports_release_and_debug_profiles(self):
        script = (REPO_ROOT / "build.sh").read_text(encoding="utf-8")

        self.assertIn("[release|debug] [build|config|only|clean]", script)
        self.assertIn('BUILD_DIR="build-debug"', script)
        self.assertIn('BUILD_TYPE="Debug"', script)
        self.assertIn('BUILD_DIR="build"', script)
        self.assertIn('BUILD_TYPE="Release"', script)


if __name__ == "__main__":
    unittest.main()
