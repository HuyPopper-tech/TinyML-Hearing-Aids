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
    uint32_t arena_used_bytes;
} audio_telemetry_snapshot_t;

void audio_telemetry_init(void);
void audio_telemetry_record_processing_us(uint32_t elapsed_us);
void audio_telemetry_poll(
    const audio_telemetry_snapshot_t *snapshot, bool dmic_block_pending);
void audio_telemetry_fatal(const char *stage, int status);

#ifdef __cplusplus
}
#endif
