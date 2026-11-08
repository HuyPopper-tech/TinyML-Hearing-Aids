"""Release-only probe must stop audio before its UART summary."""

from pathlib import Path
import subprocess
import tempfile
import textwrap
import unittest


ROOT = Path(__file__).resolve().parents[1]


class DeepfirAudioReleaseProbeTests(unittest.TestCase):
    def test_probe_is_opt_in_and_reports_after_stop(self):
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
            static uint32_t stops;
            static uint32_t logs;
            static uint32_t flushes;
            static uint32_t early_uart;
            static bool submitted;
            static char last_log[1024];
            static char result_log[1024];
            static const char *failure = "";

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
            #if M55M1_AUDIO_RELEASE_PROBE
            const volatile audio_startup_trace_t *audio_transport_get_startup_trace(void) {
                static audio_startup_trace_t trace = {.count=1, .events={{
                    .phase=AUDIO_STARTUP_TX_TD, .cycle=11, .tx_fifo_words=8,
                    .tx_threshold_words=8, .tx_dma_ctl=0x1234, .primask=0,
                }}};
                if (stops != 1U) ++early_uart;
                return &trace;
            }
            #endif
            bool audio_transport_take_capture(
                int16_t destination[AUDIO_TRANSPORT_SAMPLES_PER_BLOCK],
                audio_transport_capture_timing_t *timing)
            {
                ++frame;
                memset(destination, 0, AUDIO_TRANSPORT_SAMPLES_PER_BLOCK * sizeof(int16_t));
                timing->sequence = frame;
                if (!strcmp(failure, "sequence")) ++timing->sequence;
                timing->completed_cycle = frame * 1000U;
                return true;
            }
            audio_transport_status_t audio_transport_submit_tagged(
                const int16_t samples[AUDIO_TRANSPORT_SAMPLES_PER_BLOCK],
                const audio_transport_block_tag_t *tag)
            {
                (void)samples;
                (void)tag;
                submitted = true;
                return !strcmp(failure, "submit") ? AUDIO_TRANSPORT_STATUS_BUSY : AUDIO_TRANSPORT_STATUS_OK;
            }
            void audio_transport_get_health(audio_transport_health_t *health)
            {
                *health = (audio_transport_health_t){
                    .dmic_overrun_blocks = !strcmp(failure, "transport") ? 1U : 0U,
                    .dac_underrun_blocks = 0U,
                };
            }
            uint32_t audio_transport_cycles_now(void)
            {
                return frame * 1000U +
                    ((!strcmp(failure, "deadline") ||
                      (!strcmp(failure, "publish") && submitted)) ? 1200U : 800U);
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
            void deepfir_inference_reset(void) {}
            bool deepfir_inference_process(const int16_t *input, int16_t *output,
                                           uint32_t count)
            {
                (void)input;
                memset(output, 0, count * sizeof(int16_t));
                output[0] = 32767;
                return strcmp(failure, "invoke") != 0;
            }
            void deepfir_inference_get_last_timing(deepfir_inference_timing_t *timing)
            {
                *timing = (deepfir_inference_timing_t){
                    .feature_us = 250U, .pre_fft_us = 20U,
                    .fft_us = 80U, .post_fft_us = 150U,
                    .npu_us = 450U,
                    .invoke_us = 500U, .invoke_overhead_us = 50U,
                    .dsp_us = 100U, .total_us = 850U,
            #if M55M1_AUDIO_RELEASE_PROBE
                    .bridge_us = 40U, .carry_us = 50U, .taps_us = 60U,
                    .fir_us = 100U, .pcm_us = 7U, .other_us = 50U,
            #endif
                };
            }
            #if M55M1_AUDIO_RELEASE_PROBE
            void deepfir_inference_get_dsp_status(
                bool *feature_ready, bool *fir_ready, uint32_t *self_test_us)
            {
                *feature_ready = true;
                *fir_ready = true;
                *self_test_us = 0U;
            }
            #endif
            void debug_uart_tx_init(void)
            {
                if (stops != 1U) ++early_uart;
            }
            bool debug_uart_tx_try_enqueue(const char *message, size_t length)
            {
                if (stops != 1U) ++early_uart;
                if (length >= sizeof(last_log)) return false;
                if (strstr(message, "[AUDIO_RELEASE_RESULT]")) {
                    if (length >= sizeof(result_log)) return false;
                    memcpy(result_log, message, length); result_log[length] = '\0';
                } else if (strstr(message, "[AUDIO_RELEASE_PROBE]")) {
                    memcpy(last_log, message, length);
                    last_log[length] = '\0';
                } else if (strstr(message, "[AUDIO_STARTUP]")) {
                    if (!strstr(message,"phase=TX_TD") || !strstr(message,"txfifo=8") ||
                        !strstr(message,"txth=8") || !strstr(message,"txctl=00001234")) return false;
                }
                ++logs;
                return true;
            }
            void debug_uart_tx_flush_blocking(void)
            {
                if (stops != 1U) ++early_uart;
                ++flushes;
            }
            uint32_t debug_uart_tx_drop_count(void) { return 0U; }

            int main(int argc, char **argv)
            {
                if (!audio_pipeline_init() || !audio_pipeline_start()) return 1;
                if (argc > 1) {
                    failure = argv[1];
                    audio_pipeline_poll();
                    if (stops != 1U || logs != 3U || flushes != 3U || early_uart ||
                        !strstr(result_log, "result=FAIL") || !strstr(result_log, "restarts=0")) {
                        fprintf(stderr, "%s\n%s", last_log, result_log); return 7;
                    }
                    const char *fault = !strcmp(failure,"transport") ? "fault=0 " :
                        !strcmp(failure,"sequence") ? "fault=1 " :
                        !strcmp(failure,"invoke") ? "fault=2 " :
                        !strcmp(failure,"deadline") ? "fault=3 " :
                        !strcmp(failure,"publish") ? "fault=5 " : "fault=4 ";
                    if (!strstr(result_log, fault)) return 9;
                    if ((!strcmp(failure, "deadline") || !strcmp(failure, "publish") ||
                         !strcmp(failure, "submit")) &&
                        (!strstr(last_log, "feature_max_us=250") ||
                         !strstr(last_log, "total_max_us=850") ||
                         !strstr(last_log, "total_mean_us=850") ||
                         !strstr(last_log, !strcmp(failure, "submit") ?
                                 "e2e_max_us=800" : "e2e_max_us=1200") ||
                         !strstr(last_log, !strcmp(failure, "submit") ?
                                 "late_1ms=0" : "late_1ms=1") ||
                         !strstr(result_log, "last_pre_fft_us=20") ||
                         !strstr(result_log, "last_fft_us=80") ||
                         !strstr(result_log, "last_post_fft_us=150"))) {
                        fprintf(stderr, "%s\n%s", last_log, result_log); return 10;
                    }
                    const uint32_t stopped_frame = frame;
                    audio_pipeline_poll();
                    if (frame != stopped_frame || stops != 1U || logs != 3U) return 8;
                    return 0;
                }
                for (uint32_t index = 0U; index < 59999U; ++index) {
                    audio_pipeline_poll();
                }
                if (stops || logs || flushes || early_uart) return 2;
                audio_pipeline_poll();
            #if M55M1_AUDIO_RELEASE_PROBE
                if (stops != 1U || logs != 3U || flushes != 3U || early_uart) return 3;
                if (!strstr(last_log, "[AUDIO_RELEASE_PROBE]") ||
                    !strstr(last_log, "frame=60000") ||
                    !strstr(last_log, "dmic_overrun=0") ||
                    !strstr(last_log, "dac_underrun=0") ||
                    !strstr(last_log, "feature_mve_ready=1") ||
                    !strstr(last_log, "fir_mve_ready=1") ||
                    !strstr(last_log, "mve_test_us=0") ||
                    !strstr(last_log, "feature_max_us=250") ||
                    !strstr(last_log, "pre_fft_max_us=20") ||
                    !strstr(last_log, "fft_max_us=80") ||
                    !strstr(last_log, "post_fft_max_us=150") ||
                    !strstr(last_log, "dsp_max_us=100") ||
                    !strstr(last_log, "bridge_max_us=40") ||
                    !strstr(last_log, "carry_max_us=50") ||
                    !strstr(last_log, "taps_max_us=60") ||
                    !strstr(last_log, "fir_max_us=100") ||
                    !strstr(last_log, "pcm_max_us=7") ||
                    !strstr(last_log, "other_max_us=50") ||
                    !strstr(last_log, "bridge_mean_us=40") ||
                    !strstr(last_log, "carry_mean_us=50") ||
                    !strstr(last_log, "taps_mean_us=60") ||
                    !strstr(last_log, "fir_mean_us=100") ||
                    !strstr(last_log, "pcm_mean_us=7") ||
                    !strstr(last_log, "other_mean_us=50") ||
                    !strstr(last_log, "total_mean_us=850") ||
                    !strstr(last_log, "e2e_mean_us=800") ||
                    !strstr(last_log, "capture_last_seq=60000") ||
                    !strstr(result_log, "result=COMPLETE") ||
                    !strstr(last_log, "late_1ms=0")) {
                    fprintf(stderr, "%s", last_log);
                    return 4;
                }
                audio_pipeline_poll();
                if (frame != 60000U || stops != 1U || logs != 3U) return 5;
            #else
                if (stops || logs || flushes || early_uart) return 6;
            #endif
                return 0;
            }
            """
        )
        with tempfile.TemporaryDirectory() as temporary_directory:
            temporary = Path(temporary_directory)
            source = temporary / "audio_release_probe_test.c"
            source.write_text(harness, encoding="utf-8")
            for enabled in (0, 1):
                executable = temporary / f"audio_release_probe_{enabled}.exe"
                result = subprocess.run(
                    ["gcc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                     "-DM55M1_RUNTIME_TELEMETRY=0",
                     f"-DM55M1_AUDIO_RELEASE_PROBE={enabled}",
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
                if enabled:
                    for failure in ('transport', 'sequence', 'invoke', 'deadline', 'submit', 'publish'):
                        with self.subTest(failure=failure):
                            run = subprocess.run([str(executable), failure], capture_output=True, text=True)
                            self.assertEqual(run.returncode, 0, run.stderr)


if __name__ == "__main__":
    unittest.main()
