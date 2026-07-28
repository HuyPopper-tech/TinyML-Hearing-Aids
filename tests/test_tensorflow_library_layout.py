"""Contract tests for the source-only TensorFlow Lite Micro layout."""

from __future__ import annotations

import unittest
from pathlib import Path
import subprocess


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

    def test_configured_build_exposes_the_tflm_static_library(self):
        """A missing CMake target must be visible before the firmware build starts."""
        result = subprocess.run(
            ["cmake", "--build", str(REPO_ROOT / "build-debug"), "--target", "help"],
            check=True,
            capture_output=True,
            text=True,
        )

        self.assertIn("TensorFlowLiteMicro", result.stdout)

    def test_configured_build_excludes_the_cmsis_pack_timer_port(self):
        """M55M1 must not compile the Cortex-M generic RTE timer implementation."""
        build_ninja = (REPO_ROOT / "build-debug" / "build.ninja").read_text(
            encoding="utf-8"
        )

        self.assertNotIn("cortex_m_generic/micro_time.cpp", build_ninja)

    def test_configured_build_excludes_kernel_test_harnesses(self):
        """KernelRunner depends on mock-only TFLM classes removed from production."""
        build_ninja = (REPO_ROOT / "build-debug" / "build.ninja").read_text(
            encoding="utf-8"
        )

        self.assertNotIn("kernels/kernel_runner.cpp", build_ninja)
        self.assertNotIn("micro/fake_micro_context.cpp", build_ninja)

    def test_cmsis_nn_v18_supplies_tflm_kernel_apis(self):
        """The pinned CMSIS-NN revision must provide TFLM's optimized APIs."""
        header = (REPO_ROOT / "libs" / "NN" / "Include" / "arm_nnfunctions.h")
        contents = header.read_text(encoding="utf-8")

        self.assertIn("$Revision:    V.18.0.0", contents)
        self.assertIn("arm_maximum_s8", contents)
        self.assertIn("arm_minimum_s8", contents)
        self.assertIn("arm_pad_s8", contents)

    def test_configured_build_selects_cmsis_nn_kernel_providers(self):
        """Each optimized kernel must replace its generic provider, not duplicate it."""
        build_ninja = (REPO_ROOT / "build-debug" / "build.ninja").read_text(
            encoding="utf-8"
        )

        self.assertIn("kernels/cmsis_nn/maximum_minimum.cpp", build_ninja)
        self.assertNotIn("kernels/maximum_minimum.cpp", build_ninja)


if __name__ == "__main__":
    unittest.main()
