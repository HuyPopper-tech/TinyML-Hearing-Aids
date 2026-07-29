"""Host behavior test for ERB-GRU acoustic diagnostic accumulation."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import textwrap
import unittest


ROOT = Path(__file__).resolve().parents[1]
KISSFFT = ROOT / "libs" / "tensorflow" / "third_party" / "kissfft"


class ErbGruAudioDiagnosticsTests(unittest.TestCase):
    def test_accumulator_reports_literal_pcm_tensor_statistics_and_resets(self):
        harness = textwrap.dedent(
            r"""
            #include <stdint.h>

            #include "services/erb_gru/erb_gru_audio_diagnostics.h"

            int main(void)
            {
                const int16_t input[] = {-32768, -3, 4, 32767};
                const int16_t output[] = {1, -2, 2, -1};
                const int8_t feature[] = {-128, -2, 127};
                const int8_t gain[] = {-128, -116, 0, 116, 127};
                const int8_t state1[] = {-128, -1, 127};
                const int8_t state2[] = {-3, 4, 5};
                erb_gru_audio_diagnostics_t result;

                erb_gru_audio_diagnostics_reset();
                erb_gru_audio_diagnostics_record_feature(feature, 3U);
                erb_gru_audio_diagnostics_record_gain_state(
                    gain, 5U, state1, state2, 3U);
                erb_gru_audio_diagnostics_record_pcm(input, output, 4U);
                if (!erb_gru_audio_diagnostics_take(&result)) return 1;
                if (result.frames != 1U || result.pcm_samples != 4U) return 2;
                if (result.input_energy != 2147418138ULL ||
                    result.output_energy != 10ULL) return 3;
                if (result.input_peak != 32768U || result.output_peak != 2U) return 4;
                if (result.input_clip_count != 2U ||
                    result.output_clip_count != 0U) return 5;
                if (result.feature_sum != -3 || result.feature_count != 3U ||
                    result.feature_min != -128 || result.feature_max != 127 ||
                    result.feature_sat_low != 1U || result.feature_sat_high != 1U) return 6;
                if (result.gain_sum != -1 || result.gain_count != 5U ||
                    result.gain_min != -128 || result.gain_max != 127 ||
                    result.gain_near_zero != 2U || result.gain_near_unity != 2U) return 7;
                if (result.state1_min != -128 || result.state1_max != 127 ||
                    result.state2_min != -3 || result.state2_max != 5 ||
                    result.state_sat_count != 2U) return 8;
                return erb_gru_audio_diagnostics_take(&result) ? 9 : 0;
            }
            """
        )
        with tempfile.TemporaryDirectory() as temporary_directory:
            temporary = Path(temporary_directory)
            source = temporary / "audio_diagnostics_test.c"
            executable = temporary / "audio_diagnostics_test.exe"
            source.write_text(harness, encoding="utf-8")
            compile_result = subprocess.run(
                [
                    "gcc",
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-DM55M1_ERB_GRU_DIAGNOSTICS=1",
                    "-I",
                    str(ROOT),
                    str(source),
                    str(
                        ROOT
                        / "services"
                        / "erb_gru"
                        / "erb_gru_audio_diagnostics.c"
                    ),
                    "-o",
                    str(executable),
                ],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(compile_result.returncode, 0, compile_result.stderr)
            run_result = subprocess.run(
                [str(executable)], capture_output=True, text=True, check=False
            )
            self.assertEqual(run_result.returncode, 0, run_result.stderr)

    @unittest.skipUnless(shutil.which("g++"), "g++ is required for enhancer diagnostics")
    def test_enhancer_excludes_preroll_and_compares_output_with_source_hop(self):
        harness = textwrap.dedent(
            r"""
            #include <cstdint>
            #include <cstring>

            #include "services/erb_gru/erb_gru_audio_enhancer.h"
            #include "services/erb_gru/erb_gru_inference.h"

            extern "C" bool erb_gru_inference_init(void) { return true; }
            extern "C" bool erb_gru_inference_invoke(
                const int8_t feature[32], const int8_t state1[64],
                const int8_t state2[64], int8_t gain[32],
                int8_t next_state1[64], int8_t next_state2[64])
            {
                (void)feature;
                (void)state1;
                (void)state2;
                std::memset(gain, 127, 32U);
                std::memset(next_state1, -1, 64U);
                std::memset(next_state2, 2, 64U);
                return true;
            }
            extern "C" void erb_gru_inference_get_performance(
                erb_gru_inference_performance_t *performance)
            {
                std::memset(performance, 0, sizeof(*performance));
            }

            int main(void)
            {
                int16_t first[160];
                int16_t second[160];
                int16_t output[160];
                erb_gru_audio_diagnostics_t diagnostics;

                for (uint32_t index = 0U; index < 160U; ++index) {
                    first[index] = 3;
                    second[index] = 1000;
                }
                if (!erb_gru_audio_enhancer_init()) return 1;
                if (erb_gru_audio_enhancer_process(first, output) !=
                    ERB_GRU_AUDIO_RESULT_PREROLL_SILENCE) return 2;
                if (erb_gru_audio_enhancer_take_diagnostics(&diagnostics)) return 3;
                if (erb_gru_audio_enhancer_process(second, output) !=
                    ERB_GRU_AUDIO_RESULT_ENHANCED) return 4;
                if (!erb_gru_audio_enhancer_take_diagnostics(&diagnostics)) return 5;
                if (diagnostics.frames != 1U || diagnostics.pcm_samples != 160U) return 6;
                if (diagnostics.input_energy != 1440ULL ||
                    diagnostics.input_peak != 3U) return 7;
                if (diagnostics.feature_count != 32U ||
                    diagnostics.gain_count != 32U) return 8;
                if (diagnostics.state1_min != -1 || diagnostics.state1_max != -1 ||
                    diagnostics.state2_min != 2 || diagnostics.state2_max != 2) return 9;
                return 0;
            }
            """
        )
        with tempfile.TemporaryDirectory() as temporary_directory:
            temporary = Path(temporary_directory)
            source = temporary / "enhancer_diagnostics_test.cpp"
            executable = temporary / "enhancer_diagnostics_test.exe"
            source.write_text(harness, encoding="utf-8")
            compile_result = subprocess.run(
                [
                    shutil.which("g++"),
                    "-std=c++11",
                    "-O2",
                    "-DM55M1_ERB_GRU_DIAGNOSTICS=1",
                    "-DMODEL_GOLDEN_TEST_DATA=0",
                    "-I",
                    str(ROOT),
                    "-I",
                    str(KISSFFT),
                    str(ROOT / "services" / "erb_gru" / "erb_gru_audio_enhancer.cpp"),
                    str(ROOT / "services" / "erb_gru" / "erb_gru_audio_diagnostics.c"),
                    str(KISSFFT / "kiss_fft.c"),
                    str(KISSFFT / "tools" / "kiss_fftr.c"),
                    str(source),
                    "-o",
                    str(executable),
                ],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(compile_result.returncode, 0, compile_result.stderr)
            run_result = subprocess.run(
                [str(executable)], capture_output=True, text=True, check=False
            )
            self.assertEqual(run_result.returncode, 0, run_result.stderr)


if __name__ == "__main__":
    unittest.main()
