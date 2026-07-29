/**
 * @file erb_gru_audio_enhancer.h
 * @brief Causal ERB-GRU audio enhancement service.
 * @author Bui Minh Huy
 * @date 2026-07-28
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ERB_GRU_AUDIO_HOP_SAMPLES 160U

typedef enum {
    ERB_GRU_AUDIO_RESULT_ENHANCED = 0,
    ERB_GRU_AUDIO_RESULT_PREROLL_SILENCE,
    ERB_GRU_AUDIO_RESULT_UNITY_FALLBACK,
    ERB_GRU_AUDIO_RESULT_INVALID
} erb_gru_audio_result_t;

bool erb_gru_audio_enhancer_init(void);
erb_gru_audio_result_t erb_gru_audio_enhancer_process(
    const int16_t input[ERB_GRU_AUDIO_HOP_SAMPLES],
    int16_t output[ERB_GRU_AUDIO_HOP_SAMPLES]);
void erb_gru_audio_enhancer_reset(void);

#if MODEL_GOLDEN_TEST_DATA
bool erb_gru_audio_enhancer_flush(int16_t output[ERB_GRU_AUDIO_HOP_SAMPLES]);
#endif

#ifdef __cplusplus
}
#endif
