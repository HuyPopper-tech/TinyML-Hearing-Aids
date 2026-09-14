"""Static policy gates for explicit model-package integration."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class CMakeModelPackagePolicyTests(unittest.TestCase):
    def test_root_cmake_has_no_model_family_name(self):
        cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8").lower()
        for name in ("deepfir", "speech_boosting", "neuralaid", "deepfilternet", "erb_gru"):
            self.assertNotIn(name, cmake)

    def test_ethosu_runtime_sources_are_strictly_opt_in(self):
        cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn("option(M55M1_ENABLE_ETHOSU_RUNTIME", cmake)
        self.assertIn("option(M55M1_BUILD_ETHOSU_SMOKE", cmake)
        self.assertIn("M55M1_ENABLE_TFLM=ON requires M55M1_ENABLE_ETHOSU_RUNTIME=ON", cmake)
        self.assertIn("list(REMOVE_ITEM M55M1_STDDRIVER_SOURCES ${M55M1_ETHOSU_SOURCES})", cmake)
        self.assertIn("M55M1_ETHOSU_SOURCES", cmake)
        self.assertIn("list(APPEND M55M1_STDDRIVER_SOURCES ${M55M1_ETHOSU_CORE_SOURCES})", cmake)
        self.assertIn("if(M55M1_ENABLE_TFLM)", cmake)
        self.assertIn("drivers/ethosu_runtime/ethosu_tflm_bridge.cpp", cmake)
        self.assertIn("M55M1_EthosUSmoke.axf", cmake)

    def test_model_package_is_explicit_and_opt_in(self):
        cmake = (ROOT / "cmake" / "model_package.cmake").read_text(encoding="utf-8")
        self.assertIn('set(M55M1_MODEL_PACKAGE_DIR "" CACHE PATH', cmake)
        self.assertIn("m55m1_model_package.cmake", cmake)
        self.assertIn("M55M1_MODEL_SOURCES", cmake)
        self.assertIn("M55M1_MODEL_INCLUDE_DIRS", cmake)
        self.assertIn("M55M1_MODEL_DEFINITIONS", cmake)
        self.assertIn("ethosu_driver.c", cmake)
        self.assertIn("ethosu_device_u55_u65.c", cmake)
        self.assertIn("NPU_IRQHandler", cmake)
        self.assertIn("M55M1_MODEL_PACKAGE_DIR", cmake)


if __name__ == "__main__":
    unittest.main()
