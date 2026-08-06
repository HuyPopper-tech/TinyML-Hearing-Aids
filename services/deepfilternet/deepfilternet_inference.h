/**
 * @file deepfilternet_inference.h
 * @brief Model-specific TFLM/Ethos-U adapter for DeepFilterNet.
 * @author Bui Minh Huy
 * @date 2026-08-05
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DEEPFILTERNET_INFERENCE_INIT_NOT_RUN = 0,
    DEEPFILTERNET_INFERENCE_INIT_READY = 1,
    DEEPFILTERNET_INFERENCE_INIT_MODEL_VERIFY_FAILED = -101,
    DEEPFILTERNET_INFERENCE_INIT_MODEL_CONTRACT_FAILED = -102,
    DEEPFILTERNET_INFERENCE_INIT_ETHOSU_FAILED = -103,
    DEEPFILTERNET_INFERENCE_INIT_RESOLVER_FAILED = -104,
    DEEPFILTERNET_INFERENCE_INIT_ARENA_FAILED = -105,
    DEEPFILTERNET_INFERENCE_INIT_TENSOR_CONTRACT_FAILED = -106,
} deepfilternet_inference_init_status_t;

#define DEEPFILTERNET_INFERENCE_ERB_FEATURE_BYTES 32U
#define DEEPFILTERNET_INFERENCE_COMPLEX_FEATURE_BYTES 288U
#define DEEPFILTERNET_INFERENCE_GAIN_BINS 257U
#define DEEPFILTERNET_INFERENCE_DF_COEFFICIENT_BYTES 1440U
#define DEEPFILTERNET_INFERENCE_STATE_COUNT 7U

typedef struct {
    uint32_t invoke_count;
    uint32_t invoke_error_count;
    uint32_t arena_used_bytes;
    uint32_t arena_capacity_bytes;
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
    int32_t state_min[DEEPFILTERNET_INFERENCE_STATE_COUNT];
    int32_t state_max[DEEPFILTERNET_INFERENCE_STATE_COUNT];
    uint32_t state_sat_count[DEEPFILTERNET_INFERENCE_STATE_COUNT];
} deepfilternet_inference_diagnostics_t;

/**
 * @brief Initialize the static DeepFilterNet TFLM/Ethos-U lifetime.
 * @return True when the model and complete tensor contract validate.
 *
 * Foreground-only and idempotent after success. The interpreter, resolver,
 * recurrent state, and tensor arena remain owned by this adapter.
 */
bool deepfilternet_inference_init(void);

/* Return the most recent initialization result for boot diagnostics. */
int deepfilternet_inference_last_init_status(void);

/**
 * @brief Invoke one quantized DeepFilterNet frame.
 * @param erb_feature Caller-owned quantized ERB feature frame.
 * @param complex_feature Caller-owned quantized complex feature frame.
 * @param gain_bins Caller-owned destination for 257 gain bins.
 * @param df_coefficients Caller-owned destination for 5x144 complex taps.
 * @param alpha Caller-owned destination for the one quantized blend value.
 * @return True only when outputs and recurrent state commit successfully.
 *
 * Foreground-only and non-reentrant. Recurrent state and transaction staging
 * are adapter-owned and are never exposed to callers.
 */
bool deepfilternet_inference_invoke(
    const int8_t erb_feature[32],
    const int8_t complex_feature[288],
    int8_t gain_bins[257],
    int8_t df_coefficients[1440],
    int8_t *alpha);

/**
 * @brief Reset all internal recurrent tensors to their quantized zero points.
 * @return None.
 *
 * Foreground-only. Reset never allocates and does not invoke the model.
 */
void deepfilternet_inference_reset(void);

void deepfilternet_inference_get_diagnostics(
    deepfilternet_inference_diagnostics_t *diagnostics);

#ifdef __cplusplus
}
#endif
