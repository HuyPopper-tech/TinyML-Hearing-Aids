"""Host-compiled parity test for the production ERB-GRU DSP service."""

from __future__ import annotations

from pathlib import Path
import shutil
import subprocess
import tempfile
import textwrap
import unittest


ROOT = Path(__file__).resolve().parents[1]
KISSFFT = ROOT / "libs" / "tensorflow" / "third_party" / "kissfft"


class ErbGruHostParityTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which("g++"), "g++ is required for host DSP parity")
    def test_all_streams_match_golden_pipeline(self):
        """Wrong framing, DSP layout, recurrent state, or WOLA must break PCM parity."""
        harness = textwrap.dedent(
            r"""
            #include <cmath>
            #include <cstdint>
            #include <cstdio>
            #include <cstring>

            #include "models/erb_gru/generated/erb_gru_deploy_contract.h"
            #include "models/erb_gru/generated/golden/golden_sine_1khz.h"
            #include "services/erb_gru/erb_gru_audio_enhancer.h"
            #include "services/erb_gru/erb_gru_inference.h"

            static uint32_t g_frame;
            static int32_t g_feature_mismatch_frame = -1;
            static int32_t g_state1_mismatch_frame = -1;
            static int32_t g_state2_mismatch_frame = -1;
            static int32_t g_max_feature_lsb;

            extern "C" bool erb_gru_inference_init(void)
            {
                return true;
            }

            extern "C" bool erb_gru_inference_invoke(
                const int8_t feature[32], const int8_t state1[64], const int8_t state2[64],
                int8_t gain[32], int8_t next_state1[64], int8_t next_state2[64])
            {
                if (g_frame >= 24U)
                {
                    return false;
                }
                for (uint32_t index = 0; index < 32U; ++index)
                {
                    const int8_t expected =
                        erb_gru_sine_1khz_erb_feature_int8[g_frame * 32U + index];
                    const int32_t error = static_cast<int32_t>(feature[index]) - expected;
                    const int32_t magnitude = (error < 0) ? -error : error;
                    if (magnitude > g_max_feature_lsb)
                    {
                        g_max_feature_lsb = magnitude;
                    }
                    if ((magnitude > 1) && (g_feature_mismatch_frame < 0))
                    {
                        g_feature_mismatch_frame = static_cast<int32_t>(g_frame);
                    }
                }
                if ((g_state1_mismatch_frame < 0) &&
                    (std::memcmp(state1,
                                 erb_gru_sine_1khz_state1_int8 + g_frame * 64U,
                                 64U) != 0))
                {
                    g_state1_mismatch_frame = static_cast<int32_t>(g_frame);
                }
                if ((g_state2_mismatch_frame < 0) &&
                    (std::memcmp(state2,
                                 erb_gru_sine_1khz_state2_int8 + g_frame * 64U,
                                 64U) != 0))
                {
                    g_state2_mismatch_frame = static_cast<int32_t>(g_frame);
                }
                std::memcpy(gain, erb_gru_sine_1khz_gain_int8 + g_frame * 32U, 32U);
                std::memcpy(next_state1,
                            erb_gru_sine_1khz_next_state1_int8 + g_frame * 64U, 64U);
                std::memcpy(next_state2,
                            erb_gru_sine_1khz_next_state2_int8 + g_frame * 64U, 64U);
                ++g_frame;
                return true;
            }

            extern "C" void erb_gru_inference_get_performance(
                erb_gru_inference_performance_t *performance)
            {
                if (performance != nullptr)
                {
                    std::memset(performance, 0, sizeof(*performance));
                }
            }

            int main()
            {
                int16_t actual[4000] = {};
                int16_t block[160] = {};
                uint32_t output_offset = 0U;

                if (!erb_gru_audio_enhancer_init())
                {
                    return 10;
                }
                for (uint32_t input_offset = 0U; input_offset < 4000U; input_offset += 160U)
                {
                    const erb_gru_audio_result_t result = erb_gru_audio_enhancer_process(
                        erb_gru_sine_1khz_pcm_input_int16 + input_offset, block);
                    if (input_offset == 0U)
                    {
                        if (result != ERB_GRU_AUDIO_RESULT_PREROLL_SILENCE)
                        {
                            return 11;
                        }
                        continue;
                    }
                    if (result != ERB_GRU_AUDIO_RESULT_ENHANCED)
                    {
                        return 12;
                    }
                    std::memcpy(actual + output_offset, block, sizeof(block));
                    output_offset += 160U;
                }
                if (!erb_gru_audio_enhancer_flush(actual + output_offset))
                {
                    return 13;
                }
                if ((g_feature_mismatch_frame >= 0) || (g_state1_mismatch_frame >= 0) ||
                    (g_state2_mismatch_frame >= 0) || (g_frame != 24U))
                {
                    std::printf("frames=%lu feature_bad=%ld feature_max=%ld state1_bad=%ld state2_bad=%ld\n",
                                (long)g_frame, (long)g_feature_mismatch_frame,
                                (long)g_max_feature_lsb,
                                (long)g_state1_mismatch_frame, (long)g_state2_mismatch_frame);
                    return 14;
                }

                double signal = 0.0;
                double noise = 0.0;
                double squared_error = 0.0;
                int32_t max_error = 0;
                for (uint32_t index = 0U; index < 4000U; ++index)
                {
                    const int32_t expected = erb_gru_sine_1khz_pcm_output_board_int16[index];
                    const int32_t error = static_cast<int32_t>(actual[index]) - expected;
                    const int32_t magnitude = (error < 0) ? -error : error;
                    signal += static_cast<double>(expected) * expected;
                    noise += static_cast<double>(error) * error;
                    squared_error += static_cast<double>(error) * error;
                    if (magnitude > max_error)
                    {
                        max_error = magnitude;
                    }
                }
                const double snr_db = 10.0 * std::log10(signal / noise);
                const double rmse = std::sqrt(squared_error / 4000.0);
                std::printf("snr=%.3f rmse=%.3f max=%ld\n", snr_db, rmse, (long)max_error);
                return ((snr_db >= 60.0) && (rmse <= 2.0) && (max_error <= 16)) ? 0 : 15;
            }
            """
        )

        for scenario in (
            "silence",
            "sine_1khz",
            "clean_speech",
            "speech_noise",
            "near_full_scale",
        ):
            with self.subTest(scenario=scenario), tempfile.TemporaryDirectory() as temporary_directory:
                temporary = Path(temporary_directory)
                harness_path = temporary / "erb_gru_host_parity.cpp"
                executable = temporary / "erb_gru_host_parity.exe"
                scenario_harness = harness.replace("sine_1khz", scenario)
                scenario_harness = scenario_harness.replace(
                    "const double snr_db = 10.0 * std::log10(signal / noise);",
                    "const double snr_db = (noise == 0.0) ? 999.0 : "
                    "10.0 * std::log10(signal / noise);",
                )
                harness_path.write_text(scenario_harness, encoding="utf-8")
                command = [
                    shutil.which("g++"),
                    "-std=c++11",
                    "-O2",
                    "-DMODEL_GOLDEN_TEST_DATA=1",
                    "-I",
                    str(ROOT),
                    "-I",
                    str(KISSFFT),
                    str(ROOT / "services" / "erb_gru" / "erb_gru_audio_enhancer.cpp"),
                    str(KISSFFT / "kiss_fft.c"),
                    str(KISSFFT / "tools" / "kiss_fftr.c"),
                    str(harness_path),
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
