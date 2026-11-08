"""The custom product has one application and two matching build profiles."""

import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class FirmwareFlowContractTests(unittest.TestCase):
    def test_only_the_product_entrypoint_and_build_roots_remain(self):
        sources = {p.relative_to(ROOT / "app").as_posix()
                   for p in (ROOT / "app").rglob("*.c")}
        self.assertEqual(sources, {"main.c"})
        cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn("${PROJ_ROOT}/app/main.c", cmake)
        self.assertIn('OUTPUT_BIN "${CMAKE_BINARY_DIR}/M55M1_GestureAI.bin"', cmake)
        for obsolete in ("BUILD_CUSTOM_DMIC_TEST", "BUILD_NUMAKER_DMIC_TEST",
                         "BUILD_UART_GPIO_TEST", "BUILD_ETHOSU_SMOKE", "test_compat"):
            self.assertNotIn(obsolete, cmake)
        for obsolete in ("app/custom_clock", "app/custom_i2s", "app/custom_passthrough",
                         "app/test_custom_dmic_main.c", "cmake/custom_clock",
                         "cmake/custom_i2s", "cmake/custom_passthrough",
                         "cmake/custom_dmic_test.cmake", "services/clock_diagnostic",
                         "services/dmic_diagnostic", "services/custom_passthrough",
                         "services/i2s_sweep", "services/enhancement_passthrough",
                         "drivers/custom_i2s_output"):
            self.assertFalse((ROOT / obsolete).exists(), obsolete)

    def test_profiles_and_wrappers_select_the_custom_product(self):
        presets = json.loads((ROOT / "CMakePresets.json").read_text(encoding="utf-8"))
        configure = {p["name"]: p for p in presets["configurePresets"]}
        self.assertEqual(set(configure), {"m55m1-debug", "m55m1-release"})
        for profile, directory in (("debug", "build-debug"), ("release", "build")):
            preset = configure[f"m55m1-{profile}"]
            self.assertEqual(preset["binaryDir"], "${sourceDir}/" + directory)
            variables = preset["cacheVariables"]
            self.assertEqual(variables["M55M1_DEBUG_UART_IDX"], "2")
            self.assertEqual(variables["M55M1_ENABLE_DAC_I2S_OUTPUT"], "ON")
            self.assertFalse(any("TEST" in key for key in variables))
        for wrapper in ("build.bat", "build.sh"):
            text = (ROOT / wrapper).read_text(encoding="utf-8")
            self.assertNotIn("BUILD_CUSTOM_DMIC_TEST", text)
            self.assertNotIn("NUMAKER", text)
            self.assertIn("-DM55M1_DEBUG_UART_IDX=2", text)


if __name__ == "__main__":
    unittest.main()
