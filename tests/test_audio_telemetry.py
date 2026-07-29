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
            static char last_message[1024];

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
                        (i == 99U) ? 45000U : 25000U,
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
                snapshot.audio.frames = 100U;
                snapshot.audio.pcm_samples = 4U;
                snapshot.audio.input_energy = 400U;
                snapshot.audio.output_energy = 100U;
                snapshot.audio.input_peak = 10U;
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
                    !strstr(last_message, "[AUDIO]")) return 5;
                if (!strstr(last_message, "enh_us=1049/1099/1098") ||
                    !strstr(last_message, "npu_us=300/300/300")) return 6;
                if (!strstr(last_message, "e2e_gap=1") ||
                    !strstr(last_message, "e2e_40ms=1")) return 7;
                if (!strstr(last_message, "mac_inf=45056") ||
                    !strstr(last_message, "stream_mmac_s_x1000=4506")) {
                    fprintf(stderr, "%s", last_message);
                    return 8;
                }
                if (!strstr(last_message, "in_rms=10") ||
                    !strstr(last_message, "out_rms=5") ||
                    !strstr(last_message, "rms_ratio_milli=500")) return 9;
                if (!strstr(last_message, "gain_milli=0/500/996") ||
                    !strstr(last_message, "gain_zero_pct=25") ||
                    !strstr(last_message, "gain_unity_pct=50")) return 10;
                audio_telemetry_poll(&snapshot, false);
                return (messages == 1U) ? 0 : 11;
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
                    "-DM55M1_ERB_GRU_DIAGNOSTICS=1",
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
