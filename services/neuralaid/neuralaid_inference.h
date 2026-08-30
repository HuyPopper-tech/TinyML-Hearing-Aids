/**
 * @file neuralaid_inference.h
 * @brief NeuralAid TFLM/Ethos-U inference API.
 * @author Bui Minh Huy
 * @date 2026-08-23
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NEURALAID_TF_BINS 129U
#define NEURALAID_TF_VALUES (NEURALAID_TF_BINS * 2U)
#define NEURALAID_STATE_BYTES 21444U

typedef struct {
    int8_t bytes[NEURALAID_STATE_BYTES];
} neuralaid_stream_state_t;

typedef struct {
    uint32_t cpu_operator_count;
    uint32_t ethosu_operator_count;
} neuralaid_model_diagnostics_t;

typedef struct {
    uint32_t input_state_copy_us;
    uint32_t invoke_only_us;
    uint32_t output_state_us;
} neuralaid_inference_diagnostics_t;

bool neuralaid_inference_init(void);
int neuralaid_inference_last_init_status(void);

/** @brief Return the operator partition verified from the deployed FlatBuffer. */
void neuralaid_inference_get_model_diagnostics(
    neuralaid_model_diagnostics_t *diagnostics);

/** @brief Return the latest per-invocation CPU and Ethos-U stage timings. */
void neuralaid_inference_get_diagnostics(
    neuralaid_inference_diagnostics_t *diagnostics);
void neuralaid_inference_reset(neuralaid_stream_state_t *state);
bool neuralaid_inference_invoke(
    const int8_t tf_frame[NEURALAID_TF_VALUES],
    neuralaid_stream_state_t *state,
    int8_t enhanced_tf[NEURALAID_TF_VALUES]);

#ifdef __cplusplus
}
#endif
