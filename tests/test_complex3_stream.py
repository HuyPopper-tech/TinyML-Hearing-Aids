"""Component behavior only: no neural replay or alternate firmware image."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class Complex3StreamTests(unittest.TestCase):
    def test_context_reset_feedback_failure_and_pcm(self):
        harness = r'''
        #include <math.h>
        #include <stdint.h>
        #include <string.h>
        #include "services/deepfir/deepfir_model_binding.h"

        static unsigned calls;
        static int fail;
        static int infer(const deepfir_tensor_map_t *in, uint32_t ni,
                         deepfir_tensor_map_t *out, uint32_t no, void *context) {
            (void)context;
            if (ni != 3 || no != 4 || in[0].elements != 774 ||
                in[1].semantic != DEEPFIR_TENSOR_H ||
                in[2].semantic != DEEPFIR_TENSOR_C ||
                out[0].elements != 65 || out[1].elements != 65) { return -11; }
            for (unsigned i = 0; i < 387; ++i) {
                if (in[0].data[i] != (calls ? 123 : 0) ||
                    in[0].data[i + 387] != (calls ? -456 : 123)) { return -12; }
            }
            for (unsigned i = 0; i < 200; ++i) {
                if (in[1].data[i] != (calls ? 321 : 0) ||
                    in[2].data[i] != (calls ? -790 : 0)) { return -13; }
                out[2].data[i] = 321;
                out[3].data[i] = -789;
            }
            memset(out[0].data, 0, 65 * sizeof(int16_t));
            memset(out[1].data, 0, 65 * sizeof(int16_t));
            ++calls;
            return fail ? -99 : 0;
        }

        /* Controller lifecycle only; numerical FFT is covered by test_r2_integration. */
        static int controller_test_fft(float *data, uint32_t n, uint8_t inverse, void *ctx) {
            (void)data; (void)n; (void)inverse; (void)ctx; return 0;
        }
                int main(void) {
            static deepfir_model_adapter_t a;
            int16_t feature[387] = {0};
            float pcm[16] = {0}, output[16];
            a.infer_mapped = infer; a.fft = controller_test_fft;
            if (deepfir_model_adapter_process_hop(&a, feature, pcm, 16, output) != -2 || calls) { return 13; }
            if (deepfir_model_init_qparams(&a)) { return 1; }
            if (a.feedback_multiplier[1] != 1075624845 || a.feedback_shift[1] != 30) { return 2; }
            a.infer_mapped = infer; a.fft = controller_test_fft;
            deepfir_model_adapter_reset(&a);
            if (a.dsp.previous_taps[64] != 1 || a.dsp.current_taps[64] != 1) { return 3; }
            for (unsigned i = 0; i < 387; ++i) { feature[i] = 123; }
            if (deepfir_model_adapter_process_hop(&a, feature, pcm, 15, output) != -1 || calls) { return 4; }
            if (deepfir_model_adapter_process_hop(&a, feature, pcm, 16, output)) { return 5; }
            for (unsigned i = 0; i < 387; ++i) { feature[i] = -456; }
            if (deepfir_model_adapter_process_hop(&a, feature, pcm, 16, output)) { return 6; }
            deepfir_model_adapter_reset(&a);
            calls = 0; fail = 1;
            for (unsigned i = 0; i < 387; ++i) { feature[i] = 123; }
            for (unsigned i = 0; i < 16; ++i) { output[i] = 42; }
            if (deepfir_model_adapter_process_hop(&a, feature, pcm, 16, output) != -99) { return 7; }
            for (unsigned i = 0; i < 16; ++i) { if (output[i] != 42) { return 8; } }
            for (unsigned i = 0; i < 200; ++i) { if (a.h[i] || a.c[i]) { return 9; } }
            if (a.dsp.sample_count || a.feature_context_buffer[0]) { return 10; }
            deepfir_model_adapter_reset(&a); calls = 0; fail = 0;
            if (deepfir_model_adapter_process_hop(&a, feature, pcm, 16, output)) { return 11; }
            if (deepfir_pcm16_from_float(0.5f / 32768) != 1 ||
                deepfir_pcm16_from_float(-0.5f / 32768) != -1 ||
                deepfir_pcm16_from_float(10) != 32767 ||
                deepfir_pcm16_from_float(-10) != -32768 ||
                deepfir_pcm16_from_float(NAN) != 0 ||
                deepfir_pcm16_from_float(INFINITY) != 0) { return 12; }
            const float invalid_scales[3] = {0};
            if (deepfir_model_adapter_set_qparams(&a, invalid_scales, deepfir_input_zero,
                                                 deepfir_output_scale, deepfir_output_zero) == 0 ||
                deepfir_model_adapter_process_hop(&a, feature, pcm, 16, output) != -2) { return 14; }
            return 0;
        }
        '''
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'stream.c'
            executable = Path(directory) / 'stream.exe'
            source.write_text(harness)
            build = subprocess.run(['gcc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                                    '-I', str(ROOT), '-I', str(ROOT / 'services/deepfir'),
                                    str(source), str(ROOT / 'services/deepfir/deepfir_model_adapter.c'),
                                    str(ROOT / 'services/deepfir/deepfir_dsp.c'), str(ROOT / 'services/deepfir/deepfir_reconstruction.c'), '-lm', '-o', str(executable)],
                                   capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            run = subprocess.run([str(executable)], capture_output=True)
            self.assertEqual(run.returncode, 0, run.stderr)
