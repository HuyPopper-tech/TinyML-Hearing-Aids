/**
 * @file audio_pipeline.c
 * @brief Foreground Speech Boosting Chunk64 audio pipeline and UART telemetry.
 * @author Bui Minh Huy
 * @date 2026-08-27
 */

#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "drivers/audio_transport/audio_transport.h"
#include "drivers/debug_uart_tx/debug_uart_tx.h"
#include "drivers/ethosu_u55/ethosu_u55.h"
#include "speech_boosting_model_contract.h"
#include "speech_boosting_u55_vela_model_data.h"
#include "services/audio_pipeline/audio_pipeline.h"
#include "services/speech_boosting/speech_boosting_audio_enhancer.h"
#include "services/speech_boosting/speech_boosting_inference.h"

#ifndef M55M1_SPEECH_BOOSTING_DIAGNOSTICS
#define M55M1_SPEECH_BOOSTING_DIAGNOSTICS 0
#endif

#ifndef M55M1_ENABLE_DAC_I2S_OUTPUT
#define M55M1_ENABLE_DAC_I2S_OUTPUT 0
#endif

enum {
    SPEECH_BOOSTING_LOG_INTERVAL_CHUNKS =
        SPEECH_BOOSTING_SAMPLE_RATE / SPEECH_BOOSTING_CHUNK_SAMPLES,
};

typedef struct {
    uint32_t minimum;
    uint32_t maximum;
    uint64_t total;
    uint32_t sample_count;
} speech_boosting_latency_window_t;

typedef struct {
    uint32_t peak;
    uint64_t squared_total;
    uint32_t sample_count;
    uint32_t clipping_count;
} speech_boosting_microphone_window_t;

typedef struct {
    int16_t capture[SPEECH_BOOSTING_CHUNK_SAMPLES];
    int16_t rolling_capture[SPEECH_BOOSTING_CAPTURE_SAMPLES];
    int16_t playback[SPEECH_BOOSTING_CHUNK_SAMPLES];
    speech_boosting_audio_stream_t stream;
} speech_boosting_audio_working_set_t;

#define SPEECH_BOOSTING_DTCM_BSS __attribute__((section(".bss.DTCM.ZeroInit")))

static speech_boosting_audio_working_set_t g_audio_working_set
    SPEECH_BOOSTING_DTCM_BSS;
static bool g_initialized;
static bool g_running;
static bool g_have_previous_chunk;
static uint32_t g_previous_sequence;
static uint32_t g_previous_capture_completed_cycle;
static uint32_t g_window_chunk_count;
static uint32_t g_capture_gap_count;
static uint32_t g_dropped_submission_count;
static speech_boosting_latency_window_t g_capture_to_foreground_latency;
static speech_boosting_latency_window_t g_lookahead_assembly_latency;
static speech_boosting_latency_window_t g_input_state_copy_latency;
static speech_boosting_latency_window_t g_neural_network_latency;
static speech_boosting_latency_window_t g_output_state_copy_latency;
static speech_boosting_latency_window_t g_output_dequantization_latency;
static speech_boosting_latency_window_t g_capture_to_model_output_ready_latency;
static speech_boosting_latency_window_t g_capture_to_playback_completion_latency;
static speech_boosting_microphone_window_t g_microphone_window;
static ethosu_u55_diagnostics_t g_previous_ethosu_diagnostics;
static bool g_have_previous_ethosu_diagnostics;

static void speech_boosting_log(const char *format, ...) {
#if M55M1_SPEECH_BOOSTING_DIAGNOSTICS
    char message[512];
    va_list arguments;
    int length;

    va_start(arguments, format);
    length = vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);
    if (length > 0) {
        const size_t bytes = (length < (int)sizeof(message))
                                 ? (size_t)length
                                 : sizeof(message) - 1U;
        (void)debug_uart_tx_try_enqueue(message, bytes);
    }
#else
    (void)format;
#endif
}

static void latency_window_record(
    speech_boosting_latency_window_t *window,
    uint32_t microseconds) {
    if (window->sample_count == 0U) {
        window->minimum = microseconds;
        window->maximum = microseconds;
    } else {
        if (microseconds < window->minimum) {
            window->minimum = microseconds;
        }
        if (microseconds > window->maximum) {
            window->maximum = microseconds;
        }
    }
    window->total += microseconds;
    ++window->sample_count;
}

static void latency_window_log_and_reset(
    const char *stage_name,
    speech_boosting_latency_window_t *window) {
    const uint32_t mean = (window->sample_count == 0U)
                              ? 0U
                              : (uint32_t)(window->total / window->sample_count);

    speech_boosting_log(
        "latency_window stage=%s sample_count=%lu minimum_microseconds=%lu "
        "mean_microseconds=%lu maximum_microseconds=%lu\r\n",
        stage_name,
        (unsigned long)window->sample_count,
        (unsigned long)window->minimum,
        (unsigned long)mean,
        (unsigned long)window->maximum);
    *window = (speech_boosting_latency_window_t){0};
}

static void microphone_window_record(
    const int16_t samples[SPEECH_BOOSTING_CHUNK_SAMPLES]) {
    for (uint32_t index = 0U; index < SPEECH_BOOSTING_CHUNK_SAMPLES; ++index) {
        const int32_t sample = samples[index];
        const uint32_t magnitude = (sample < 0) ? (uint32_t)(-sample) : (uint32_t)sample;

        if (magnitude > g_microphone_window.peak) {
            g_microphone_window.peak = magnitude;
        }
        g_microphone_window.squared_total +=
            (uint64_t)((int64_t)sample * (int64_t)sample);
        ++g_microphone_window.sample_count;
        if ((sample == INT16_MIN) || (sample == INT16_MAX)) {
            ++g_microphone_window.clipping_count;
        }
    }
}

static void microphone_window_log_and_reset(void) {
    const uint32_t root_mean_square = (g_microphone_window.sample_count == 0U)
                                          ? 0U
                                          : (uint32_t)sqrt(
                                                (double)g_microphone_window.squared_total /
                                                g_microphone_window.sample_count);

    speech_boosting_log(
        "microphone_signal sample_count=%lu microphone_peak_pulse_code_modulation_16=%lu "
        "microphone_root_mean_square_pulse_code_modulation_16=%lu microphone_clipping_count=%lu\r\n",
        (unsigned long)g_microphone_window.sample_count,
        (unsigned long)g_microphone_window.peak,
        (unsigned long)root_mean_square,
        (unsigned long)g_microphone_window.clipping_count);
    g_microphone_window = (speech_boosting_microphone_window_t){0};
}

#if M55M1_ENABLE_DAC_I2S_OUTPUT
static void record_playback_completions(void) {
    audio_transport_completion_t completion;

    while (audio_transport_take_completion(&completion)) {
        latency_window_record(
            &g_capture_to_playback_completion_latency,
            audio_transport_cycles_to_us(audio_transport_cycles_elapsed(
                completion.capture_completed_cycle,
                completion.dac_completed_cycle)));
    }
}

static void submit_playback(
    const int16_t samples[SPEECH_BOOSTING_CHUNK_SAMPLES],
    uint32_t sequence,
    uint32_t capture_completed_cycle) {
    const audio_transport_block_tag_t tag = {
        .sequence = sequence,
        .capture_completed_cycle = capture_completed_cycle,
    };

    if (audio_transport_submit_tagged(samples, &tag) != AUDIO_TRANSPORT_STATUS_OK) {
        ++g_dropped_submission_count;
    }
}
#endif

static void log_window(void) {
    audio_transport_health_t health;
    speech_boosting_audio_diagnostics_t audio_diagnostics;
    ethosu_u55_diagnostics_t ethosu_diagnostics;
    uint32_t neural_processing_unit_active_cycles;
    uint32_t neural_processing_unit_idle_cycles;
    uint32_t multiply_accumulate_active_cycles;

    audio_transport_get_health(&health);
    speech_boosting_audio_enhancer_get_diagnostics(&audio_diagnostics);
    ethosu_u55_get_diagnostics(&ethosu_diagnostics);
    if (g_have_previous_ethosu_diagnostics) {
        neural_processing_unit_active_cycles =
            ethosu_diagnostics.npu_active_cycles -
            g_previous_ethosu_diagnostics.npu_active_cycles;
        neural_processing_unit_idle_cycles =
            ethosu_diagnostics.npu_idle_cycles -
            g_previous_ethosu_diagnostics.npu_idle_cycles;
        multiply_accumulate_active_cycles =
            ethosu_diagnostics.mac_active_cycles -
            g_previous_ethosu_diagnostics.mac_active_cycles;
    } else {
        neural_processing_unit_active_cycles = ethosu_diagnostics.npu_active_cycles;
        neural_processing_unit_idle_cycles = ethosu_diagnostics.npu_idle_cycles;
        multiply_accumulate_active_cycles = ethosu_diagnostics.mac_active_cycles;
        g_have_previous_ethosu_diagnostics = true;
    }
    g_previous_ethosu_diagnostics = ethosu_diagnostics;
    latency_window_log_and_reset(
        "microphone_direct_memory_access_capture_to_foreground",
        &g_capture_to_foreground_latency);
    latency_window_log_and_reset(
        "lookahead_assembly_and_pcm_quantization",
        &g_lookahead_assembly_latency);
    latency_window_log_and_reset(
        "input_state_tensor_copy",
        &g_input_state_copy_latency);
    latency_window_log_and_reset(
        "ethos_u55_neural_network_invocation",
        &g_neural_network_latency);
    latency_window_log_and_reset(
        "output_state_tensor_copy",
        &g_output_state_copy_latency);
    latency_window_log_and_reset(
        "output_dequantization",
        &g_output_dequantization_latency);
    latency_window_log_and_reset(
        "capture_to_model_output_ready",
        &g_capture_to_model_output_ready_latency);
#if M55M1_ENABLE_DAC_I2S_OUTPUT
    latency_window_log_and_reset(
        "capture_to_playback_direct_memory_access_completion_end_to_end",
        &g_capture_to_playback_completion_latency);
#endif
    microphone_window_log_and_reset();
    speech_boosting_log(
        "transport_counters capture_gaps=%lu microphone_direct_memory_access_overruns=%lu "
        "microphone_direct_memory_access_errors=%lu "
#if M55M1_ENABLE_DAC_I2S_OUTPUT
        "playback_underruns=%lu playback_direct_memory_access_errors=%lu "
#else
        "audio_output_disabled=1 "
#endif
        "invocation_errors=%lu output_clipping=%lu dropped_submissions=%lu\r\n",
        (unsigned long)g_capture_gap_count,
        (unsigned long)health.dmic_overrun_blocks,
        (unsigned long)health.dmic_dma_errors,
#if M55M1_ENABLE_DAC_I2S_OUTPUT
        (unsigned long)health.dac_underrun_blocks,
        (unsigned long)health.dac_dma_errors,
#endif
        (unsigned long)audio_diagnostics.invoke_error_count,
        (unsigned long)audio_diagnostics.output_saturation_count,
        (unsigned long)g_dropped_submission_count);
    speech_boosting_log(
        "ethos_u55_performance_counters "
        "neural_processing_unit_active_cycles=%lu "
        "neural_processing_unit_idle_cycles=%lu "
        "multiply_accumulate_active_cycles=%lu "
        "neural_network_invocation_count=%lu "
        "neural_processing_unit_interrupt_count=%lu\r\n",
        (unsigned long)neural_processing_unit_active_cycles,
        (unsigned long)neural_processing_unit_idle_cycles,
        (unsigned long)multiply_accumulate_active_cycles,
        (unsigned long)ethosu_diagnostics.inference_end_count,
        (unsigned long)ethosu_diagnostics.npu_irq_count);
}

bool audio_pipeline_init(void) {
    audio_transport_init_report_t transport_report;
    speech_boosting_inference_diagnostics_t inference_diagnostics;

    if (g_initialized) {
        return true;
    }
    if (!audio_transport_init(&transport_report, NULL, NULL) ||
        !transport_report.clocks_valid ||
        !speech_boosting_audio_enhancer_init()) {
        return false;
    }
    speech_boosting_audio_enhancer_reset(&g_audio_working_set.stream);
    speech_boosting_inference_get_diagnostics(&inference_diagnostics);
#if M55M1_SPEECH_BOOSTING_DIAGNOSTICS
    debug_uart_tx_init();
    speech_boosting_log(
        "speech_boosting_boot model_name=speech_boosting_chunk64 "
        "vela_memory_profile=%s model_memory_region=%s model_sha_256=%s model_bytes=%lu\r\n",
        SPEECH_BOOSTING_MEMORY_PROFILE_NAME,
        SPEECH_BOOSTING_MODEL_MEMORY_REGION_NAME,
        SPEECH_BOOSTING_VELA_SHA256,
        (unsigned long)g_speech_boosting_u55_vela_model_len);
    speech_boosting_log(
        "speech_boosting_memory tensorflow_lite_micro_tensor_arena_capacity_bytes=%lu "
        "tensorflow_lite_micro_tensor_arena_used_bytes=%lu persistent_state_bytes=%u "
        "capture_static_bytes=%lu model_output_static_bytes=%lu "
        "rolling_capture_static_bytes=%lu\r\n",
        (unsigned long)inference_diagnostics.tensor_arena_capacity_bytes,
        (unsigned long)inference_diagnostics.tensor_arena_used_bytes,
        SPEECH_BOOSTING_STATE_BYTES,
        (unsigned long)sizeof(g_audio_working_set.capture),
        (unsigned long)sizeof(g_audio_working_set.playback),
        (unsigned long)sizeof(g_audio_working_set.rolling_capture));
    speech_boosting_log(
        "speech_boosting_memory_ranges model_memory_region=%s "
        "model_start_address=0x%08lx model_end_address=0x%08lx "
        "tensor_arena_memory_region=%s tensor_arena_start_address=0x%08lx "
        "tensor_arena_end_address=0x%08lx\r\n",
        SPEECH_BOOSTING_MODEL_MEMORY_REGION_NAME,
        (unsigned long)inference_diagnostics.model_start_address,
        (unsigned long)inference_diagnostics.model_end_address,
        SPEECH_BOOSTING_MODEL_MEMORY_REGION_NAME,
        (unsigned long)inference_diagnostics.tensor_arena_start_address,
        (unsigned long)inference_diagnostics.tensor_arena_end_address);
    speech_boosting_log(
        "speech_boosting_foreground_audio_working_set memory_region=dtcm "
        "start_address=0x%08lx end_address=0x%08lx static_bytes=%lu\r\n",
        (unsigned long)(uintptr_t)&g_audio_working_set,
        (unsigned long)((uintptr_t)&g_audio_working_set +
                        sizeof(g_audio_working_set)),
        (unsigned long)sizeof(g_audio_working_set));
    speech_boosting_log(
        "speech_boosting_accelerator microphone_digital_signal_processing_gain_decibels=%d "
        "ethos_u55_operator_count=%lu central_processing_unit_operator_count=%lu\r\n",
        AUDIO_TRANSPORT_MICROPHONE_DSP_GAIN_DB,
        (unsigned long)inference_diagnostics.ethosu_operator_count,
        (unsigned long)inference_diagnostics.cpu_operator_count);
    speech_boosting_log(
        "speech_boosting_audio_output dac_i2s_output_enabled=%u\r\n",
        M55M1_ENABLE_DAC_I2S_OUTPUT);
#endif
    g_initialized = true;
    return true;
}

bool audio_pipeline_start(void) {
    if (!g_initialized || g_running ||
        (audio_transport_start() != AUDIO_TRANSPORT_STATUS_OK)) {
        return false;
    }
    g_running = true;
    return true;
}

void audio_pipeline_poll(void) {
    audio_transport_capture_timing_t timing;
    speech_boosting_audio_diagnostics_t audio_diagnostics;
    speech_boosting_inference_diagnostics_t inference_diagnostics;
    uint32_t capture_to_foreground_microseconds;

    if (!g_running) {
        return;
    }
#if M55M1_ENABLE_DAC_I2S_OUTPUT
    record_playback_completions();
#endif
    if (!audio_transport_take_capture(g_audio_working_set.capture, &timing)) {
        return;
    }

    capture_to_foreground_microseconds = audio_transport_cycles_to_us(
        audio_transport_cycles_elapsed(timing.completed_cycle, audio_transport_cycles_now()));
    latency_window_record(
        &g_capture_to_foreground_latency,
        capture_to_foreground_microseconds);
    microphone_window_record(g_audio_working_set.capture);

    if (g_have_previous_chunk && (timing.sequence != g_previous_sequence + 1U)) {
        ++g_capture_gap_count;
        speech_boosting_audio_enhancer_reset(&g_audio_working_set.stream);
        g_have_previous_chunk = false;
    }

    if (g_have_previous_chunk) {
        (void)memcpy(
            g_audio_working_set.rolling_capture + SPEECH_BOOSTING_CHUNK_SAMPLES,
            g_audio_working_set.capture,
            SPEECH_BOOSTING_LOOKAHEAD_SAMPLES * sizeof(g_audio_working_set.capture[0]));
        if (!speech_boosting_audio_enhancer_process(
                &g_audio_working_set.stream,
                g_audio_working_set.rolling_capture,
                g_audio_working_set.playback)) {
            (void)memset(
                g_audio_working_set.playback, 0, sizeof(g_audio_working_set.playback));
        }
        speech_boosting_audio_enhancer_get_diagnostics(&audio_diagnostics);
        speech_boosting_inference_get_diagnostics(&inference_diagnostics);
        latency_window_record(
            &g_lookahead_assembly_latency,
            audio_diagnostics.lookahead_assembly_and_pcm_quantization_microseconds);
        latency_window_record(
            &g_input_state_copy_latency,
            inference_diagnostics.input_state_tensor_copy_microseconds);
        latency_window_record(
            &g_neural_network_latency,
            inference_diagnostics.neural_network_invocation_microseconds);
        latency_window_record(
            &g_output_state_copy_latency,
            inference_diagnostics.output_state_tensor_copy_microseconds);
        latency_window_record(
            &g_output_dequantization_latency,
            audio_diagnostics.output_dequantization_microseconds);
        latency_window_record(
            &g_capture_to_model_output_ready_latency,
            audio_transport_cycles_to_us(audio_transport_cycles_elapsed(
                g_previous_capture_completed_cycle,
                audio_transport_cycles_now())));
#if M55M1_ENABLE_DAC_I2S_OUTPUT
        submit_playback(
            g_audio_working_set.playback,
            g_previous_sequence,
            g_previous_capture_completed_cycle);
#endif
    } else {
#if M55M1_ENABLE_DAC_I2S_OUTPUT
        (void)memset(
            g_audio_working_set.playback, 0, sizeof(g_audio_working_set.playback));
        submit_playback(
            g_audio_working_set.playback, timing.sequence, timing.completed_cycle);
#endif
    }

    (void)memcpy(
        g_audio_working_set.rolling_capture,
        g_audio_working_set.capture,
        sizeof(g_audio_working_set.capture));
    g_previous_sequence = timing.sequence;
    g_previous_capture_completed_cycle = timing.completed_cycle;
    g_have_previous_chunk = true;
    ++g_window_chunk_count;
    if (g_window_chunk_count == SPEECH_BOOSTING_LOG_INTERVAL_CHUNKS) {
#if M55M1_ENABLE_DAC_I2S_OUTPUT
        record_playback_completions();
#endif
        log_window();
        g_window_chunk_count = 0U;
    }
}

void audio_pipeline_stop_safe(void) {
    audio_transport_stop_safe();
    speech_boosting_audio_enhancer_reset(&g_audio_working_set.stream);
    g_initialized = false;
    g_running = false;
    g_have_previous_chunk = false;
    g_have_previous_ethosu_diagnostics = false;
    g_window_chunk_count = 0U;
}
