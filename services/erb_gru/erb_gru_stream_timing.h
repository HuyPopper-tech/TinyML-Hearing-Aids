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

void erb_gru_stream_timing_reset(erb_gru_stream_timing_t *timing);
bool erb_gru_stream_timing_advance(
    erb_gru_stream_timing_t *timing,
    uint32_t capture_sequence,
    uint32_t capture_completed_cycle,
    erb_gru_source_tag_t *output_source);

#ifdef __cplusplus
}
#endif
