"""Static safety checks for the checked-in VS Code and CMake debug setup."""

from __future__ import annotations

import json
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


class DebugConfigurationTests(unittest.TestCase):
    def test_debug_preset_keeps_debug_symbols_in_a_separate_build_directory(self):
        presets = json.loads((REPO_ROOT / "CMakePresets.json").read_text(encoding="utf-8"))
        preset = next(item for item in presets["configurePresets"] if item["name"] == "m55m1-debug")

        self.assertEqual(preset["binaryDir"], "${sourceDir}/build-debug")
        self.assertEqual(preset["cacheVariables"]["CMAKE_BUILD_TYPE"], "Debug")
        self.assertEqual(preset["cacheVariables"]["CMAKE_TOOLCHAIN_FILE"], "${sourceDir}/cmake/armclang.cmake")

    def test_armclang_toolchain_forces_dwarf_symbols_for_debug_builds(self):
        toolchain = (REPO_ROOT / "cmake" / "armclang.cmake").read_text(encoding="utf-8")

        self.assertIn('set(CMAKE_C_FLAGS_DEBUG "-g" CACHE STRING', toolchain)
        self.assertIn('set(CMAKE_CXX_FLAGS_DEBUG "-g" CACHE STRING', toolchain)

    def test_armclang_toolchain_uses_cmake_armclang_support(self):
        toolchain = (REPO_ROOT / "cmake" / "armclang.cmake").read_text(encoding="utf-8")

        self.assertIn('set(CMAKE_C_COMPILER_TARGET     arm-arm-none-eabi)', toolchain)
        self.assertIn('set(CMAKE_CXX_COMPILER_TARGET   arm-arm-none-eabi)', toolchain)
        self.assertIn('set(CMAKE_C_COMPILER_ID         ARMClang)', toolchain)
        self.assertIn('set(CMAKE_CXX_COMPILER_ID       ARMClang)', toolchain)
        self.assertIn('set(CMAKE_C_COMPILER_ID_RUN     TRUE)', toolchain)
        self.assertIn('set(CMAKE_CXX_COMPILER_ID_RUN   TRUE)', toolchain)
        self.assertNotIn('set(CMAKE_C_LINK_EXECUTABLE', toolchain)
        self.assertNotIn('set(CMAKE_C_ARCHIVE_CREATE', toolchain)

    def test_cortex_debug_attach_is_flash_safe_and_launch_uses_the_same_override(self):
        launch = json.loads((REPO_ROOT / ".vscode" / "launch.json").read_text(encoding="utf-8"))
        configs = {item["name"]: item for item in launch["configurations"]}
        attach = configs["M55M1: Attach (WCH-Link)"]
        launch_verified = configs["M55M1: Launch (WCH-Link, verified)"]

        self.assertEqual((attach["type"], attach["request"], attach["servertype"]), ("cortex-debug", "attach", "pyocd"))
        self.assertEqual(attach["device"], "m55m1r2ljae")
        self.assertNotIn("boardId", attach)
        self.assertEqual(
            attach["serverArgs"][0:2],
            ["--probe", "0001A0000001"],
        )
        self.assertEqual(
            attach["serverArgs"][2:4],
            ["--target", "m55m1r2ljae"],
        )
        self.assertIn("--connect", attach["serverArgs"])
        self.assertEqual(attach["serverArgs"][attach["serverArgs"].index("--connect") + 1], "attach")
        self.assertNotIn("postAttachCommands", attach)
        self.assertEqual(
            attach["postStartSessionCommands"],
            ["monitor reset halt sysresetreq", "tbreak main", "continue"],
        )
        self.assertIn("--script", attach["serverArgs"])
        self.assertIn("${workspaceFolder}/tools/pyocd/m55m1r2ljae.py", attach["serverArgs"])
        self.assertNotIn("loadFiles", attach)
        self.assertEqual(launch_verified["request"], "launch")
        self.assertEqual(launch_verified["serverArgs"], attach["serverArgs"])


if __name__ == "__main__":
    unittest.main()
