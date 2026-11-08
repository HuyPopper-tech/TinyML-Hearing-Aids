"""R2 carry requantizes both encoded-state lanes with exact integer rounding."""

from pathlib import Path
import subprocess
import tempfile
import textwrap
import unittest


ROOT = Path(__file__).resolve().parents[1]


class DeepfirStateCarryTests(unittest.TestCase):
    def test_r2_q31_feedback_and_changed_qparams_requantize_without_hot_frexp(self):
        harness = textwrap.dedent(r"""
            #include <math.h>
            #include <stdint.h>
            #include <string.h>
            #include "services/deepfir/deepfir_model_binding.h"

            static unsigned frexp_calls;
            static uint32_t code_base;
            static int16_t external_outputs[4][DEEPFIR_MODEL_STATE_UNITS];

            double counted_frexp(double value, int *exponent) {
                ++frexp_calls;
                return frexp(value, exponent);
            }

            static int16_t state_code(uint32_t index) {
                uint32_t code = code_base + index;
                return code < 65536u ? (int16_t)((int32_t)code - 32768) : 0;
            }

            static int infer(const deepfir_tensor_map_t *inputs, uint32_t input_count,
                             deepfir_tensor_map_t *outputs, uint32_t output_count,
                             void *context) {
                (void)inputs;
                (void)context;
                if (input_count != 3u || output_count != 4u) return -1;
                for (uint32_t state = 0u; state < 4u; ++state) {
                    outputs[state].data = external_outputs[state];
                }
                memset(outputs[0].data, 0, 65u * sizeof(int16_t));
                for (uint32_t state = 2u; state < 4u; ++state) {
                    for (uint32_t index = 0u; index < DEEPFIR_MODEL_STATE_UNITS; ++index) {
                        outputs[state].data[index] = state_code(index);
                    }
                }
                return 0;
            }

        /* Controller lifecycle only; numerical FFT is covered by test_r2_integration. */
        static int controller_test_fft(float *data, uint32_t n, uint8_t inverse, void *ctx) {
            (void)data; (void)n; (void)inverse; (void)ctx; return 0;
        }
                    int main(void) {
                static deepfir_model_adapter_t adapter;
                const int16_t feature[DEEPFIR_MODEL_FEATURES] = {0};
                const float pcm[16] = {0.0f};
                float output[16];
                float input_scales[3];
                memcpy(input_scales, deepfir_input_scale, sizeof(input_scales));
                if (deepfir_model_init_qparams(&adapter) != 0) return 1;
                adapter.infer_mapped = infer; adapter.fft = controller_test_fft;
                for (code_base = 0u; code_base < 65536u;
                     code_base += DEEPFIR_MODEL_STATE_UNITS) {
                    frexp_calls = 0u;
                    if (deepfir_model_adapter_process_hop(&adapter, feature, pcm, 16u, output) != 0) return 2;
                    if (frexp_calls != 0u) return 3;
                    for (uint32_t index = 0u; index < DEEPFIR_MODEL_STATE_UNITS; ++index) {
                        int64_t raw = state_code(index);
                        int64_t h = llround((double)(raw * INT64_C(2147371134)) / 2147483648.0);
                        int64_t c = llround((double)(raw * INT64_C(1075624845)) / 1073741824.0);
                        if (c < INT16_MIN) c = INT16_MIN;
                        if (c > INT16_MAX) c = INT16_MAX;
                        if (adapter.h[index] != h || adapter.c[index] != c) return 4;
                    }
                }

                code_base = 32668u;
                input_scales[2] = deepfir_output_scale[3] * 0.5f;
                if (deepfir_model_adapter_set_qparams(&adapter, input_scales,
                        deepfir_input_zero, deepfir_output_scale,
                        deepfir_output_zero) != 0) return 5;
                if (deepfir_model_adapter_process_hop(&adapter, feature, pcm, 16u, output) != 0) return 6;
                for (uint32_t index = 0u; index < DEEPFIR_MODEL_STATE_UNITS; ++index) {
                    int32_t expected = (int32_t)state_code(index) * 2;
                    if (expected < INT16_MIN) expected = INT16_MIN;
                    if (expected > INT16_MAX) expected = INT16_MAX;
                    if (adapter.c[index] != expected) return 7;
                }
                return 0;
            }
        """)
        with tempfile.TemporaryDirectory() as directory:
            temporary = Path(directory)
            source = temporary / "state_carry.c"
            adapter_object = temporary / "deepfir_model_adapter.o"
            executable = temporary / "state_carry.exe"
            source.write_text(harness, encoding="utf-8")
            compile_adapter = subprocess.run(
                ["gcc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                 "-fno-builtin-frexp", "-Dfrexp=counted_frexp",
                 "-I", str(ROOT), "-I", str(ROOT / "services/deepfir"),
                 "-c", str(ROOT / "services/deepfir/deepfir_model_adapter.c"),
                 "-o", str(adapter_object)],
                capture_output=True, text=True, check=False,
            )
            self.assertEqual(compile_adapter.returncode, 0, compile_adapter.stderr)
            compile_harness = subprocess.run(
                ["gcc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                 "-fno-builtin-frexp", "-I", str(ROOT),
                 "-I", str(ROOT / "services/deepfir"), str(source),
                 str(adapter_object), str(ROOT / "services/deepfir/deepfir_dsp.c"), str(ROOT / "services/deepfir/deepfir_reconstruction.c"),
                 "-lm", "-o", str(executable)],
                capture_output=True, text=True, check=False,
            )
            self.assertEqual(compile_harness.returncode, 0, compile_harness.stderr)
            run = subprocess.run([str(executable)], capture_output=True, check=False)
            self.assertEqual(run.returncode, 0, run.stderr)


if __name__ == "__main__":
    unittest.main()
