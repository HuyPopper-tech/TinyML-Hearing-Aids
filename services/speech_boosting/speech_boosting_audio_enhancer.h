/**
 * @file speech_boosting_audio_enhancer.h
 * @brief PCM16 adapter for the deployed Speech Boosting stream model.
 * @author Bui Minh Huy
 * @date 2026-08-27
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "services/speech_boosting/speech_boosting_inference.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    speech_boosting_stream_state_t state;
} speech_boosting_audio_stream_t;

typedef struct {
    uint32_t lookahead_assembly_and_pcm_quantization_microseconds;
    uint32_t neural_network_invocation_microseconds;
    uint32_t output_dequantization_microseconds;
    uint32_t invoke_error_count;
    uint32_t output_saturation_count;
} speech_boosting_audio_diagnostics_t;

/**
 * @brief Initialize the static inference backend.
 */
bool speech_boosting_audio_enhancer_init(void);

/**
 * @brief Reset one foreground-owned audio stream.
 */
void speech_boosting_audio_enhancer_reset(speech_boosting_audio_stream_t *stream);

/**
 * @brief Process 64 current samples plus 16 future samples into 64 PCM16 samples.
 * @return True only when the accelerator invocation and state feedback succeeded.
 */
bool speech_boosting_audio_enhancer_process(
    speech_boosting_audio_stream_t *stream,
    const int16_t capture[SPEECH_BOOSTING_CAPTURE_SAMPLES],
    int16_t output[SPEECH_BOOSTING_CHUNK_SAMPLES]);

/**
 * @brief Copy the latest foreground-only enhancer diagnostics.
 */
void speech_boosting_audio_enhancer_get_diagnostics(
    speech_boosting_audio_diagnostics_t *diagnostics);

#ifdef __cplusplus
}
#endif
