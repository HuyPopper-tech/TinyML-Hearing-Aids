"""Static safety checks for the checked-in VS Code and CMake debug setup."""

from __future__ import annotations

import json
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


class DebugConfigurationTests(unittest.TestCase):
    def test_release_preset_uses_the_canonical_build_directory(self):
        presets = json.loads((REPO_ROOT / "CMakePresets.json").read_text(encoding="utf-8"))
        configure = {
            item["name"]: item for item in presets["configurePresets"]
        }
        builds = {item["name"]: item for item in presets["buildPresets"]}

        release = configure["m55m1-release"]
        self.assertEqual(release["binaryDir"], "${sourceDir}/build")
        self.assertEqual(release["cacheVariables"]["CMAKE_BUILD_TYPE"], "Release")
        self.assertEqual(
            release["cacheVariables"]["CMAKE_TOOLCHAIN_FILE"],
            "${sourceDir}/cmake/armclang.cmake",
        )
        self.assertEqual(
            builds["m55m1-release"]["configurePreset"],
            "m55m1-release",
        )

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

    def test_cortex_debug_uses_nuvoton_openocd_and_separates_attach_from_flash(self):
        launch_path = REPO_ROOT / ".vscode" / "launch.json"
        launch = json.loads(launch_path.read_text(encoding="utf-8"))
        configs = {item["name"]: item for item in launch["configurations"]}
        self.assertEqual(len(configs), 5)

        for config in configs.values():
            self.assertEqual(config["type"], "cortex-debug")
            self.assertEqual(config["servertype"], "openocd")
            self.assertEqual(config["configFiles"], ["${workspaceFolder}/tools/openocd/m55m1-ch549.cfg"])
            self.assertEqual(config["serverpath"], "${workspaceFolder}/tools/openocd/.runtime/bin/openocd.exe")
            self.assertNotIn("shutdown", json.dumps(config))
            self.assertNotIn("poll off", json.dumps(config))

        for prefix, image, task in (
            ("Debug: ", "build-debug/M55M1_GestureAI", "M55M1: Build Debug"),
            ("Release: ", "build/M55M1_GestureAI", "M55M1: Build Release"),
        ):
            attach = configs[f"M55M1: {prefix}Attach (OpenOCD Nuvoton)"]
            flash = configs[f"M55M1: {prefix}Flash + Debug (OpenOCD Nuvoton)"]
            with self.subTest(image=image):
                self.assertEqual(attach["request"], "attach")
                self.assertEqual(attach["overrideAttachCommands"], ["monitor reset halt"])
                self.assertNotIn("preLaunchTask", attach)
                self.assertEqual(attach["postStartSessionCommands"], ["thbreak main", "continue"])
                self.assertNotIn("runToEntryPoint", attach, "Cortex-Debug ignores it for Attach")
                self.assertNotIn("postAttachCommands", attach, "Resume only after session startup")
                self.assertEqual(attach["executable"], f"${{workspaceFolder}}/{image}.axf")
                self.assertEqual(flash["request"], "launch")
                self.assertEqual(flash["preLaunchTask"], task)
                self.assertEqual(flash["executable"], attach["executable"])
                self.assertEqual(flash["loadFiles"], [])
                self.assertEqual(flash["preLaunchCommands"], ["set remotetimeout 60"])
                self.assertEqual(flash["overrideLaunchCommands"], [
                    f"monitor m55m1_program {{{image}.bin}}", "monitor reset halt",
                ])
                self.assertEqual(flash["runToEntryPoint"], "main")

        probe = configs["M55M1: Release Probe: Flash + Debug (OpenOCD Nuvoton)"]
        release = configs["M55M1: Release: Flash + Debug (OpenOCD Nuvoton)"]
        self.assertEqual(probe, dict(release, name=probe["name"],
                                     preLaunchTask="M55M1: Build Release Probe"))

    def test_probe_build_enables_instrumentation_after_product_configure(self):
        task_file = json.loads((REPO_ROOT / ".vscode/tasks.json").read_text(encoding="utf-8"))
        tasks = {task["label"]: task for task in task_file["tasks"]}
        configure = tasks["M55M1: Configure Release Probe"]
        self.assertEqual(configure["dependsOn"], "M55M1: Configure Release")
        self.assertEqual(configure["command"], "cmake")
        self.assertEqual(configure["args"], ["-S", "${workspaceFolder}",
            "-B", "${workspaceFolder}/build", "-DM55M1_AUDIO_RELEASE_PROBE=ON"])
        build = tasks["M55M1: Build Release Probe"]
        self.assertEqual(build["dependsOn"], configure["label"])
        self.assertEqual(build["command"], "cmd.exe")
        self.assertEqual(build["args"], ["/d", "/c", "build.bat release only"])
        self.assertEqual(tasks["M55M1: Configure Release"]["args"][-1], "build.bat release config")
        self.assertEqual(tasks["M55M1: Build Release"]["args"][-1], "build.bat release build")

    def test_flash_tasks_build_matching_images_and_the_server_stays_running(self):
        launch = json.loads((REPO_ROOT / ".vscode/launch.json").read_text(encoding="utf-8"))
        task_file = json.loads((REPO_ROOT / ".vscode/tasks.json").read_text(encoding="utf-8"))
        tasks = {task["label"]: task for task in task_file["tasks"]}
        for config in launch["configurations"]:
            if "preLaunchTask" in config:
                self.assertIn(config["preLaunchTask"], tasks)
        for task in tasks.values():
            if "dependsOn" in task:
                self.assertIn(task["dependsOn"], tasks)
        for profile, directory in [("Debug", "build-debug"), ("Release", "build"),
                                   ("Release Probe", "build")]:
            task = tasks[f"M55M1: Flash {profile} (APROM, verify)"]
            self.assertEqual(task["dependsOn"], f"M55M1: Build {profile}")
            self.assertIn(f"m55m1_program {{${{workspaceFolder}}/{directory}/M55M1_GestureAI.bin}}", task["args"])
            self.assertEqual(task["args"][-2:], ["-c", "shutdown"])
        server = tasks["M55M1: OpenOCD gdbserver"]
        self.assertNotIn("shutdown", json.dumps(server))
        self.assertNotIn("poll off", json.dumps(server))
        self.assertNotIn("pyocd", json.dumps(task_file).lower())
        self.assertNotIn("pyocd", json.dumps(launch).lower())


if __name__ == "__main__":
    unittest.main()
