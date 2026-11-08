"""Host check that live DeepFIR telemetry reports audio faults without blocking."""

from pathlib import Path
import subprocess
import tempfile
import textwrap
import unittest


ROOT = Path(__file__).resolve().parents[1]


class DeepfirAudioRuntimeDebugTests(unittest.TestCase):
    def test_debug_reports_faults_without_blocking_live_audio(self):
        harness = textwrap.dedent(
            r"""
            #include <stdbool.h>
            #include <stddef.h>
            #include <stdint.h>
            #include <stdio.h>
            #include <string.h>
            #include "drivers/audio_transport/audio_transport.h"
            #include "services/deepfir/deepfir_inference_service.h"
            #include "services/audio_pipeline/audio_pipeline.h"

            static uint32_t frame;
            static uint32_t logs;
            static unsigned stops, resets, submissions;
            static bool fail_model;
            static uint32_t blocking_flushes;
            static char last_log[1024];

            bool audio_transport_init(audio_transport_init_report_t *report,
                                      audio_transport_progress_callback_t callback,
                                      void *context)
            {
                (void)callback;
                (void)context;
                report->clocks_valid = true;
                return true;
            }
            audio_transport_status_t audio_transport_start(void)
            {
                return AUDIO_TRANSPORT_STATUS_OK;
            }
            void audio_transport_wait_for_work(void) {}
            void audio_transport_stop_safe(void) { ++stops; }
            bool audio_transport_take_capture(
                int16_t destination[AUDIO_TRANSPORT_SAMPLES_PER_BLOCK],
                                              audio_transport_capture_timing_t *timing)
            {
                ++frame;
                memset(destination, 0, AUDIO_TRANSPORT_SAMPLES_PER_BLOCK * sizeof(int16_t));
                destination[0] = 32767;
                timing->sequence = frame;
                timing->completed_cycle = frame * 1000U;
                return frame <= 1001U;
            }
            audio_transport_status_t audio_transport_submit_tagged(
                const int16_t samples[AUDIO_TRANSPORT_SAMPLES_PER_BLOCK],
                const audio_transport_block_tag_t *tag)
            {
                ++submissions;
                (void)samples;
                (void)tag;
                return AUDIO_TRANSPORT_STATUS_OK;
            }
            void audio_transport_get_health(audio_transport_health_t *health)
            {
                *health = (audio_transport_health_t){.dmic_overrun_blocks = 0U,
                    .dac_underrun_blocks = 0U};
            }
            uint32_t audio_transport_cycles_now(void)
            {
                return frame * 1000U + 800U;
            }
            uint32_t audio_transport_cycles_elapsed(uint32_t start, uint32_t end)
            {
                return end - start;
            }
            uint32_t audio_transport_cycles_to_us(uint32_t cycles)
            {
                return cycles;
            }
            bool deepfir_inference_init(void) { return true; }
            void deepfir_inference_reset(void) { ++resets; }
            bool deepfir_inference_process(const int16_t *input, int16_t *output,
                                           uint32_t count)
            {
                (void)input;
                if (count != AUDIO_TRANSPORT_SAMPLES_PER_BLOCK || fail_model) {
                    return false;
                }
                memset(output, 0, count * sizeof(int16_t));
                output[0] = -32768;
                return true;
            }
            void deepfir_inference_get_last_timing(deepfir_inference_timing_t *timing)
            {
                *timing = (deepfir_inference_timing_t){.feature_us = 200U,
                    .pre_fft_us = 20U, .fft_us = 70U, .post_fft_us = 110U,
                    .npu_us = 300U, .invoke_us = 350U,
                    .invoke_overhead_us = 50U, .dsp_us = 100U,
                    .total_us = 700U, .bridge_us = 10U, .carry_us = 20U,
                    .taps_us = 30U, .fir_us = 40U, .other_us = 50U};
            }
            void deepfir_inference_get_dsp_status(
                bool *feature_ready, bool *fir_ready, uint32_t *self_test_us)
            {
                *feature_ready = true;
                *fir_ready = true;
                *self_test_us = 77U;
            }
            void debug_uart_tx_init(void) {}
            bool debug_uart_tx_try_enqueue(const char *message, size_t length)
            {
                if (length >= sizeof(last_log)) return false;
                memcpy(last_log, message, length);
                last_log[length] = '\0';
                ++logs;
                return true;
            }
            void debug_uart_tx_flush_blocking(void) { ++blocking_flushes; }
            uint32_t debug_uart_tx_drop_count(void) { return 0U; }

            int main(void)
            {
                if (!audio_pipeline_init() || !audio_pipeline_start()) return 1;
                for (uint32_t index = 0U; index < 1000U; ++index) {
                    audio_pipeline_poll();
                }
                if (blocking_flushes != 0U || logs != 1U) return 2;
                if (!strstr(last_log, "frame=1000") ||
                    !strstr(last_log, "model_fail=0") ||
                    !strstr(last_log, "submit_busy=0") ||
                    !strstr(last_log, "dmic_overrun=0") ||
                    !strstr(last_log, "dac_underrun=0") ||
                    !strstr(last_log, "in_clip=1000") ||
                    !strstr(last_log, "out_clip=1000") ||
                    !strstr(last_log, "feature_mve=1 fir_mve=1 mve_test_us=77") ||
                    !strstr(last_log, "pre_fft_max_us=20") ||
                    !strstr(last_log, "fir_max_us=40") ||
                    !strstr(last_log, "overhead_max_us=50") ||
                    !strstr(last_log, "e2e_max_us=800") ||
                    !strstr(last_log, "late_1ms=0")) {
                    fprintf(stderr, "%s", last_log);
                    return 3;
                }
                // The next failed invocation triggers full reset/restart, never submission.
                fail_model = true;
                unsigned before = submissions;
                unsigned before_reset = resets;
                audio_pipeline_poll();
                audio_pipeline_stats_t stats;
                audio_pipeline_get_stats(&stats);
                if (stops != 1 || resets <= before_reset || submissions != before) return 4;
                if (stats.model_faults != 1 || stats.restarts != 1) return 5;
                return 0;
            }
            """
        )
        with tempfile.TemporaryDirectory() as temporary_directory:
            temporary = Path(temporary_directory)
            source = temporary / "deepfir_audio_debug_test.c"
            executable = temporary / "deepfir_audio_debug_test.exe"
            source.write_text(harness, encoding="utf-8")
            result = subprocess.run(
                ["gcc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-DM55M1_RUNTIME_TELEMETRY=1",
                 "-DM55M1_ENABLE_DAC_I2S_OUTPUT=1", "-I", str(ROOT),
                 str(source), str(ROOT / "services/audio_pipeline/audio_pipeline.c"),
                 "-o", str(executable)],
                capture_output=True, text=True, check=False,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            run = subprocess.run(
                [str(executable)], capture_output=True, text=True, check=False
            )
            self.assertEqual(run.returncode, 0, run.stderr)


if __name__ == "__main__":
    unittest.main()
