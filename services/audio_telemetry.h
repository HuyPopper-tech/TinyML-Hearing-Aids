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

/** @brief Reset foreground telemetry counters. @return None. */
void audio_telemetry_init(void);
/** @brief Record an enhancer duration. @param elapsed_us Frame processing time. @return None. Foreground-only. */
void audio_telemetry_record_processing_us(uint32_t elapsed_us);
/** @brief Record one model invocation duration. @param elapsed_us Invoke time. @return None. Foreground-only. */
void audio_telemetry_record_invoke_us(uint32_t elapsed_us);
/** @brief Record capture-to-DAC latency. @param elapsed_us End-to-end duration. @param sequence Source frame ordinal. @return None. Foreground-only. */
void audio_telemetry_record_e2e_us(uint32_t elapsed_us, uint32_t sequence);
/** @brief Rate-limit and emit a telemetry snapshot. @param snapshot Caller-owned aggregate read only for this call. @param dmic_block_pending Current capture availability. @return None. Foreground-only; never blocks audio or runs in an ISR. */
void audio_telemetry_poll(
    const audio_telemetry_snapshot_t *snapshot, bool dmic_block_pending);
/** @brief Emit a terminal failure report. @param stage Static failure stage text. @param status Platform status. @return None. Foreground-only; caller retains stage lifetime. */
void audio_telemetry_fatal(const char *stage, int status);

#ifdef __cplusplus
}
#endif
