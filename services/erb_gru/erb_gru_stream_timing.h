/**
 * @file erb_gru_stream_timing.h
 * @brief Source-hop timestamp tracking for the one-hop-delayed WOLA stream.
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
    uint32_t sequence;
    uint32_t capture_completed_cycle;
} erb_gru_source_tag_t;

typedef struct {
    erb_gru_source_tag_t pending;
    bool pending_valid;
} erb_gru_stream_timing_t;

/**
 * @brief Clear one-hop WOLA source history.
 * @param timing Caller-owned tracker, ignored when NULL.
 * @return None.
 *
 * Foreground-only. This discards any pending source tag.
 */
void erb_gru_stream_timing_reset(erb_gru_stream_timing_t *timing);

/**
 * @brief Advance source attribution across the one-hop WOLA delay.
 * @param timing Caller-owned stream tracker.
 * @param capture_sequence Current capture ordinal.
 * @param capture_completed_cycle Current capture completion timestamp.
 * @param output_source Caller-owned destination for the preceding source tag.
 * @return True when delayed output attribution exists; false during preroll or for invalid pointers.
 *
 * Foreground-only and non-reentrant for each tracker. No caller-owned storage is
 * retained after return.
 */
bool erb_gru_stream_timing_advance(
    erb_gru_stream_timing_t *timing,
    uint32_t capture_sequence,
    uint32_t capture_completed_cycle,
    erb_gru_source_tag_t *output_source);

#ifdef __cplusplus
}
#endif
