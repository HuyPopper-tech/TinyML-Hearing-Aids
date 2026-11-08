/**
 * @file audio_pipeline.c
 * @brief Foreground DMIC to DeepFIR inference and audio transport pipeline.
 * @author Bui Minh Huy
 * @date 2026-09-13
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "services/deepfir/deepfir_inference_service.h"
#include "drivers/audio_transport/audio_transport.h"
#include "services/audio_pipeline/audio_pipeline.h"

#ifndef M55M1_ENABLE_DAC_I2S_OUTPUT
#define M55M1_ENABLE_DAC_I2S_OUTPUT 0
#endif
#ifndef M55M1_RUNTIME_TELEMETRY
#define M55M1_RUNTIME_TELEMETRY 0
#endif
#ifndef M55M1_AUDIO_RELEASE_PROBE
#define M55M1_AUDIO_RELEASE_PROBE 0
#endif
#ifndef M55M1_AUDIO_UART_PCM_CAPTURE
#define M55M1_AUDIO_UART_PCM_CAPTURE 0
#endif
#if M55M1_RUNTIME_TELEMETRY || M55M1_AUDIO_RELEASE_PROBE || M55M1_AUDIO_UART_PCM_CAPTURE
#include "drivers/debug_uart_tx/debug_uart_tx.h"
#endif
#if M55M1_AUDIO_UART_PCM_CAPTURE
#ifndef M55M1_DMIC_GAIN_DB
#define M55M1_DMIC_GAIN_DB 30
#endif
#endif

typedef struct {
    int16_t capture[AUDIO_TRANSPORT_SAMPLES_PER_BLOCK];
    int16_t playback[AUDIO_TRANSPORT_SAMPLES_PER_BLOCK];
} audio_working_set_t;

// Foreground owns both buffers; capture copies from DMA and submit copies into TX storage.
static audio_working_set_t g_audio_working_set;
static bool g_initialized;
static bool g_running;
static uint32_t g_expected_sequence;
static audio_pipeline_stats_t g_stats;

void audio_pipeline_get_stats(audio_pipeline_stats_t *stats) {
    if (stats != NULL) {
        *stats = g_stats;
    }
}

static bool transport_faulted(void) {
    audio_transport_health_t health = {0};
    audio_transport_get_health(&health);
    return health.dmic_overrun_blocks || health.dmic_dma_errors || health.dac_underrun_blocks ||
           health.dac_dma_errors;
}

/* Foreground only: stop DMA before resetting any shared model/audio history. */
static void restart_stream(void) {
    audio_pipeline_stop_safe();
    memset(&g_audio_working_set, 0, sizeof(g_audio_working_set));
    if (audio_pipeline_init() && audio_pipeline_start()) {
        ++g_stats.restarts;
    } else {
        ++g_stats.restart_failures;
    }
}

#if M55M1_AUDIO_UART_PCM_CAPTURE
#define AUDIO_PIPELINE_CAPTURE_SAMPLE_COUNT AUDIO_TRANSPORT_SAMPLE_RATE_HZ
#define AUDIO_PIPELINE_CAPTURE_HEADER_BYTES 48U
#define AUDIO_PIPELINE_CAPTURE_CHUNK_BYTES 512U

/* One second per stream; this diagnostic buffer lives in cacheable SRAM. */
static int16_t g_capture_input[AUDIO_PIPELINE_CAPTURE_SAMPLE_COUNT];
static int16_t g_capture_output[AUDIO_PIPELINE_CAPTURE_SAMPLE_COUNT];
static uint32_t g_capture_sample_count;
static uint32_t g_capture_input_clips;
static uint32_t g_capture_output_clips;
static uint32_t g_capture_sequence_gaps;
static uint32_t g_capture_last_sequence;
static bool g_capture_has_sequence;
static bool g_capture_armed;
static bool g_capture_sent;

static void capture_store_u16(uint8_t *destination, uint16_t value) {
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8U);
}

static void capture_store_u32(uint8_t *destination, uint32_t value) {
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8U);
    destination[2] = (uint8_t)(value >> 16U);
    destination[3] = (uint8_t)(value >> 24U);
}

static uint32_t capture_crc32_byte(uint32_t crc, uint8_t value) {
    crc ^= value;
    for (uint32_t bit = 0U; bit < 8U; ++bit) {
        crc = (crc >> 1U) ^ ((crc & 1U) ? 0xEDB88320U : 0U);
    }
    return crc;
}

static uint32_t capture_payload_crc32(uint32_t sample_count) {
    uint32_t crc = 0xFFFFFFFFU;
    for (uint32_t index = 0U; index < sample_count; ++index) {
        const uint16_t input = (uint16_t)g_capture_input[index];
        const uint16_t output = (uint16_t)g_capture_output[index];
        crc = capture_crc32_byte(crc, (uint8_t)input);
        crc = capture_crc32_byte(crc, (uint8_t)(input >> 8U));
        crc = capture_crc32_byte(crc, (uint8_t)output);
        crc = capture_crc32_byte(crc, (uint8_t)(output >> 8U));
    }
    return ~crc;
}

static bool capture_send_bytes(const uint8_t *bytes, uint32_t length) {
    if (!debug_uart_tx_try_enqueue((const char *)bytes, length)) {
        debug_uart_tx_flush_blocking();
        if (!debug_uart_tx_try_enqueue((const char *)bytes, length)) {
            return false;
        }
    }
    debug_uart_tx_flush_blocking();
    return true;
}

static void capture_send_packet(uint32_t status) {
    uint8_t header[AUDIO_PIPELINE_CAPTURE_HEADER_BYTES] = {0U};
    uint8_t chunk[AUDIO_PIPELINE_CAPTURE_CHUNK_BYTES];
    const uint32_t sample_count = (status == 0U) ? g_capture_sample_count : 0U;
    uint32_t chunk_length = 0U;

    if (g_capture_sent) {
        return;
    }
    g_capture_sent = true;
    g_capture_armed = false;
    audio_pipeline_stop_safe();

    memcpy(header, "DFIRPCM1", 8U);
    capture_store_u16(&header[8], 1U);
    capture_store_u16(&header[10], AUDIO_PIPELINE_CAPTURE_HEADER_BYTES);
    capture_store_u32(&header[12], status);
    capture_store_u32(&header[16], AUDIO_TRANSPORT_SAMPLE_RATE_HZ);
    capture_store_u32(&header[20], sample_count);
    capture_store_u16(&header[24], AUDIO_TRANSPORT_SAMPLES_PER_BLOCK);
    capture_store_u16(&header[26], 0U);
    capture_store_u16(&header[28], (uint16_t)(int16_t)M55M1_DMIC_GAIN_DB);
    capture_store_u16(&header[30], 0U);
    capture_store_u32(&header[32], g_capture_input_clips);
    capture_store_u32(&header[36], g_capture_output_clips);
    capture_store_u32(&header[40], g_capture_sequence_gaps);
    capture_store_u32(&header[44], (status == 0U) ? capture_payload_crc32(sample_count) : 0U);
    if (!capture_send_bytes(header, sizeof(header)) || (status != 0U)) {
        return;
    }

    for (uint32_t index = 0U; index < sample_count; ++index) {
        const uint16_t input = (uint16_t)g_capture_input[index];
        const uint16_t output = (uint16_t)g_capture_output[index];
        chunk[chunk_length++] = (uint8_t)input;
        chunk[chunk_length++] = (uint8_t)(input >> 8U);
        chunk[chunk_length++] = (uint8_t)output;
        chunk[chunk_length++] = (uint8_t)(output >> 8U);
        if (chunk_length == sizeof(chunk)) {
            if (!capture_send_bytes(chunk, chunk_length)) {
                return;
            }
            chunk_length = 0U;
        }
    }
    if (chunk_length > 0U) {
        (void)capture_send_bytes(chunk, chunk_length);
    }
}

static void capture_store_hop(const int16_t *input, const int16_t *output, uint32_t sequence) {
    if (g_capture_has_sequence && (sequence != g_capture_last_sequence + 1U)) {
        ++g_capture_sequence_gaps;
    }
    g_capture_last_sequence = sequence;
    g_capture_has_sequence = true;

    for (uint32_t index = 0U; index < AUDIO_TRANSPORT_SAMPLES_PER_BLOCK; ++index) {
        const uint32_t destination = g_capture_sample_count + index;
        g_capture_input[destination] = input[index];
        g_capture_output[destination] = output[index];
        if ((input[index] == INT16_MIN) || (input[index] == INT16_MAX)) {
            ++g_capture_input_clips;
        }
        if ((output[index] == INT16_MIN) || (output[index] == INT16_MAX)) {
            ++g_capture_output_clips;
        }
    }
    g_capture_sample_count += AUDIO_TRANSPORT_SAMPLES_PER_BLOCK;
    if (g_capture_sample_count == AUDIO_PIPELINE_CAPTURE_SAMPLE_COUNT) {
        capture_send_packet(0U);
    }
}
#endif

#if M55M1_RUNTIME_TELEMETRY || M55M1_AUDIO_RELEASE_PROBE
#if M55M1_RUNTIME_TELEMETRY
#define AUDIO_PIPELINE_REPORT_HOPS 1000U
#endif
#if M55M1_AUDIO_RELEASE_PROBE
#define AUDIO_PIPELINE_PROBE_HOPS 2000U
#endif
#define AUDIO_PIPELINE_HOP_BUDGET_US                                                               \
    ((AUDIO_TRANSPORT_SAMPLES_PER_BLOCK * 1000000UL) / AUDIO_TRANSPORT_SAMPLE_RATE_HZ)

typedef struct {
    uint32_t input_peak;
    uint32_t output_peak;
    uint32_t input_clip_samples;
    uint32_t output_clip_samples;
    uint32_t model_failures;
    uint32_t submit_busy;
    uint32_t submit_errors;
    uint32_t max_feature_us;
    uint32_t max_pre_fft_us;
    uint32_t max_fft_us;
    uint32_t max_post_fft_us;
    uint32_t max_npu_us;
    uint32_t max_invoke_us;
    uint32_t max_overhead_us;
    uint32_t max_dsp_us;
    uint32_t max_total_us;
    uint32_t max_e2e_us;
    uint32_t late_hops;
    uint32_t max_bridge_us;
    uint32_t max_carry_us;
    uint32_t max_taps_us;
    uint32_t max_fir_us;
    uint32_t max_other_us;
#if M55M1_AUDIO_RELEASE_PROBE
    uint64_t total_us_sum;
    uint64_t e2e_us_sum;
    uint64_t bridge_us_sum;
    uint64_t carry_us_sum;
    uint64_t taps_us_sum;
    uint64_t fir_us_sum;
    uint64_t other_us_sum;
    uint32_t capture_last_sequence;
#endif
} audio_pipeline_debug_stats_t;

static uint32_t g_frame_count;
static audio_pipeline_debug_stats_t g_debug_stats;

static void record_pcm_stats(const int16_t *samples, uint32_t *peak, uint32_t *clip_samples) {
    for (uint32_t index = 0U; index < AUDIO_TRANSPORT_SAMPLES_PER_BLOCK; ++index) {
        const int32_t sample = samples[index];
        const uint32_t magnitude = (uint32_t)(sample < 0 ? -sample : sample);
        if (magnitude > *peak) {
            *peak = magnitude;
        }
        if (sample == -32768 || sample == 32767) {
            ++*clip_samples;
        }
    }
}

static void record_timing(const deepfir_inference_timing_t *timing) {
    if (timing->feature_us > g_debug_stats.max_feature_us) {
        g_debug_stats.max_feature_us = timing->feature_us;
    }
    if (timing->pre_fft_us > g_debug_stats.max_pre_fft_us) {
        g_debug_stats.max_pre_fft_us = timing->pre_fft_us;
    }
    if (timing->fft_us > g_debug_stats.max_fft_us) {
        g_debug_stats.max_fft_us = timing->fft_us;
    }
    if (timing->post_fft_us > g_debug_stats.max_post_fft_us) {
        g_debug_stats.max_post_fft_us = timing->post_fft_us;
    }
    if (timing->npu_us > g_debug_stats.max_npu_us) {
        g_debug_stats.max_npu_us = timing->npu_us;
    }
    if (timing->invoke_us > g_debug_stats.max_invoke_us) {
        g_debug_stats.max_invoke_us = timing->invoke_us;
    }
    if (timing->invoke_overhead_us > g_debug_stats.max_overhead_us) {
        g_debug_stats.max_overhead_us = timing->invoke_overhead_us;
    }
    if (timing->dsp_us > g_debug_stats.max_dsp_us) {
        g_debug_stats.max_dsp_us = timing->dsp_us;
    }
    if (timing->total_us > g_debug_stats.max_total_us) {
        g_debug_stats.max_total_us = timing->total_us;
    }
    if (timing->bridge_us > g_debug_stats.max_bridge_us) {
        g_debug_stats.max_bridge_us = timing->bridge_us;
    }
    if (timing->carry_us > g_debug_stats.max_carry_us) {
        g_debug_stats.max_carry_us = timing->carry_us;
    }
    if (timing->taps_us > g_debug_stats.max_taps_us) {
        g_debug_stats.max_taps_us = timing->taps_us;
    }
    if (timing->fir_us > g_debug_stats.max_fir_us) {
        g_debug_stats.max_fir_us = timing->fir_us;
    }
    if (timing->other_us > g_debug_stats.max_other_us) {
        g_debug_stats.max_other_us = timing->other_us;
    }
#if M55M1_AUDIO_RELEASE_PROBE
    g_debug_stats.total_us_sum += timing->total_us;
    g_debug_stats.bridge_us_sum += timing->bridge_us;
    g_debug_stats.carry_us_sum += timing->carry_us;
    g_debug_stats.taps_us_sum += timing->taps_us;
    g_debug_stats.fir_us_sum += timing->fir_us;
    g_debug_stats.other_us_sum += timing->other_us;
#endif
}

static void record_progress(uint32_t capture_completed_cycle, bool model_succeeded) {
    uint32_t e2e_us = audio_transport_cycles_to_us(
        audio_transport_cycles_elapsed(capture_completed_cycle, audio_transport_cycles_now()));

    if (model_succeeded) {
        deepfir_inference_timing_t timing;
        deepfir_inference_get_last_timing(&timing);
        record_timing(&timing);
    }
    if (e2e_us > g_debug_stats.max_e2e_us) {
        g_debug_stats.max_e2e_us = e2e_us;
    }
    if (e2e_us >= AUDIO_PIPELINE_HOP_BUDGET_US) {
        ++g_debug_stats.late_hops;
    }
#if M55M1_AUDIO_RELEASE_PROBE
    g_debug_stats.e2e_us_sum += e2e_us;
#endif
#if M55M1_RUNTIME_TELEMETRY
    if ((g_frame_count % AUDIO_PIPELINE_REPORT_HOPS) != 0U) {
        return;
    }

    audio_transport_health_t health;
    audio_transport_get_health(&health);
    bool feature_mve_ready = false;
    bool fir_mve_ready = false;
    uint32_t mve_self_test_us = 0U;
    deepfir_inference_get_dsp_status(&feature_mve_ready, &fir_mve_ready, &mve_self_test_us);
    char message[768];
    const int length = snprintf(
        message, sizeof(message),
        "[AUDIO] frame=%lu in_peak=%lu out_peak=%lu in_clip=%lu out_clip=%lu "
        "model_fail=%lu submit_busy=%lu submit_error=%lu "
        "dmic_overrun=%lu dac_underrun=%lu dmic_dma=%lu dac_dma=%lu "
        "feature_mve=%lu fir_mve=%lu mve_test_us=%lu "
        "feature_max_us=%lu pre_fft_max_us=%lu fft_max_us=%lu "
        "post_fft_max_us=%lu npu_max_us=%lu invoke_max_us=%lu "
        "overhead_max_us=%lu bridge_max_us=%lu carry_max_us=%lu "
        "taps_max_us=%lu fir_max_us=%lu other_max_us=%lu "
        "dsp_max_us=%lu total_max_us=%lu e2e_max_us=%lu late_1ms=%lu "
        "uart_drop=%lu\r\n",
        (unsigned long)g_frame_count, (unsigned long)g_debug_stats.input_peak,
        (unsigned long)g_debug_stats.output_peak, (unsigned long)g_debug_stats.input_clip_samples,
        (unsigned long)g_debug_stats.output_clip_samples,
        (unsigned long)g_debug_stats.model_failures, (unsigned long)g_debug_stats.submit_busy,
        (unsigned long)g_debug_stats.submit_errors, (unsigned long)health.dmic_overrun_blocks,
        (unsigned long)health.dac_underrun_blocks, (unsigned long)health.dmic_dma_errors,
        (unsigned long)health.dac_dma_errors, (unsigned long)feature_mve_ready,
        (unsigned long)fir_mve_ready, (unsigned long)mve_self_test_us,
        (unsigned long)g_debug_stats.max_feature_us, (unsigned long)g_debug_stats.max_pre_fft_us,
        (unsigned long)g_debug_stats.max_fft_us, (unsigned long)g_debug_stats.max_post_fft_us,
        (unsigned long)g_debug_stats.max_npu_us, (unsigned long)g_debug_stats.max_invoke_us,
        (unsigned long)g_debug_stats.max_overhead_us, (unsigned long)g_debug_stats.max_bridge_us,
        (unsigned long)g_debug_stats.max_carry_us, (unsigned long)g_debug_stats.max_taps_us,
        (unsigned long)g_debug_stats.max_fir_us, (unsigned long)g_debug_stats.max_other_us,
        (unsigned long)g_debug_stats.max_dsp_us, (unsigned long)g_debug_stats.max_total_us,
        (unsigned long)g_debug_stats.max_e2e_us, (unsigned long)g_debug_stats.late_hops,
        (unsigned long)debug_uart_tx_drop_count());
    if (length > 0 && (size_t)length < sizeof(message)) {
        (void)debug_uart_tx_try_enqueue(message, (size_t)length);
    }
#endif
}

#if M55M1_AUDIO_RELEASE_PROBE
static void finish_release_probe_if_ready(void) {
    if (g_frame_count != AUDIO_PIPELINE_PROBE_HOPS) {
        return;
    }

    audio_transport_health_t health;
    audio_transport_get_health(&health);
    audio_pipeline_stop_safe();
    debug_uart_tx_init();
    bool feature_mve_ready = false;
    bool fir_mve_ready = false;
    uint32_t mve_self_test_us = 0U;
    deepfir_inference_get_dsp_status(&feature_mve_ready, &fir_mve_ready, &mve_self_test_us);

    char message[1024];
    const int length = snprintf(
        message, sizeof(message),
        "[AUDIO_RELEASE_PROBE] frame=%lu in_peak=%lu out_peak=%lu "
        "in_clip=%lu out_clip=%lu model_fail=%lu "
        "submit_busy=%lu submit_error=%lu "
        "dmic_overrun=%lu dac_underrun=%lu dmic_dma=%lu dac_dma=%lu "
        "feature_mve_ready=%lu fir_mve_ready=%lu mve_test_us=%lu "
        "feature_max_us=%lu pre_fft_max_us=%lu fft_max_us=%lu "
        "post_fft_max_us=%lu npu_max_us=%lu invoke_max_us=%lu "
        "overhead_max_us=%lu dsp_max_us=%lu "
        "bridge_max_us=%lu carry_max_us=%lu taps_max_us=%lu "
        "fir_max_us=%lu other_max_us=%lu total_max_us=%lu "
        "e2e_max_us=%lu late_1ms=%lu "
        "bridge_mean_us=%lu carry_mean_us=%lu taps_mean_us=%lu "
        "fir_mean_us=%lu other_mean_us=%lu "
        "total_mean_us=%lu e2e_mean_us=%lu capture_last_seq=%lu\r\n",
        (unsigned long)g_frame_count, (unsigned long)g_debug_stats.input_peak,
        (unsigned long)g_debug_stats.output_peak, (unsigned long)g_debug_stats.input_clip_samples,
        (unsigned long)g_debug_stats.output_clip_samples,
        (unsigned long)g_debug_stats.model_failures, (unsigned long)g_debug_stats.submit_busy,
        (unsigned long)g_debug_stats.submit_errors, (unsigned long)health.dmic_overrun_blocks,
        (unsigned long)health.dac_underrun_blocks, (unsigned long)health.dmic_dma_errors,
        (unsigned long)health.dac_dma_errors, (unsigned long)feature_mve_ready,
        (unsigned long)fir_mve_ready, (unsigned long)mve_self_test_us,
        (unsigned long)g_debug_stats.max_feature_us, (unsigned long)g_debug_stats.max_pre_fft_us,
        (unsigned long)g_debug_stats.max_fft_us, (unsigned long)g_debug_stats.max_post_fft_us,
        (unsigned long)g_debug_stats.max_npu_us, (unsigned long)g_debug_stats.max_invoke_us,
        (unsigned long)g_debug_stats.max_overhead_us, (unsigned long)g_debug_stats.max_dsp_us,
        (unsigned long)g_debug_stats.max_bridge_us, (unsigned long)g_debug_stats.max_carry_us,
        (unsigned long)g_debug_stats.max_taps_us, (unsigned long)g_debug_stats.max_fir_us,
        (unsigned long)g_debug_stats.max_other_us, (unsigned long)g_debug_stats.max_total_us,
        (unsigned long)g_debug_stats.max_e2e_us, (unsigned long)g_debug_stats.late_hops,
        (unsigned long)(g_debug_stats.bridge_us_sum / g_frame_count),
        (unsigned long)(g_debug_stats.carry_us_sum / g_frame_count),
        (unsigned long)(g_debug_stats.taps_us_sum / g_frame_count),
        (unsigned long)(g_debug_stats.fir_us_sum / g_frame_count),
        (unsigned long)(g_debug_stats.other_us_sum / g_frame_count),
        (unsigned long)(g_debug_stats.total_us_sum / g_frame_count),
        (unsigned long)(g_debug_stats.e2e_us_sum / g_frame_count),
        (unsigned long)g_debug_stats.capture_last_sequence);
    if (length > 0 && (size_t)length < sizeof(message)) {
        (void)debug_uart_tx_try_enqueue(message, (size_t)length);
        debug_uart_tx_flush_blocking();
    }
}
#endif
#endif

bool audio_pipeline_init(void) {
    audio_transport_init_report_t transport_report = {0};

    if (g_initialized) {
        return true;
    }
    if (!audio_transport_init(&transport_report, NULL, NULL) || !transport_report.clocks_valid) {
        return false;
    }
#if M55M1_RUNTIME_TELEMETRY
    // Enable the UART before model setup so allocation failures are visible.
    debug_uart_tx_init();
#endif
    if (!deepfir_inference_init()) {
        return false;
    }
#if M55M1_AUDIO_UART_PCM_CAPTURE
    debug_uart_tx_init();
    g_capture_sample_count = 0U;
    g_capture_input_clips = 0U;
    g_capture_output_clips = 0U;
    g_capture_sequence_gaps = 0U;
    g_capture_last_sequence = 0U;
    g_capture_has_sequence = false;
    g_capture_armed = false;
    g_capture_sent = false;
#endif

#if M55M1_RUNTIME_TELEMETRY || M55M1_AUDIO_RELEASE_PROBE
    g_frame_count = 0U;
    g_debug_stats = (audio_pipeline_debug_stats_t){0};
#endif
    g_initialized = true;
    return true;
}

bool audio_pipeline_start(void) {
    if (!g_initialized || g_running) {
        return false;
    }
    deepfir_inference_reset();
    memset(&g_audio_working_set, 0, sizeof(g_audio_working_set));
    g_expected_sequence = 1U;
    if (audio_transport_start() != AUDIO_TRANSPORT_STATUS_OK) {
        audio_transport_stop_safe();
        g_initialized = false;
        return false;
    }
    g_running = true;
#if M55M1_AUDIO_UART_PCM_CAPTURE
    if (!debug_uart_tx_try_enqueue("READY\r\n", 7U)) {
        audio_pipeline_stop_safe();
        return false;
    }
#endif
    return true;
}

void audio_pipeline_wait_for_work(void) {
    if (g_running) {
        audio_transport_wait_for_work();
    }
}

void audio_pipeline_poll(void) {
    audio_transport_capture_timing_t timing;

    if (!g_running) {
        return;
    }
    if (transport_faulted()) {
        ++g_stats.transport_faults;
        restart_stream();
        return;
    }
#if M55M1_AUDIO_UART_PCM_CAPTURE
    if (!g_capture_armed && !g_capture_sent) {
        uint8_t command = 0U;
        if (debug_uart_rx_try_read(&command) && (command == (uint8_t)'C')) {
            g_capture_armed = true;
            (void)debug_uart_tx_try_enqueue("CAPTURE_ARMED\r\n", 15U);
        }
    }
#endif
    if (!audio_transport_take_capture(g_audio_working_set.capture, &timing)) {
        return;
    }
    if (timing.sequence != g_expected_sequence) {
        ++g_stats.sequence_faults;
        restart_stream();
        return;
    }
    g_stats.last_sequence = timing.sequence;
    g_stats.capture_sample_position =
        ((uint64_t)timing.sequence - 1U) * AUDIO_TRANSPORT_SAMPLES_PER_BLOCK;
    g_stats.scheduled_playback_slot = timing.sequence + 1U;
    g_stats.capture_completed_cycle = timing.completed_cycle;
    g_stats.process_start_cycle = audio_transport_cycles_now();

#if M55M1_RUNTIME_TELEMETRY || M55M1_AUDIO_RELEASE_PROBE
    ++g_frame_count;
#if M55M1_AUDIO_RELEASE_PROBE
    g_debug_stats.capture_last_sequence = timing.sequence;
#endif
    record_pcm_stats(g_audio_working_set.capture, &g_debug_stats.input_peak,
                     &g_debug_stats.input_clip_samples);
#endif
    if (!deepfir_inference_process(g_audio_working_set.capture, g_audio_working_set.playback,
                                   AUDIO_TRANSPORT_SAMPLES_PER_BLOCK)) {
#if M55M1_RUNTIME_TELEMETRY || M55M1_AUDIO_RELEASE_PROBE
        ++g_debug_stats.model_failures;
        record_progress(timing.completed_cycle, false);
#if M55M1_AUDIO_RELEASE_PROBE
        finish_release_probe_if_ready();
#endif
#endif
#if M55M1_AUDIO_UART_PCM_CAPTURE
        if (g_capture_armed) {
            capture_send_packet(1U);
            return;
        }
#endif
        ++g_stats.model_faults;
        restart_stream();
        return;
    }
    g_stats.process_end_cycle = audio_transport_cycles_now();
    if (audio_transport_cycles_to_us(audio_transport_cycles_elapsed(
            timing.completed_cycle, g_stats.process_end_cycle)) >= 1000U) {
        ++g_stats.deadline_misses;
        restart_stream();
        return;
    }
    if (transport_faulted()) {
        ++g_stats.transport_faults;
        restart_stream();
        return;
    }
#if M55M1_RUNTIME_TELEMETRY || M55M1_AUDIO_RELEASE_PROBE
    record_pcm_stats(g_audio_working_set.playback, &g_debug_stats.output_peak,
                     &g_debug_stats.output_clip_samples);
#endif

#if M55M1_ENABLE_DAC_I2S_OUTPUT
    const audio_transport_status_t submit_status = audio_transport_submit_tagged(
        g_audio_working_set.playback, &(audio_transport_block_tag_t){
                                          .sequence = timing.sequence,
                                          .capture_completed_cycle = timing.completed_cycle,
                                      });
#if M55M1_RUNTIME_TELEMETRY || M55M1_AUDIO_RELEASE_PROBE
    if (submit_status == AUDIO_TRANSPORT_STATUS_BUSY) {
        ++g_debug_stats.submit_busy;
    } else if (submit_status != AUDIO_TRANSPORT_STATUS_OK) {
        ++g_debug_stats.submit_errors;
    }
#endif
    if (submit_status != AUDIO_TRANSPORT_STATUS_OK) {
        ++g_stats.transport_faults;
        restart_stream();
        return;
    }
#if M55M1_AUDIO_UART_PCM_CAPTURE
    if (g_capture_armed) {
        capture_store_hop(g_audio_working_set.capture, g_audio_working_set.playback,
                          timing.sequence);
        if (!g_running) {
            return;
        }
    }
#endif
#endif
    g_stats.published_cycle = audio_transport_cycles_now();
    const uint32_t elapsed_us = audio_transport_cycles_to_us(
        audio_transport_cycles_elapsed(timing.completed_cycle, g_stats.published_cycle));
    if (elapsed_us > g_stats.max_capture_to_publish_us) {
        g_stats.max_capture_to_publish_us = elapsed_us;
    }
    if (elapsed_us >= 1000U) {
        ++g_stats.deadline_misses;
        restart_stream();
        return;
    }
    ++g_expected_sequence;
#if M55M1_RUNTIME_TELEMETRY || M55M1_AUDIO_RELEASE_PROBE
    record_progress(timing.completed_cycle, true);
#if M55M1_AUDIO_RELEASE_PROBE
    finish_release_probe_if_ready();
#endif
#endif
}

void audio_pipeline_stop_safe(void) {
    if (g_running) {
        audio_transport_stop_safe();
    }

    g_initialized = false;
    g_running = false;
    deepfir_inference_reset();
}
