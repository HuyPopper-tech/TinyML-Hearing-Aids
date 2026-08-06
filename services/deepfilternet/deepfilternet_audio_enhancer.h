/**
 * @file deepfilternet_audio_enhancer.h
 * @brief DeepFilterNet scalar/MVE STFT, post-processing, and WOLA service.
 * @author Bui Minh Huy
 * @date 2026-08-06
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DEEPFILTERNET_AUDIO_FRAME_SAMPLES 512U
#define DEEPFILTERNET_AUDIO_HOP_SAMPLES 256U

typedef struct {
    uint32_t fft_us;
    uint32_t feature_us;
    uint32_t npu_us;
    uint32_t df_us;
    uint32_t wola_us;
    uint32_t e2e_us;
    uint32_t invoke_count;
    uint32_t fallback_count;
    uint32_t reset_count;
    uint32_t deadline_miss_count;
    uint32_t npu_error_count;
    uint32_t parity_max_pcm_difference;
    uint32_t parity_violation_count;
    uint32_t input_peak;
    uint32_t model_input_peak;
    uint32_t output_peak;
    uint64_t input_energy;
    uint64_t model_input_energy;
    uint64_t output_energy;
    uint32_t pcm_samples;
    uint32_t input_clip_count;
    uint32_t model_input_clip_count;
    uint32_t output_clip_count;
    int32_t feature_min;
    int32_t feature_max;
    uint32_t feature_sat_count;
    int32_t complex_min;
    int32_t complex_max;
    uint32_t complex_sat_count;
    int32_t gain_min;
    int32_t gain_max;
    uint32_t gain_sat_count;
    int32_t df_min;
    int32_t df_max;
    uint32_t df_sat_count;
    int32_t alpha_min;
    int32_t alpha_max;
    uint32_t alpha_sat_count;
    int32_t state_min[7];
    int32_t state_max[7];
    uint32_t state_sat_count[7];
    uint32_t arena_used_bytes;
    uint32_t arena_capacity_bytes;
    uint32_t dsp_static_bytes;
} deepfilternet_audio_diagnostics_t;

typedef enum {
    DEEPFILTERNET_AUDIO_RESULT_ENHANCED = 0,
    DEEPFILTERNET_AUDIO_RESULT_PREROLL_SILENCE,
    DEEPFILTERNET_AUDIO_RESULT_UNITY_FALLBACK,
    DEEPFILTERNET_AUDIO_RESULT_INVALID
} deepfilternet_audio_result_t;

bool deepfilternet_audio_enhancer_init(void);

/* Return the most recent initialization result for boot diagnostics. */
int deepfilternet_audio_enhancer_last_init_status(void);

deepfilternet_audio_result_t deepfilternet_audio_enhancer_process(
    const int16_t input[DEEPFILTERNET_AUDIO_HOP_SAMPLES],
    int16_t output[DEEPFILTERNET_AUDIO_HOP_SAMPLES]);

void deepfilternet_audio_enhancer_reset(void);

/* Alias used by stream owners when a capture discontinuity is detected. */
void deepfilternet_audio_enhancer_discontinuity(void);

void deepfilternet_audio_enhancer_get_diagnostics(
    deepfilternet_audio_diagnostics_t *diagnostics);

#if MODEL_GOLDEN_TEST_DATA
bool deepfilternet_audio_enhancer_flush(
    int16_t output[DEEPFILTERNET_AUDIO_HOP_SAMPLES]);
#endif

#ifdef __cplusplus
}
#endif
