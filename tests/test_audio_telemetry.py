"""Host test for bounded, deferred, rate-limited telemetry formatting."""

from pathlib import Path
import subprocess
import tempfile
import textwrap
import unittest


ROOT = Path(__file__).resolve().parents[1]


class AudioTelemetryTests(unittest.TestCase):
    def test_detailed_snapshot_is_deferred_atomic_and_retried_without_data_loss(self):
        harness = textwrap.dedent(
            r"""
            #include <stdbool.h>
            #include <stddef.h>
            #include <stdint.h>
            #include <stdio.h>
            #include <string.h>
            #include "services/audio_telemetry.h"

            static unsigned int enqueue_calls;
            static unsigned int messages;
            static bool reject_once;
            static char last_message[1537];

            void debug_uart_tx_init(void) {}
            uint32_t debug_uart_tx_drop_count(void) { return 7U; }
            bool debug_uart_tx_try_enqueue(const char *message, size_t length)
            {
                ++enqueue_calls;
                if (reject_once) {
                    reject_once = false;
                    return false;
                }
                if (length >= sizeof(last_message)) return false;
                memcpy(last_message, message, length);
                last_message[length] = '\0';
                ++messages;
                return true;
            }

            int main(void)
            {
                audio_telemetry_snapshot_t snapshot = {0};
                audio_telemetry_init();
                for (unsigned int i = 0; i < 100U; ++i) {
                    audio_telemetry_record_processing_us(1000U + i);
                    audio_telemetry_record_invoke_us(300U);
                    audio_telemetry_record_e2e_us(
                        (i == 99U) ? 17000U : 15000U,
                        10U + i + ((i >= 50U) ? 1U : 0U));
                }
                snapshot.frame_count = 99U;
                audio_telemetry_poll(&snapshot, false);
                if (messages != 0U) return 1;
                snapshot.frame_count = 100U;
                snapshot.invoke_count = 100U;
                snapshot.arena_used_bytes = 1484U;
                snapshot.arena_capacity_bytes = 131072U;
                snapshot.dsp_static_bytes = 18000U;
                snapshot.stack_configured_bytes = 8192U;
                snapshot.macs_per_inference = 45056U;
                snapshot.npu_operator_count = 30U;
                snapshot.cpu_operator_count = 0U;
                snapshot.model_flash_bytes = 59232U;
                snapshot.fault_injected = 100U;
                snapshot.diagnostics_enabled = 1U;
                snapshot.stack_high_water_bytes = 4096U;
                snapshot.stack_headroom_bytes = 4096U;
                snapshot.audio.frames = 1U;
                snapshot.audio.pcm_samples = 4U;
                snapshot.audio.input_energy = 400U;
                snapshot.audio.model_input_energy = 1600U;
                snapshot.audio.output_energy = 100U;
                snapshot.audio.input_peak = 10U;
                snapshot.audio.model_input_peak = 40U;
                snapshot.audio.output_peak = 5U;
                snapshot.audio.feature_count = 4U;
                snapshot.audio.feature_sum = -4;
                snapshot.audio.feature_min = -128;
                snapshot.audio.feature_max = 127;
                snapshot.audio.feature_sat_low = 1U;
                snapshot.audio.feature_sat_high = 2U;
                snapshot.audio.gain_count = 4U;
                snapshot.audio.gain_sum = 0;
                snapshot.audio.gain_min = -128;
                snapshot.audio.gain_max = 127;
                snapshot.audio.gain_near_zero = 1U;
                snapshot.audio.gain_near_unity = 2U;
                snapshot.audio.state1_min = -10;
                snapshot.audio.state1_max = 11;
                snapshot.audio.state2_min = -12;
                snapshot.audio.state2_max = 13;
                snapshot.audio.state_sat_count = 3U;
                snapshot.audio.spectrum_input_energy_q30 = 1000000ULL;
                snapshot.audio.spectrum_output_energy_q30 = 998001ULL;
                snapshot.audio.ifft_energy_q30 = 40000ULL;
                snapshot.audio.wola_energy_q30 = 10000ULL;
                snapshot.audio.ifft_sample_count = 100U;
                snapshot.audio.wola_sample_count = 100U;
                snapshot.audio.bin_gain_sum_q15 = 32768ULL * 161ULL;
                snapshot.audio.bin_gain_count = 161U;
                snapshot.audio.bin_gain_min_q15 = 32768U;
                snapshot.audio.bin_gain_max_q15 = 32768U;
                snapshot.audio.wola_peak_q15 = 456U;
                snapshot.audio.complex_min = -8;
                snapshot.audio.complex_max = 9;
                snapshot.audio.erb_cache_min = -3;
                snapshot.audio.erb_cache_max = 4;
                snapshot.audio.enc0_min = -5;
                snapshot.audio.enc0_max = 6;
                snapshot.audio.df1_min = -7;
                snapshot.audio.df1_max = 8;
                for (unsigned int i = 0; i < 100U; ++i) {
                    audio_telemetry_record_audio_stats(&snapshot.audio);
                }
                audio_telemetry_poll(&snapshot, true);
                if (messages != 0U) return 2;
                snapshot.frame_count = 101U;
                reject_once = true;
                audio_telemetry_poll(&snapshot, false);
                if (messages != 0U || enqueue_calls != 1U) return 3;
                audio_telemetry_poll(&snapshot, false);
                if (messages != 1U || enqueue_calls != 2U) return 4;
                if (!strstr(last_message, "[PERF]") ||
                    !strstr(last_message, "[MODEL]") ||
                    !strstr(last_message, "[AUDIO]") ||
                    !strstr(last_message, "[DSP]") ||
                    !strstr(last_message, "[DFN]") ||
                    !strstr(last_message, "[STACK]")) return 5;
                if (!strstr(last_message, "enh_us=1049/1099/1098") ||
                    !strstr(last_message, "npu_us=300/300/300")) return 6;
                if (!strstr(last_message, "e2e_gap=1") ||
                    !strstr(last_message, " e2e_16ms=1 ")) return 7;
                if (!strstr(last_message, "mac_inf=45056") ||
                    !strstr(last_message, "stream_mmac_s_x1000=4506")) {
                    fprintf(stderr, "%s", last_message);
                    return 8;
                }
                if (!strstr(last_message, "raw_rms=10") ||
                    !strstr(last_message, "model_rms=20") ||
                    !strstr(last_message, "out_rms=5") ||
                    !strstr(last_message, "rms_ratio_milli=500")) return 9;
                if (!strstr(last_message, "gain_milli=0/500/996") ||
                    !strstr(last_message, "gain_zero_pct=25") ||
                    !strstr(last_message, "gain_unity_pct=50")) return 10;
                if (!strstr(last_message, "fault_injected=100") ||
                    !strstr(last_message, "frames=100") ||
                    !strstr(last_message, "complex_q=-8/9/0") ||
                    !strstr(last_message, "headroom=4096 gate=pass") ||
                    !strstr(last_message, "valid=1") ||
                    !strstr(last_message, "bin_gain_milli=1000/1000/1000") ||
                    !strstr(last_message, "spec_ratio_milli=999") ||
                    !strstr(last_message, "wola_peak=456")) {
                    fprintf(stderr, "%s", last_message);
                    return 11;
                }
                if (strlen(last_message) >= 1536U) return 12;
                audio_telemetry_poll(&snapshot, false);
                if (messages != 1U) return 13;

                for (unsigned int i = 0; i < 100U; ++i) {
                    audio_telemetry_record_processing_us(1000U);
                }
                memset(&snapshot.audio, 0, sizeof(snapshot.audio));
                snapshot.frame_count = 201U;
                audio_telemetry_poll(&snapshot, false);
                if (messages != 2U) return 14;
                if (!strstr(last_message, "gain_milli=0/0/0") ||
                    !strstr(last_message, "valid=0")) return 15;
                return 0;
            }
            """
        )
        with tempfile.TemporaryDirectory() as temporary_directory:
            temporary = Path(temporary_directory)
            source = temporary / "telemetry_test.c"
            executable = temporary / "telemetry_test.exe"
            source.write_text(harness, encoding="utf-8")
            result = subprocess.run(
                [
                    "gcc",
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(ROOT),
                    str(source),
                    str(ROOT / "services" / "audio_telemetry.c"),
                    "-o",
                    str(executable),
                ],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            run = subprocess.run(
                [str(executable)], capture_output=True, text=True, check=False
            )
            self.assertEqual(run.returncode, 0, run.stderr)


if __name__ == "__main__":
    unittest.main()
