"""The FIR crossfade must keep its output bytes without per-hop trigonometry."""

import struct
import math
from pathlib import Path
import subprocess
import tempfile
import textwrap
import unittest


ROOT = Path(__file__).resolve().parents[1]


class DeepfirFirBlendCacheTests(unittest.TestCase):
    def test_blend_is_prepared_before_audio_and_output_is_unchanged(self):
        harness = textwrap.dedent(r"""
            #include <math.h>
            #include <stdint.h>
            #include <stdio.h>
            #include <string.h>
            #ifdef _WIN32
            #include <fcntl.h>
            #include <io.h>
            #endif
            #include "services/deepfir/deepfir_dsp.h"

            static uint32_t random_state = 20260923u;
            static unsigned cosf_calls;
            static float previous[128], history[128];

            float counted_cosf(float value) {
                ++cosf_calls;
                return cosf(value);
            }

            static uint32_t next_random(void) {
                random_state ^= random_state << 13;
                random_state ^= random_state >> 17;
                random_state ^= random_state << 5;
                return random_state;
            }

            int main(void) {
                static deepfir_dsp_state_t state;
                float input[DEEPFIR_DSP_HOP];
                float taps[DEEPFIR_DSP_TAPS];
                float output[DEEPFIR_DSP_HOP];
                unsigned prepared_calls;
            #ifdef _WIN32
                if (_setmode(_fileno(stdout), _O_BINARY) == -1) return 1;
            #endif
                deepfir_dsp_reset(&state, NULL);
                prepared_calls = cosf_calls;
                previous[64] = 1.0f;
                for (uint32_t hop = 0u; hop < 64u; ++hop) {
                    for (uint32_t index = 0u; index < DEEPFIR_DSP_HOP; ++index) {
                        input[index] = ((int32_t)(next_random() & 65535u) - 32768) / 32768.0f;
                    }
                    for (uint32_t index = 0u; index < DEEPFIR_DSP_TAPS; ++index) {
                        taps[index] = ((int32_t)(next_random() & 1023u) - 512) / 8192.0f;
                    }
                    if (deepfir_dsp_process(&state, input, DEEPFIR_DSP_HOP,
                                            taps, output) != 0) return 2;
                    for (unsigned sample = 0; sample < 16; ++sample) {
                        for (unsigned i = 127; i > 0; --i) { history[i] = history[i-1]; }
                        history[0] = input[sample];
                        double old_value = 0, new_value = 0;
                        for (unsigned i = 0; i < 128; ++i) {
                            old_value += (double)previous[i] * history[i];
                            new_value += (double)taps[i] * history[i];
                        }
                        double blend = 0.5 - 0.5 * cos(3.14159265358979323846 * (sample + 0.5) / 16.0);
                        float pair[2] = {output[sample], (float)(old_value * (1.0 - blend) + new_value * blend)};
                        if (fwrite(pair, sizeof(pair), 1u, stdout) != 1u) return 3;
                    }
                    memcpy(previous, taps, sizeof(previous));
                    if (memcmp(state.history, history, sizeof(history)) ||
                        memcmp(state.current_taps, taps, sizeof(previous)) ||
                        memcmp(state.previous_taps, taps, sizeof(previous))) return 4;
                }
                fprintf(stderr, "%u,%u", prepared_calls, cosf_calls);
                return 0;
            }
        """)
        with tempfile.TemporaryDirectory() as directory:
            temporary = Path(directory)
            source = temporary / "fir_blend.c"
            dsp_object = temporary / "deepfir_dsp.o"
            executable = temporary / "fir_blend.exe"
            source.write_text(harness, encoding="utf-8")
            compile_dsp = subprocess.run(
                ["gcc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                 "-fno-builtin-cosf", "-Dcosf=counted_cosf",
                 "-I", str(ROOT), "-c", str(ROOT / "services/deepfir/deepfir_dsp.c"),
                 "-o", str(dsp_object)], capture_output=True, text=True, check=False,
            )
            self.assertEqual(compile_dsp.returncode, 0, compile_dsp.stderr)
            compile_harness = subprocess.run(
                ["gcc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                 "-fno-builtin-cosf", "-I", str(ROOT), str(source),
                 str(dsp_object), "-lm", "-o", str(executable)],
                capture_output=True, text=True, check=False,
            )
            self.assertEqual(compile_harness.returncode, 0, compile_harness.stderr)
            run = subprocess.run([str(executable)], capture_output=True, check=False)
            self.assertEqual(run.returncode, 0, run.stderr)
            self.assertEqual(len(run.stdout), 64 * 16 * 8)
            for actual, expected in struct.iter_unpack('ff', run.stdout):
                self.assertTrue(math.isfinite(actual))
                self.assertLessEqual(abs(actual - expected), 2e-5 + 2e-6 * abs(expected))
            prepared_calls, final_calls = map(int, run.stderr.decode().split(","))
            self.assertEqual(prepared_calls, 16)
            self.assertEqual(final_calls, prepared_calls)


if __name__ == "__main__":
    unittest.main()
