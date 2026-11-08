"""Contract tests for the source-only vendor layout and model-free build."""

from __future__ import annotations

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
TENSORFLOW_ROOT = REPO_ROOT / "libs" / "tensorflow"


class TensorFlowLibraryLayoutTests(unittest.TestCase):
    def test_runtime_and_dependency_sources_have_stable_roots(self):
        """A package-layout regression must not hide required TFLM headers."""
        required_paths = (
            "runtime/tensorflow/lite/micro/micro_interpreter.h",
            "runtime/tensorflow/lite/micro/kernels/ethos_u/ethosu.cpp",
            "runtime/signal",
            "third_party/flatbuffers/include/flatbuffers/flexbuffers.h",
            "third_party/gemmlowp/fixedpoint/fixedpoint.h",
            "third_party/kissfft/kiss_fft.h",
            "third_party/kissfft/tools/kiss_fftr.h",
            "third_party/ruy/ruy",
        )

        for relative_path in required_paths:
            with self.subTest(relative_path=relative_path):
                self.assertTrue((TENSORFLOW_ROOT / relative_path).exists())

    def test_cmsis_pack_metadata_and_examples_are_not_shipped(self):
        """The vendor tree must remain source-only and not retain pack-only assets."""
        self.assertFalse(any(TENSORFLOW_ROOT.rglob("*.pdsc")))
        self.assertFalse((TENSORFLOW_ROOT / "runtime" / "examples").exists())

    def test_product_build_enables_tflm_by_default(self):
        """The product image includes its controller runtime by default."""
        cmake = (REPO_ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn('option(M55M1_ENABLE_TFLM "Build and link TensorFlow Lite Micro with Ethos-U" ON)', cmake)
        self.assertIn('option(M55M1_ENABLE_TFLM "Build and link TensorFlow Lite Micro with Ethos-U" ON)', cmake)

    def test_configured_build_excludes_the_cmsis_pack_timer_port(self):
        """M55M1 must not compile the Cortex-M generic RTE timer implementation."""
        cmake = (REPO_ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertNotIn("cortex_m_generic/micro_time.cpp", cmake)

    def test_configured_build_excludes_kernel_test_harnesses(self):
        """KernelRunner depends on mock-only TFLM classes removed from production."""
        cmake = (REPO_ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertNotIn("kernels/kernel_runner.cpp", cmake)
        self.assertNotIn("micro/fake_micro_context.cpp", cmake)

    def test_cmsis_nn_v18_supplies_tflm_kernel_apis(self):
        """The pinned CMSIS-NN revision must provide TFLM's optimized APIs."""
        header = (REPO_ROOT / "libs" / "NN" / "Include" / "arm_nnfunctions.h")
        contents = header.read_text(encoding="utf-8")

        self.assertIn("$Revision:    V.18.0.0", contents)
        self.assertIn("arm_maximum_s8", contents)
        self.assertIn("arm_minimum_s8", contents)
        self.assertIn("arm_pad_s8", contents)

    def test_model_package_is_the_only_future_model_extension(self):
        """Future model sources enter through the explicit package module."""
        cmake = (REPO_ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn("cmake/model_package.cmake", cmake)
        self.assertIn("M55M1_MODEL_SOURCES", cmake)


if __name__ == "__main__":
    unittest.main()
