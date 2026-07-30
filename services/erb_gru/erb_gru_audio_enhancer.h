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

#if M55M1_ERB_GRU_DIAGNOSTICS
typedef struct {
    uint32_t frames;
    uint64_t input_energy;
    uint64_t output_energy;
    uint32_t pcm_samples;
    uint32_t input_clip_count;
    uint32_t output_clip_count;
    uint16_t input_peak;
    uint16_t output_peak;

    int64_t feature_sum;
    uint32_t feature_count;
    uint32_t feature_sat_low;
    uint32_t feature_sat_high;
    int8_t feature_min;
    int8_t feature_max;

    int64_t gain_sum;
    uint32_t gain_count;
    uint32_t gain_near_zero;
    uint32_t gain_near_unity;
    int8_t gain_min;
    int8_t gain_max;

    int8_t state1_min;
    int8_t state1_max;
    int8_t state2_min;
    int8_t state2_max;
    uint32_t state_sat_count;

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
} erb_gru_audio_diagnostics_t;
#endif

bool erb_gru_audio_enhancer_init(void);
erb_gru_audio_result_t erb_gru_audio_enhancer_process(
    const int16_t input[ERB_GRU_AUDIO_HOP_SAMPLES],
    int16_t output[ERB_GRU_AUDIO_HOP_SAMPLES]);
void erb_gru_audio_enhancer_reset(void);

#if M55M1_ERB_GRU_DIAGNOSTICS
bool erb_gru_audio_enhancer_take_diagnostics(
    erb_gru_audio_diagnostics_t *diagnostics);
uint32_t erb_gru_audio_enhancer_static_bytes(void);
#endif

#if MODEL_GOLDEN_TEST_DATA
bool erb_gru_audio_enhancer_flush(int16_t output[ERB_GRU_AUDIO_HOP_SAMPLES]);
#endif

#ifdef __cplusplus
}
#endif
