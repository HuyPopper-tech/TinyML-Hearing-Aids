"""The fast feature transform stays within one INT16 code of its reference."""

from pathlib import Path
import subprocess
import tempfile
import textwrap
import unittest


ROOT = Path(__file__).resolve().parents[1]


class DeepfirFeatureTransformTests(unittest.TestCase):
    def test_float_transform_matches_double_reference_within_one_code(self):
        harness = textwrap.dedent(
            r"""
            #include <math.h>
            #include <stdint.h>
            #include "services/deepfir/deepfir_dsp.h"
            #include "services/deepfir/deepfir_model_binding.h"

            static uint32_t random_state = 20260921U;
            static float fft_output[258];
            static unsigned hop_number;

            static uint32_t next_random(void) {
                random_state ^= random_state << 13;
                random_state ^= random_state >> 17;
                random_state ^= random_state << 5;
                return random_state;
            }

            static int fake_fft(float *buffer, uint32_t length,
                                 uint8_t inverse, void *context) {
                (void)inverse;
                (void)context;
                if (length != 256U) return -1;
                for (uint32_t index = 0U; index < 129U; ++index) {
                    buffer[2U * index] =
                        (float)(next_random() & 0xffffU) / 4096.0f - 8.0f;
                    buffer[2U * index + 1U] =
                        (float)(next_random() & 0xffffU) / 4096.0f - 8.0f;
                    if (hop_number < 8u) {
                        const float cases[8] = {
                            0.0f, 1.0e-24f, -1.0e-20f, 1.0e-19f,
                            -1.0e-8f, 1.0e-4f, -1.0f, -1000.0f,
                        };
                        buffer[2U * index] = cases[hop_number];
                        buffer[2U * index + 1U] = -cases[hop_number];
                    }
                    fft_output[2U * index] = buffer[2U * index];
                    fft_output[2U * index + 1U] = buffer[2U * index + 1U];
                }
                return 0;
            }

            int main(void) {
                deepfir_feature_state_t state = {0};
                int16_t input[16];
                int16_t feature[387];
                state.scale = deepfir_input_scale[0];
                state.fft = fake_fft;
                deepfir_feature_reset(&state);
                for (unsigned i = 0; i < 256; ++i) { if (state.history[i] != 0) return 3; }
                if (fabsf(state.window[0] - 0.08f) > 1e-7f ||
                    fabsf(state.window[255] - 0.08f) > 1e-7f) return 4;
                for (uint32_t hop = 0U; hop < 2000U; ++hop) {
                    for (uint32_t index = 0U; index < 16U; ++index) {
                        input[index] = (int16_t)next_random();
                    }
                    hop_number = hop;
                    if (deepfir_feature_pcm16(input, 16U, feature, &state) != 0) {
                        return 1;
                    }
                    for (uint32_t index = 0U; index < 129U; ++index) {
                        double real = fft_output[2U * index];
                        double imaginary = fft_output[2U * index + 1U];
                        double power = real * real + imaginary * imaginary;
                        double gain = pow(sqrt(power + 1.0e-16), -0.7);
                        double values[3] = {pow(sqrt(power), 0.3), real * gain, imaginary * gain};
                        for (unsigned channel = 0; channel < 3; ++channel) {
                            double code = values[channel] / (double)state.scale;
                            double rounded = code >= 0 ? floor(code + 0.5) : -floor(-code + 0.5);
                            int32_t expected = rounded < -32768 ? -32768 : rounded > 32767 ? 32767 : (int32_t)rounded;
                            int32_t delta = (int32_t)feature[3U * index + channel] - expected;
                            if (delta < -1 || delta > 1) return 2;
                        }
                    }
                }
                deepfir_feature_reset(&state);
                for (unsigned i = 0; i < 256; ++i) { if (state.history[i] != 0) return 5; }
                return 0;
            }
            """
        )
        with tempfile.TemporaryDirectory() as temporary_directory:
            temporary = Path(temporary_directory)
            source = temporary / "feature_transform_test.c"
            executable = temporary / "feature_transform.exe"
            source.write_text(harness, encoding="utf-8")
            for vector in (False, True):
                with self.subTest(vector=vector):
                    # The native lane model exercises the MVE control flow with host math.
                    flags = ["-D__ARM_FEATURE_MVE=2", "-I", str(ROOT / "tests/mve_model")] if vector else []
                    build = subprocess.run(
                        ["gcc", "-std=c11", "-O3", "-Wall", "-Wextra", "-Werror",
                         "-DM55M1_DEEPFIR_MVE=1", *flags, "-I", str(ROOT), str(source),
                         str(ROOT / "services/deepfir/deepfir_features.c"),
                         "-lm", "-o", str(executable)],
                        capture_output=True, text=True, check=False,
                    )
                    self.assertEqual(build.returncode, 0, build.stderr)
                    run = subprocess.run([str(executable)], capture_output=True, check=False)
                    self.assertEqual(run.returncode, 0, run.stderr)


if __name__ == "__main__":
    unittest.main()
