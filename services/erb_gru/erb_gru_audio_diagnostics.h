/**
 * @file erb_gru_audio_diagnostics.h
 * @brief Internal integer-only ERB-GRU acoustic statistic accumulator.
 * @author Bui Minh Huy
 * @date 2026-07-29
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "services/erb_gru/erb_gru_audio_enhancer.h"

#ifdef __cplusplus
extern "C" {
#endif

void erb_gru_audio_diagnostics_reset(void);
void erb_gru_audio_diagnostics_record_pcm(
    const int16_t *input, const int16_t *output, size_t count);
void erb_gru_audio_diagnostics_record_feature(
    const int8_t *feature, size_t count);
void erb_gru_audio_diagnostics_record_gain_state(
    const int8_t *gain,
    size_t gain_count,
    const int8_t *state1,
    const int8_t *state2,
    size_t state_count);
bool erb_gru_audio_diagnostics_take(erb_gru_audio_diagnostics_t *diagnostics);

#ifdef __cplusplus
}
#endif
