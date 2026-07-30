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
                erb_gru_audio_diagnostics_record_dsp(
                    1000ULL, 999ULL, 400ULL, 100ULL, 4U, 2U,
                    32768ULL * 3ULL, 3U, 32768U, 32768U, 123U);
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
                if (result.spectrum_input_energy_q30 != 1000ULL ||
                    result.spectrum_output_energy_q30 != 999ULL ||
                    result.ifft_energy_q30 != 400ULL ||
                    result.wola_energy_q30 != 100ULL ||
                    result.ifft_sample_count != 4U ||
                    result.wola_sample_count != 2U) return 9;
                if (result.bin_gain_sum_q15 != 32768ULL * 3ULL ||
                    result.bin_gain_count != 3U ||
                    result.bin_gain_min_q15 != 32768U ||
                    result.bin_gain_max_q15 != 32768U ||
                    result.wola_peak_q15 != 123U) return 10;
                return erb_gru_audio_diagnostics_take(&result) ? 11 : 0;
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
    def test_unity_fallback_traces_unity_gain_and_dsp_round_trip(self):
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
                (void)gain;
                (void)next_state1;
                (void)next_state2;
                return false;
            }
            extern "C" void erb_gru_inference_get_performance(
                erb_gru_inference_performance_t *performance)
            {
                std::memset(performance, 0, sizeof(*performance));
            }

            int main(void)
            {
                int16_t input[160];
                int16_t output[160];
                erb_gru_audio_diagnostics_t diagnostics;

                if (!erb_gru_audio_enhancer_init()) return 1;
                for (uint32_t hop = 0U; hop < 20U; ++hop) {
                    for (uint32_t index = 0U; index < 160U; ++index) {
                        const uint32_t phase = index & 15U;
                        static const int16_t tone[16] = {
                            0, 3827, 7071, 9239, 10000, 9239, 7071, 3827,
                            0, -3827, -7071, -9239, -10000, -9239, -7071, -3827
                        };
                        input[index] = tone[phase];
                    }
                    (void)erb_gru_audio_enhancer_process(input, output);
                }
                if (!erb_gru_audio_enhancer_take_diagnostics(&diagnostics)) return 2;
                if (diagnostics.gain_count != 0U) return 3;
                if (diagnostics.bin_gain_count == 0U ||
                    diagnostics.bin_gain_min_q15 != 32768U ||
                    diagnostics.bin_gain_max_q15 != 32768U ||
                    diagnostics.bin_gain_sum_q15 !=
                        (uint64_t)diagnostics.bin_gain_count * 32768ULL) return 4;
                if (diagnostics.spectrum_input_energy_q30 == 0U) return 5;
                const uint64_t ratio_million =
                    diagnostics.spectrum_output_energy_q30 * 1000000ULL /
                    diagnostics.spectrum_input_energy_q30;
                if (ratio_million < 998001ULL || ratio_million > 1002001ULL) return 6;
                if (diagnostics.ifft_sample_count == 0U ||
                    diagnostics.wola_sample_count == 0U ||
                    diagnostics.wola_peak_q15 == 0U) return 7;
                return 0;
            }
            """
        )
        with tempfile.TemporaryDirectory() as temporary_directory:
            temporary = Path(temporary_directory)
            source = temporary / "unity_dsp_diagnostics_test.cpp"
            executable = temporary / "unity_dsp_diagnostics_test.exe"
            source.write_text(harness, encoding="utf-8")
            compile_result = subprocess.run(
                [
                    shutil.which("g++"),
                    "-std=c++11",
                    "-O3",
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
                std::memset(gain, 126, 32U);
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
                if (diagnostics.bin_gain_count != 161U ||
                    diagnostics.bin_gain_min_q15 < 32479U ||
                    diagnostics.bin_gain_max_q15 > 32545U) return 10;
                const uint32_t bin_gain_mean = (uint32_t)(
                    diagnostics.bin_gain_sum_q15 / diagnostics.bin_gain_count);
                if (bin_gain_mean < 32479U || bin_gain_mean > 32545U) return 11;
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
