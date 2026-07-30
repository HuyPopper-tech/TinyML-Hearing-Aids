/**
 * @file erb_gru_inference.h
 * @brief Model-specific TFLM adapter for the deployed ERB-GRU network.
 * @author Bui Minh Huy
 * @date 2026-07-29
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ERB_GRU_INFERENCE_BANDS 32U
#define ERB_GRU_INFERENCE_STATE_SIZE 64U

typedef struct {
    uint32_t last_invoke_cycles;
    uint32_t max_invoke_cycles;
    uint32_t invoke_count;
    uint32_t invoke_error_count;
    uint32_t fault_injected_count;
    uint32_t deadline_miss_count;
    size_t arena_used_bytes;
    size_t arena_capacity_bytes;
    uint32_t macs_per_inference;
    uint32_t npu_operator_count;
    uint32_t cpu_operator_count;
    uint32_t model_flash_bytes;
} erb_gru_inference_performance_t;

/** @brief Construct the static no-heap TFLM/Ethos-U inference lifetime. @return True if model, tensors and accelerator contract validate. Foreground-only and idempotent after success. */
bool erb_gru_inference_init(void);
/** @brief Invoke one recurrent model frame. @param feature Quantized 32-band input. @param state1 First committed recurrent state. @param state2 Second committed recurrent state. @param gain Output gains. @param next_state1 Destination for candidate state one. @param next_state2 Destination for candidate state two. @return True only when all outputs are valid; caller must commit candidate states only after true. Foreground-only, non-reentrant. */
bool erb_gru_inference_invoke(
    const int8_t feature[ERB_GRU_INFERENCE_BANDS],
    const int8_t state1[ERB_GRU_INFERENCE_STATE_SIZE],
    const int8_t state2[ERB_GRU_INFERENCE_STATE_SIZE],
    int8_t gain[ERB_GRU_INFERENCE_BANDS],
    int8_t next_state1[ERB_GRU_INFERENCE_STATE_SIZE],
    int8_t next_state2[ERB_GRU_INFERENCE_STATE_SIZE]);
/** @brief Snapshot inference counters. @param performance Destination, ignored when NULL. @return None. Foreground-only best-effort telemetry. */
void erb_gru_inference_get_performance(erb_gru_inference_performance_t *performance);

#ifdef __cplusplus
}
#endif
