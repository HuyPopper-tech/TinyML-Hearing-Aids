/**
 * @file audio_telemetry.h
 * @brief Rate-limited foreground audio telemetry.
 * @author Bui Minh Huy
 * @date 2026-07-29
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Diagnostic-only counters shared by the DFN service and the foreground
 * formatter.  The shape intentionally stays POD so callers can snapshot it
 * without pulling model or DSP headers into the telemetry service.
 */
typedef struct {
    uint32_t frames;
    uint32_t pcm_samples;
    uint64_t input_energy;
    uint64_t model_input_energy;
    uint64_t output_energy;
    uint32_t input_peak;
    uint32_t model_input_peak;
    uint32_t output_peak;
    uint32_t input_clip_count;
    uint32_t model_input_clip_count;
    uint32_t output_clip_count;
    int32_t feature_min;
    int32_t feature_max;
    int64_t feature_sum;
    uint32_t feature_count;
    uint32_t feature_sat_low;
    uint32_t feature_sat_high;
    int32_t gain_min;
    int32_t gain_max;
    int64_t gain_sum;
    uint32_t gain_count;
    uint32_t gain_sat_count;
    uint32_t gain_near_zero;
    uint32_t gain_near_unity;
    int32_t state1_min;
    int32_t state1_max;
    int32_t state2_min;
    int32_t state2_max;
    uint32_t state_sat_count;
    int32_t complex_min;
    int32_t complex_max;
    uint32_t complex_sat_count;
    int32_t df_min;
    int32_t df_max;
    uint32_t df_sat_count;
    int32_t alpha_min;
    int32_t alpha_max;
    uint32_t alpha_sat_count;
    int32_t erb_cache_min;
    int32_t erb_cache_max;
    uint32_t erb_cache_sat_count;
    int32_t complex_cache_min;
    int32_t complex_cache_max;
    uint32_t complex_cache_sat_count;
    int32_t enc0_min;
    int32_t enc0_max;
    uint32_t enc0_sat_count;
    int32_t enc1_min;
    int32_t enc1_max;
    uint32_t enc1_sat_count;
    int32_t enc2_min;
    int32_t enc2_max;
    uint32_t enc2_sat_count;
    int32_t df0_min;
    int32_t df0_max;
    uint32_t df0_sat_count;
    int32_t df1_min;
    int32_t df1_max;
    uint32_t df1_sat_count;
    uint64_t spectrum_input_energy_q30;
    uint64_t spectrum_output_energy_q30;
    uint64_t ifft_energy_q30;
    uint64_t wola_energy_q30;
    uint32_t ifft_sample_count;
    uint32_t wola_sample_count;
    uint64_t bin_gain_sum_q15;
    uint32_t bin_gain_count;
    uint32_t bin_gain_min_q15;
    uint32_t bin_gain_max_q15;
    uint32_t wola_peak_q15;
} audio_telemetry_audio_stats_t;

typedef struct {
    uint32_t average_us;
    uint32_t maximum_us;
    uint32_t p99_us;
} audio_telemetry_timing_t;

typedef struct {
    uint32_t frame_count;
    uint32_t enhance_last_us;
    uint32_t enhance_max_us;
    uint32_t invoke_last_us;
    uint32_t invoke_max_us;
    uint32_t unity_fallback;
    uint32_t deadline_miss;
    uint32_t dmic_overrun;
    uint32_t dmic_dma_error;
    uint32_t dac_busy;
    uint32_t dac_underrun;
    uint32_t dac_dma_error;
    uint32_t npu_error;
    uint32_t parity_max_pcm_difference;
    uint32_t parity_violation_count;
    uint32_t fault_injected;
    uint32_t invoke_count;
    uint32_t arena_used_bytes;
    uint32_t arena_capacity_bytes;
    uint32_t dsp_static_bytes;
    uint32_t stack_configured_bytes;
    uint32_t stack_high_water_bytes;
    uint32_t stack_headroom_bytes;
    uint32_t macs_per_inference;
    uint32_t npu_operator_count;
    uint32_t cpu_operator_count;
    uint32_t model_flash_bytes;
    const char *model_hash;
    uint32_t model_hash_valid;
    uint32_t reset_count;
    uint32_t uart_error;
    uint32_t transport_busy;
    uint32_t transport_error;
    uint32_t hop_samples;
    uint32_t frame_samples;
    uint32_t core_hz;
    uint32_t dsp_mode;
    uint32_t diagnostics_enabled;
    int32_t output_min;
    int32_t output_max;
    uint32_t output_sat;
    uint32_t cache_sat;
    uint32_t feature_sat;
    audio_telemetry_timing_t fft;
    audio_telemetry_timing_t feature;
    audio_telemetry_timing_t npu;
    audio_telemetry_timing_t df;
    audio_telemetry_timing_t wola;
    audio_telemetry_timing_t e2e;
    audio_telemetry_timing_t transport;
    audio_telemetry_audio_stats_t audio;
} audio_telemetry_snapshot_t;

typedef struct {
    const char *build;
    const char *git_rev;
    uint32_t git_dirty;
    const char *dsp_mode;
    uint32_t hop_samples;
    uint32_t frame_samples;
    uint32_t input_gain_db;
    uint32_t core_hz;
    const char *model_hash;
    uint32_t model_bytes;
} audio_telemetry_boot_info_t;

/**
 * @brief Reset foreground telemetry counters and histogram state.
 * @return None.
 *
 * Foreground-only before the diagnostic observation interval begins.
 */
void audio_telemetry_init(void);

/**
 * @brief Record one enhancer duration.
 * @param elapsed_us Complete frame processing time in microseconds.
 * @return None.
 *
 * Foreground-only and constant-time; no formatting or allocation occurs.
 */
void audio_telemetry_record_processing_us(uint32_t elapsed_us);

/**
 * @brief Record one model invocation duration.
 * @param elapsed_us Ethos-U invocation time in microseconds.
 * @return None.
 *
 * Foreground-only and constant-time.
 */
void audio_telemetry_record_invoke_us(uint32_t elapsed_us);

/**
 * @brief Record capture-to-DAC latency for one source frame.
 * @param elapsed_us End-to-end duration in microseconds.
 * @param sequence Source capture ordinal used for gap detection.
 * @return None.
 *
 * Foreground-only. The source sequence must follow WOLA one-hop attribution.
 */
void audio_telemetry_record_e2e_us(uint32_t elapsed_us, uint32_t sequence);

void audio_telemetry_record_fft_us(uint32_t elapsed_us);
void audio_telemetry_record_feature_us(uint32_t elapsed_us);
void audio_telemetry_record_df_us(uint32_t elapsed_us);
void audio_telemetry_record_wola_us(uint32_t elapsed_us);
void audio_telemetry_record_transport_us(uint32_t elapsed_us, uint32_t sequence);
void audio_telemetry_record_audio_stats(
    const audio_telemetry_audio_stats_t *stats);

void audio_telemetry_boot(const audio_telemetry_boot_info_t *info);

/**
 * @brief Rate-limit, format, and enqueue one atomic telemetry snapshot.
 * @param snapshot Caller-owned aggregate read only during this call.
 * @param dmic_block_pending True when capture work must take priority.
 * @return None.
 *
 * Foreground-only and non-blocking. Formatting is deferred while audio is
 * pending, and a full UART queue drops the whole message.
 */
void audio_telemetry_poll(
    const audio_telemetry_snapshot_t *snapshot, bool dmic_block_pending);

/**
 * @brief Emit a terminal diagnostic failure report.
 * @param stage Null-terminated failure-stage text.
 * @param status Platform status value.
 * @return None.
 *
 * Foreground-only before safe shutdown. The caller retains stage ownership and
 * must keep it valid for this call.
 */
void audio_telemetry_fatal(const char *stage, int status);

#ifdef __cplusplus
}
#endif
