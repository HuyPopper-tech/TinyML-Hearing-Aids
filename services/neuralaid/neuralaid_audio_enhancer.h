/**
 * @file neuralaid_audio_enhancer.h
 * @brief 96-sample NeuralAid audio service.
 * @author Bui Minh Huy
 * @date 2026-08-23
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "services/neuralaid/neuralaid_inference.h"

#ifdef __cplusplus
extern "C" {
#endif
#define NEURALAID_AUDIO_CHUNK_SAMPLES 96U

typedef enum {
    NEURALAID_AUDIO_ENHANCED,
    NEURALAID_AUDIO_PREROLL_SILENCE,
    NEURALAID_AUDIO_PCM_BYPASS,
} neuralaid_audio_result_t;
typedef struct {
    int16_t ring[256];
    float ola_tail[64];
    float ola_weight_tail[64];
    int16_t previous_sample;
    uint16_t write_index;
    uint32_t samples_seen;
    uint32_t logical_frame;
    neuralaid_stream_state_t state;
} neuralaid_audio_stream_t;

typedef struct {
    uint32_t preemphasis_us;
    uint32_t context_us;
    uint32_t rfft_us;
    uint32_t quantize_us;
    uint32_t invoke_us;
    uint32_t ifft_ola_us;
    uint32_t total_us;
    uint32_t bypass_count;
    uint32_t preroll_count;
    uint32_t invoke_error_count;
    uint32_t logical_frame;
    uint32_t required_samples;
    uint32_t saturation_count;
} neuralaid_audio_diagnostics_t;
bool neuralaid_audio_enhancer_init(void);
int neuralaid_audio_enhancer_last_init_status(void);
void neuralaid_audio_enhancer_reset(neuralaid_audio_stream_t *stream);
void neuralaid_audio_enhancer_get_diagnostics(
    neuralaid_audio_diagnostics_t *diagnostics);
neuralaid_audio_result_t neuralaid_audio_enhancer_process(
    neuralaid_audio_stream_t *stream,
    const int16_t input[NEURALAID_AUDIO_CHUNK_SAMPLES],
    int16_t output[NEURALAID_AUDIO_CHUNK_SAMPLES]);
#ifdef __cplusplus
}
#endif
