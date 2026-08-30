/**
 * @file speech_boosting_inference.h
 * @brief Contract-checked TensorFlow Lite Micro and Ethos-U55 inference API.
 * @author Bui Minh Huy
 * @date 2026-08-27
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "speech_boosting_model_contract.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int8_t bytes[SPEECH_BOOSTING_STATE_BYTES];
} speech_boosting_stream_state_t;

typedef struct {
    uint32_t input_state_tensor_copy_microseconds;
    uint32_t neural_network_invocation_microseconds;
    uint32_t output_state_tensor_copy_microseconds;
    uint32_t tensor_arena_capacity_bytes;
    uint32_t tensor_arena_used_bytes;
    uint32_t model_start_address;
    uint32_t model_end_address;
    uint32_t tensor_arena_start_address;
    uint32_t tensor_arena_end_address;
    uint32_t cpu_operator_count;
    uint32_t ethosu_operator_count;
} speech_boosting_inference_diagnostics_t;

/**
 * @brief Initialize and verify the immutable deployed model.
 */
bool speech_boosting_inference_init(void);

/**
 * @brief Return the last model-initialization status code.
 */
int speech_boosting_inference_last_init_status(void);

/**
 * @brief Reset all persistent state tensors to their input zero points.
 */
void speech_boosting_inference_reset(speech_boosting_stream_state_t *state);

/**
 * @brief Run one 64-sample packed INT8 inference transaction.
 *
 * The function is foreground-only and non-reentrant.  State feedback follows
 * semantic manifest mappings and requantizes every output state into its next
 * input state quantization domain before publishing it to @p state.
 */
bool speech_boosting_inference_invoke(
    const int8_t input[SPEECH_BOOSTING_CHUNK_SAMPLES]
                      [SPEECH_BOOSTING_INPUT_CHANNELS],
    speech_boosting_stream_state_t *state,
    int8_t output[SPEECH_BOOSTING_CHUNK_SAMPLES]);

/**
 * @brief Copy the latest foreground-only inference diagnostics.
 */
void speech_boosting_inference_get_diagnostics(
    speech_boosting_inference_diagnostics_t *diagnostics);

#ifdef __cplusplus
}
#endif
