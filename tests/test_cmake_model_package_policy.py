"""Static policy gates for explicit model-package integration."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class CMakeModelPackagePolicyTests(unittest.TestCase):
    def test_root_cmake_has_no_model_family_name(self):
        cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8").lower()
        for name in ("deepfir", "speech_boosting", "neuralaid", "deepfilternet", "erb_gru"):
            self.assertNotIn(name, cmake)

    def test_model_package_is_explicit_and_opt_in(self):
        cmake = (ROOT / "cmake" / "model_package.cmake").read_text(encoding="utf-8")
        self.assertIn('set(M55M1_MODEL_PACKAGE_DIR "" CACHE PATH', cmake)
        self.assertIn("m55m1_model_package.cmake", cmake)
        self.assertIn("M55M1_MODEL_SOURCES", cmake)
        self.assertIn("M55M1_MODEL_INCLUDE_DIRS", cmake)
        self.assertIn("M55M1_MODEL_DEFINITIONS", cmake)


if __name__ == "__main__":
    unittest.main()
