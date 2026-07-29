/**
 * @file erb_gru_stream_timing.c
 * @brief Source-hop timestamp tracking for the one-hop-delayed WOLA stream.
 * @author Bui Minh Huy
 * @date 2026-07-29
 */

#include <stddef.h>

#include "services/erb_gru/erb_gru_stream_timing.h"

void erb_gru_stream_timing_reset(erb_gru_stream_timing_t *timing)
{
    if (timing != NULL) {
        timing->pending = (erb_gru_source_tag_t){0U, 0U};
        timing->pending_valid = false;
    }
}

bool erb_gru_stream_timing_advance(
    erb_gru_stream_timing_t *timing,
    uint32_t capture_sequence,
    uint32_t capture_completed_cycle,
    erb_gru_source_tag_t *output_source)
{
    bool output_valid;

    if ((timing == NULL) || (output_source == NULL)) {
        return false;
    }
    output_valid = timing->pending_valid;
    if (output_valid) {
        *output_source = timing->pending;
    }
    timing->pending = (erb_gru_source_tag_t){
        .sequence = capture_sequence,
        .capture_completed_cycle = capture_completed_cycle,
    };
    timing->pending_valid = true;
    return output_valid;
}
