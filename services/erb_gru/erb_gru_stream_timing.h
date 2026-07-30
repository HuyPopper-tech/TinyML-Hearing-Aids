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

/** @brief Clear one-hop WOLA source history. @param timing Caller-owned tracker. @return None. Foreground-only; discards pending source tag. */
void erb_gru_stream_timing_reset(erb_gru_stream_timing_t *timing);
/** @brief Advance source attribution across the one-hop WOLA delay. @param timing Tracker. @param capture_sequence Current capture ordinal. @param capture_completed_cycle Capture timestamp. @param output_source Destination for the preceding source tag. @return True once a delayed output tag exists; false during preroll/invalid input. Foreground-only. */
bool erb_gru_stream_timing_advance(
    erb_gru_stream_timing_t *timing,
    uint32_t capture_sequence,
    uint32_t capture_completed_cycle,
    erb_gru_source_tag_t *output_source);

#ifdef __cplusplus
}
#endif
