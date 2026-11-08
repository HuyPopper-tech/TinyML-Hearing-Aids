"""Host check that live DeepFIR telemetry reports audio faults without blocking."""

from pathlib import Path
import subprocess
import tempfile
import textwrap
import unittest


ROOT = Path(__file__).resolve().parents[1]


class DeepfirAudioRuntimeDebugTests(unittest.TestCase):
    def test_native_tensor_diagnostics_preserve_exact_binding_and_report_qparams(self):
        source = (ROOT / "services/deepfir/deepfir_inference_service.cpp").read_text()
        # Compile production lookup/formatting against small interpreter metadata doubles.
        # These tests do not execute TFLM or claim controller parity.
        helpers = source[source.index("void trace_init("):source.index("#define DEEPFIR_TRACE_INIT")]
        lookup = source[source.index("TfLiteTensor *find_tensor("):source.index("uint32_t elapsed_us(")]
        harness = r'''
        #include <cassert>
        #include <cstdio>
        #include <cstdint>
        #include <cstring>
        #include <string>
        #include "services/deepfir/deepfir_tensor_binding.h"
        static bool g_initialized, g_processing, accessible = true;
        static uint32_t cycle;
        uint32_t audio_transport_cycles_now() { return ++cycle; }
        static std::string logs;
        static unsigned drains;
        static bool accept_log = true;
        bool debug_uart_tx_try_enqueue(const char *message, size_t bytes) {
            if (!accept_log) return false;
            logs.append(message, bytes); return true;
        }
        void debug_uart_tx_flush_blocking() { ++drains; }
        enum { kTfLiteInt16 = 7 };
        struct Dims { int size, data[4]; } dims;
        struct Tensor {
            Dims *dims;
            struct { int16_t *i16; } data;
            size_t bytes;
            struct { float scale; int zero_point; } params;
            int type;
        } tensor;
        using TfLiteTensor = Tensor;
        struct Indices { int value; int Get(unsigned) const { return value; } } indices;
        struct Interpreter {
            const Indices &inputs() { return indices; }
            const Indices &outputs() { return indices; }
            size_t inputs_size() { return 3; }
            size_t outputs_size() { return 4; }
            Tensor *input(unsigned) { return &tensor; }
            Tensor *output(unsigned) { return &tensor; }
        } interpreter;
        static Interpreter *g_interpreter = &interpreter;
        static const void *g_model;
        static const char *name;
        const char *tensor_name(const void *, int) { return name; }
        bool in_sram(const void *, size_t) { return accessible; }
        ''' + helpers + r'''
        #define DEEPFIR_TRACE_INIT(stage, status, code) trace_init(stage, status, code)
        ''' + lookup + r'''
        int main() {
            int16_t storage[774] = {};
            for (bool input : {true, false}) {
                for (unsigned semantic = 0; semantic < (input ? 3U : 4U); ++semantic) {
                    const auto *expected = deepfir_tensor_expected(input, semantic);
                    dims.size = static_cast<int>(expected->rank);
                    std::memcpy(dims.data, expected->shape, sizeof(dims.data));
                    tensor = {&dims, {storage}, expected->bytes,
                              {expected->scale, expected->zero_point}, kTfLiteInt16};
                    indices.value = expected->tensor_index; name = expected->name;
                    logs.clear();
                    assert(find_tensor(input, semantic) == &tensor);
                    assert(logs.find("kind=expected") != std::string::npos);
                    assert(logs.find("kind=actual") != std::string::npos);
                    assert(logs.find("status=OK") != std::string::npos);
                    tensor.params.scale *= 2.0f;
                    logs.clear();
                    assert(find_tensor(input, semantic) == nullptr);
                    assert(logs.find("status=FAIL") != std::string::npos);
                    uint32_t bits; std::memcpy(&bits, &tensor.params.scale, sizeof(bits));
                    char expected_bits[40];
                    std::snprintf(expected_bits, sizeof(expected_bits), "scale_bits=%08lx", (unsigned long)bits);
                    assert(logs.find(expected_bits) != std::string::npos);
                    tensor.params.scale = expected->scale;
                    ++dims.data[1];
                    assert(find_tensor(input, semantic) == nullptr);
                    --dims.data[1]; ++indices.value;
                    assert(find_tensor(input, semantic) == nullptr);
                    --indices.value; tensor.type = 3;
                    assert(find_tensor(input, semantic) == nullptr);
                    tensor.type = kTfLiteInt16; name = "wrong_name";
                    assert(find_tensor(input, semantic) == nullptr);
                }
            }
            accessible = false; logs.clear();
            assert(find_tensor(true, 0) == nullptr);
            assert(logs.find("tensor.storage_or_rank") != std::string::npos);
            assert(find_tensor(true, 99) == nullptr);
            const unsigned before = drains;
            logs.clear(); trace_hop(DEEPFIR_HOP_FFT_BEGIN, 0);
            assert(logs.empty()); // MVE boot self-tests must not consume first-hop markers.
            g_initialized = true; g_processing = true;
            trace_hop(DEEPFIR_HOP_INVOKE_BEGIN, -2);
            trace_report_hop();
            assert(logs.empty()); // Never format/enqueue while the hop is active.
            assert(drains == before);
            g_processing = false;
            trace_report_hop();
            assert(logs.find("stage=invoke.begin code=-2 cycle=1") != std::string::npos);
            assert(drains > before); // Draining is permitted only at explicit stopped reporting.
            logs.clear(); trace_report_hop();
            trace_hop(DEEPFIR_HOP_INVOKE_BEGIN, 0);
            assert(logs.empty());
            g_processing = true;
            for (int i=0; i<20; ++i) trace_hop(DEEPFIR_HOP_ADAPTER_FAIL, i);
            g_processing = false; accept_log = false;
            trace_report_hop(); assert(logs.empty());
            accept_log = true; trace_report_hop();
            assert(logs.find("stage=adapter.fail code=19") != std::string::npos);
            assert(logs.find("overflow=1") != std::string::npos);
            logs.clear(); const unsigned after_report = drains;
            g_initialized = true; trace_init("tensor.binding", "BEGIN", 0);
            assert(logs.empty() && drains == after_report);
            return 0;
        }
        '''
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            cpp = directory / "tensor_trace.cpp"
            executable = directory / "tensor_trace.exe"
            cpp.write_text(harness)
            build = subprocess.run([
                "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                "-DM55M1_RUNTIME_TELEMETRY=1", "-I", str(ROOT), str(cpp),
                str(ROOT / "services/deepfir/deepfir_tensor_binding.c"), "-o", str(executable),
            ], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stderr)

    def test_debug_reports_faults_without_blocking_live_audio(self):
        harness = textwrap.dedent(
            r"""
            #include <stdbool.h>
            #include <stddef.h>
            #include <stdint.h>
            #include <stdio.h>
            #include <stdlib.h>
            #include <string.h>
            #include "drivers/audio_transport/audio_transport.h"
            #include "services/deepfir/deepfir_inference_service.h"
            #include "services/audio_pipeline/audio_pipeline.h"

            static uint32_t frame;
            static uint32_t logs;
            static unsigned stops, resets, submissions;
            static bool fail_model, fail_init, fail_inference_init, fail_start;
            static bool audio_running;
            static char all_logs[16384];
            static uint32_t blocking_flushes;
            static char last_log[1024];
            static const i2c_recovery_trace_t trace = {
                .attempted = true, .reset_flags = 0x20U, .event_count = 2U,
                .pulses = 9U, .first_scl_low_event = I2C_RECOVERY_TRACE_NO_EVENT,
                .events = {{.phase = I2C_RECOVERY_RELEASE, .scl = 1U},
                           {.phase = I2C_RECOVERY_STOP_SDA_RELEASE, .pulse = 9U,
                            .cycle = 10U, .scl = 1U}}, // Mock timebase uses one cycle/us.
            };

            bool audio_transport_init(audio_transport_init_report_t *report,
                                      audio_transport_progress_callback_t callback,
                                      void *context)
            {
                if (callback) {
                    callback(AUDIO_TRANSPORT_BOARD_STAGE_ENTER, context);
                    callback(AUDIO_TRANSPORT_BOARD_STAGE_HXT_WAIT, context);
                }
                if (fail_init) {
                    report->board_status = 5;
                    report->i2c_trace = &trace;
                    return false;
                }
                report->clocks_valid = true;
                return true;
            }
            audio_transport_status_t audio_transport_start(void)
            {
                if (fail_start) { return AUDIO_TRANSPORT_STATUS_ERROR; }
                audio_running = true;
                return AUDIO_TRANSPORT_STATUS_OK;
            }
            void audio_transport_wait_for_work(void) {}
            void audio_transport_stop_safe(void) { audio_running = false; ++stops; }
            const volatile audio_startup_trace_t *audio_transport_get_startup_trace(void) { return NULL; }
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
            bool deepfir_inference_init(void) { return !fail_inference_init; }
            void deepfir_inference_reset(void) { ++resets; }
            void deepfir_inference_report_trace(void) {
                if (audio_running) { fprintf(stderr, "trace before DMA stop"); exit(98); }
            }
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
                if ((strstr(message, "[AUDIO_FAULT]") || strstr(message, "[I2C_")) && audio_running) {
                    fprintf(stderr, "must stop DMA before formatting fault output"); exit(98);
                }
                memcpy(last_log, message, length);
                last_log[length] = '\0';
                if (strlen(all_logs) + length < sizeof(all_logs)) strcat(all_logs, last_log);
                ++logs;
                return true;
            }
            void debug_uart_tx_flush_blocking(void) {
                if (audio_running) { fprintf(stderr, "blocking drain after audio start"); exit(99); }
                ++blocking_flushes;
            }
            uint32_t debug_uart_tx_drop_count(void) { return 0U; }

            int main(int argc, char **argv)
            {
                if (argc > 1) {
                    fail_init = strcmp(argv[1], "transport") == 0;
                    fail_inference_init = strcmp(argv[1], "inference") == 0;
                    fail_start = strcmp(argv[1], "transport_start") == 0;
                    bool initialized = audio_pipeline_init();
                    bool started = initialized && audio_pipeline_start();
                    char expected[100];
                    snprintf(expected, sizeof(expected), "[INIT] stage=%s status=FAIL", argv[1]);
                    if (started || !strstr(all_logs, expected) || !blocking_flushes) {
                        fprintf(stderr, "%s", all_logs); return 20;
                    }
                    if (fail_init && (!strstr(all_logs, "board_status=5") ||
                        !strstr(all_logs, "[I2C_RECOVERY] result=SDA_LOW") ||
                        !strstr(all_logs, "rststs=00000020") ||
                        !strstr(all_logs, "[I2C_EDGE] index=1 phase=stop.sda_release pulse=9") ||
                        !strstr(all_logs, "elapsed_us=10 sda=0 scl=1"))) {
                        fprintf(stderr, "%s", all_logs); return 21;
                    }
                    return 0;
                }
                if (!audio_pipeline_init()) return 1;
                if (!strstr(all_logs, "[INIT] stage=pipeline status=OK")) return 10;
                if (!strstr(all_logs, "[INIT] stage=board.hxt_wait")) return 11;
                if (!audio_pipeline_start()) return 1;
                const unsigned boot_flushes = blocking_flushes;
                if (!boot_flushes) return 12;
                // Capturing/processing a hop must not add formatted first-hop markers.
                const unsigned boot_logs = logs;
                audio_pipeline_poll();
                if (logs != boot_logs) return 13;
                const unsigned initial_logs = logs;
                for (uint32_t index = 0U; index < 999U; ++index) {
                    audio_pipeline_poll();
                }
                if (blocking_flushes != boot_flushes || logs != initial_logs + 1U) return 2;
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
                if (!strstr(all_logs, "[AUDIO_FAULT] stage=inference")) return 14;
                if (blocking_flushes != boot_flushes) return 15;
                frame = 0;
                audio_pipeline_poll();
                const char *first = strstr(all_logs, "[AUDIO_FAULT] stage=inference");
                if (strstr(first + 1, "[AUDIO_FAULT] stage=inference")) return 16;
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
            for stage in ("transport", "inference", "transport_start"):
                with self.subTest(failure_stage=stage):
                    run = subprocess.run([str(executable), stage], capture_output=True, text=True)
                    self.assertEqual(run.returncode, 0, run.stderr)


if __name__ == "__main__":
    unittest.main()
