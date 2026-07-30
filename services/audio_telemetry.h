/**
 * @file audio_telemetry.h
 * @brief Rate-limited foreground audio telemetry.
 * @author Bui Minh Huy
 * @date 2026-07-29
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "services/erb_gru/erb_gru_audio_enhancer.h"

#ifdef __cplusplus
extern "C" {
#endif

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
    uint32_t fault_injected;
    uint32_t invoke_count;
    uint32_t arena_used_bytes;
    uint32_t arena_capacity_bytes;
    uint32_t dsp_static_bytes;
    uint32_t stack_configured_bytes;
    uint32_t macs_per_inference;
    uint32_t npu_operator_count;
    uint32_t cpu_operator_count;
    uint32_t model_flash_bytes;
    erb_gru_audio_diagnostics_t audio;
} audio_telemetry_snapshot_t;

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
