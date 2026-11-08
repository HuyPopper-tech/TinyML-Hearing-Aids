"""Static policy gates for the checked-in DeepFIR product package."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class CMakeModelPackagePolicyTests(unittest.TestCase):
    def test_root_cmake_selects_the_checked_in_model_package(self):
        cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8").lower()
        self.assertIn("m55m1_model_package.cmake", (ROOT / "cmake" / "model_package.cmake").read_text())
        self.assertIn("m55m1_model_in_sram", cmake)

    def test_ethosu_runtime_sources_have_one_project_owned_path(self):
        cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn("option(M55M1_ENABLE_ETHOSU_RUNTIME", cmake)
        self.assertIn("M55M1_ENABLE_TFLM=ON requires M55M1_ENABLE_ETHOSU_RUNTIME=ON", cmake)
        self.assertIn("list(REMOVE_ITEM M55M1_STDDRIVER_SOURCES ${M55M1_ETHOSU_SOURCES})", cmake)
        self.assertIn("M55M1_ETHOSU_SOURCES", cmake)
        self.assertIn("list(APPEND M55M1_STDDRIVER_SOURCES ${M55M1_ETHOSU_CORE_SOURCES})", cmake)
        self.assertIn("if(M55M1_ENABLE_TFLM)", cmake)
        self.assertIn("drivers/ethosu_runtime/ethosu_tflm_bridge.cpp", cmake)

    def test_model_package_uses_project_local_sources_for_product(self):
        cmake = (ROOT / "cmake" / "model_package.cmake").read_text(encoding="utf-8")
        self.assertIn('set(M55M1_MODEL_PACKAGE_DIR "${PROJ_ROOT}/models/deepfir" CACHE PATH', cmake)
        self.assertIn("models/deepfir", cmake)
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
