"""DeepFilterNet CMSIS-DSP/MVE and scalar parity build contract."""

from pathlib import Path
import subprocess
import tempfile
import textwrap
import unittest


ROOT = Path(__file__).resolve().parents[1]


class DeepFilterNetMveTests(unittest.TestCase):
    def read(self, path: str) -> str:
        return (ROOT / path).read_text(encoding="utf-8")

    def test_cmake_emits_numeric_modes_and_release_requires_mve(self):
        cmake = self.read("CMakeLists.txt")
        for macro in (
            "M55M1_DEEPFILTERNET_DSP_MVE",
            "M55M1_DEEPFILTERNET_DSP_SCALAR",
            "M55M1_DEEPFILTERNET_DSP_PARITY",
        ):
            self.assertIn(macro, cmake)
        self.assertIn("Production Release firmware requires DeepFilterNet MVE DSP", cmake)
        self.assertIn("MVE and PARITY require M55M1_ENABLE_DSP=ON", cmake)
        self.assertIn("PARITY requires M55M1_DEEPFILTERNET_DIAGNOSTICS=ON", cmake)

    def test_mve_backend_uses_cmsis_dsp_kernels(self):
        enhancer = self.read(
            "services/deepfilternet/deepfilternet_audio_enhancer.cpp"
        )
        for symbol in (
            "arm_rfft_fast_init_512_f32",
            "arm_rfft_fast_f32",
            "arm_mult_f32",
            "arm_dot_prod_f32",
            "arm_cmplx_mult_cmplx_f32",
            "arm_add_f32",
        ):
            self.assertIn(symbol, enhancer)
        self.assertIn("M55M1_DEEPFILTERNET_DSP_PARITY", enhancer)
        self.assertIn("parity_max_pcm_difference", enhancer)
        self.assertIn("parity_violation_count", enhancer)

    def test_parity_metrics_reach_atomic_telemetry(self):
        header = self.read("services/deepfilternet/deepfilternet_audio_enhancer.h")
        pipeline = self.read("services/audio_pipeline/audio_pipeline.c")
        telemetry = self.read("services/audio_telemetry.c")
        self.assertIn("parity_max_pcm_difference", header)
        self.assertIn(".parity_max_pcm_difference", pipeline)
        self.assertIn("parity_pcm_lsb=", telemetry)
        self.assertIn("parity_violation=", telemetry)

    def test_mve_and_parity_sources_are_host_syntax_clean(self):
        arm_math = textwrap.dedent(
            """
            #pragma once
            #include <stdint.h>
            typedef float float32_t;
            typedef enum { ARM_MATH_SUCCESS = 0 } arm_status;
            typedef struct { uint16_t fftLenRFFT; } arm_rfft_fast_instance_f32;
            arm_status arm_rfft_fast_init_512_f32(arm_rfft_fast_instance_f32 *);
            void arm_rfft_fast_f32(const arm_rfft_fast_instance_f32 *, float *, float *, uint8_t);
            void arm_mult_f32(const float *, const float *, float *, uint32_t);
            void arm_dot_prod_f32(const float *, const float *, uint32_t, float *);
            void arm_cmplx_mult_cmplx_f32(const float *, const float *, float *, uint32_t);
            void arm_add_f32(const float *, const float *, float *, uint32_t);
            void arm_scale_f32(const float *, float, float *, uint32_t);
            """
        )
        with tempfile.TemporaryDirectory() as directory:
            stub = Path(directory) / "arm_math.h"
            stub.write_text(arm_math, encoding="utf-8")
            for mode in ("MVE", "PARITY"):
                result = subprocess.run(
                    [
                        "g++", "-std=c++11", "-fsyntax-only",
                        f"-DM55M1_DEEPFILTERNET_DSP_{mode}=1",
                        "-DM55M1_DEEPFILTERNET_DIAGNOSTICS=1",
                        "-I", directory, "-I", str(ROOT),
                        "-I", str(ROOT / "libs/tensorflow/third_party/kissfft"),
                        str(ROOT / "services/deepfilternet/deepfilternet_audio_enhancer.cpp"),
                    ],
                    capture_output=True, text=True, check=False,
                )
                self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
