"""Host test for inference-failure WOLA continuity and recurrent-state reset."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import textwrap
import unittest


ROOT = Path(__file__).resolve().parents[1]
KISSFFT = ROOT / "libs" / "tensorflow" / "third_party" / "kissfft"


class ErbGruFailureRecoveryTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which("g++"), "g++ is required")
    def test_invoke_failure_uses_wola_and_resets_both_states(self):
        harness = textwrap.dedent(
            r"""
            #include <cstdint>
            #include <cstring>
            #include "models/erb_gru/generated/golden/golden_sine_1khz.h"
            #include "services/erb_gru/erb_gru_audio_enhancer.h"
            #include "services/erb_gru/erb_gru_inference.h"

            static unsigned int invocation;
            static bool reset_seen;

            extern "C" bool erb_gru_inference_init(void) { return true; }
            extern "C" bool erb_gru_inference_invoke(
                const int8_t feature[32], const int8_t state1[64], const int8_t state2[64],
                int8_t gain[32], int8_t next_state1[64], int8_t next_state2[64])
            {
                (void)feature;
                if (invocation++ == 0U) return false;
                reset_seen = true;
                for (unsigned int i = 0; i < 64U; ++i) {
                    reset_seen = reset_seen && (state1[i] == -1) && (state2[i] == 0);
                    next_state1[i] = 20;
                    next_state2[i] = -20;
                }
                std::memset(gain, 127, 32U);
                return true;
            }
            extern "C" void erb_gru_inference_get_performance(
                erb_gru_inference_performance_t *performance)
            {
                if (performance) std::memset(performance, 0, sizeof(*performance));
            }

            int main()
            {
                int16_t output[160] = {};
                if (!erb_gru_audio_enhancer_init()) return 1;
                if (erb_gru_audio_enhancer_process(
                        erb_gru_sine_1khz_pcm_input_int16, output) !=
                    ERB_GRU_AUDIO_RESULT_PREROLL_SILENCE) return 2;
                if (erb_gru_audio_enhancer_process(
                        erb_gru_sine_1khz_pcm_input_int16 + 160, output) !=
                    ERB_GRU_AUDIO_RESULT_UNITY_FALLBACK) return 3;
                bool differs_from_raw = false;
                for (unsigned int i = 0; i < 160U; ++i) {
                    differs_from_raw = differs_from_raw ||
                        (output[i] != erb_gru_sine_1khz_pcm_input_int16[160U + i]);
                }
                if (!differs_from_raw) return 4;
                if (erb_gru_audio_enhancer_process(
                        erb_gru_sine_1khz_pcm_input_int16 + 320, output) !=
                    ERB_GRU_AUDIO_RESULT_ENHANCED) return 5;
                return reset_seen ? 0 : 6;
            }
            """
        )
        with tempfile.TemporaryDirectory() as temporary_directory:
            temporary = Path(temporary_directory)
            source = temporary / "recovery.cpp"
            executable = temporary / "recovery.exe"
            source.write_text(harness, encoding="utf-8")
            command = [
                shutil.which("g++"),
                "-std=c++11",
                "-O2",
                "-DMODEL_GOLDEN_TEST_DATA=1",
                "-I",
                str(ROOT),
                "-I",
                str(KISSFFT),
                str(ROOT / "services/erb_gru/erb_gru_audio_enhancer.cpp"),
                str(KISSFFT / "kiss_fft.c"),
                str(KISSFFT / "tools/kiss_fftr.c"),
                str(source),
                "-o",
                str(executable),
            ]
            compiled = subprocess.run(command, capture_output=True, text=True, check=False)
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            result = subprocess.run(
                [str(executable)], capture_output=True, text=True, check=False
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
