"""The opt-in adapter probe marks real stage boundaries in order."""

from pathlib import Path
import subprocess
import tempfile
import textwrap
import unittest


ROOT = Path(__file__).resolve().parents[1]


class DeepfirAdapterStageProbeTests(unittest.TestCase):
    def test_probe_marks_infer_carry_taps_and_fir_boundaries(self):
        harness = textwrap.dedent(r"""
            #include <stdint.h>
            #include <string.h>
            #include "services/deepfir/deepfir_model_adapter.h"

            static uint32_t marks[5];
            static uint32_t mark_count;
            static int inferred;

            static void mark(uint32_t stage, void *context) {
                deepfir_model_adapter_t *adapter = (deepfir_model_adapter_t *)context;
                if (mark_count >= 5u) return;
                marks[mark_count++] = stage;
                if (stage == 0u && inferred) mark_count = 99u;
                if (stage == 1u && !inferred) mark_count = 99u;
                if (stage == 2u && adapter->h[0] != 17) mark_count = 99u;
            }

            static int infer(const deepfir_tensor_map_t *inputs, uint32_t input_count,
                             deepfir_tensor_map_t *outputs, uint32_t output_count,
                             void *context) {
                (void)inputs;
                (void)context;
                if (input_count != 3u || output_count != 4u) return -1;
                inferred = 1;
                memset(outputs[0].data, 0, 65u * sizeof(int16_t));
                memset(outputs[1].data, 0, 65u * sizeof(int16_t));
                for (uint32_t state = 2u; state < 4u; ++state) {
                    for (uint32_t index = 0u; index < 200u; ++index) {
                        outputs[state].data[index] = 17;
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
                const float scales[4] = {1.0f, 1.0f, 1.0f, 1.0f};
                const int32_t zeros[4] = {0};
                const int16_t feature[387] = {0};
                const float pcm[16] = {0.5f};
                float output[16];
                if (deepfir_model_adapter_set_qparams(&adapter, scales, zeros,
                                                       scales, zeros) != 0) return 1;
                deepfir_model_adapter_reset(&adapter);
                adapter.infer_mapped = infer; adapter.fft = controller_test_fft;
                adapter.probe_mark = mark;
                adapter.probe_context = &adapter;
                if (deepfir_model_adapter_process_hop(&adapter, feature, pcm,
                                                      16u, output) != 0) return 2;
                if (mark_count != 5u) return 3;
                for (uint32_t index = 0u; index < 5u; ++index) {
                    if (marks[index] != index) return 4;
                }
                return 0;
            }
        """)
        with tempfile.TemporaryDirectory() as directory:
            temporary = Path(directory)
            source = temporary / "adapter_probe.c"
            executable = temporary / "adapter_probe.exe"
            source.write_text(harness, encoding="utf-8")
            build = subprocess.run(
                ["gcc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                 "-DM55M1_AUDIO_RELEASE_PROBE=1", "-I", str(ROOT),
                 str(source), str(ROOT / "services/deepfir/deepfir_model_adapter.c"),
                 str(ROOT / "services/deepfir/deepfir_dsp.c"), str(ROOT / "services/deepfir/deepfir_reconstruction.c"),
                 "-lm", "-o", str(executable)],
                capture_output=True, text=True, check=False,
            )
            self.assertEqual(build.returncode, 0, build.stderr)
            run = subprocess.run([str(executable)], capture_output=True, check=False)
            self.assertEqual(run.returncode, 0, run.stderr)


if __name__ == "__main__":
    unittest.main()
