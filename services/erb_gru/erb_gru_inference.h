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

/**
 * @brief Construct the static TFLM/Ethos-U inference lifetime.
 * @return True when the model, tensors, and accelerator contract validate.
 *
 * Foreground-only and idempotent after success. All interpreter and tensor
 * storage is static; no ownership is transferred to the caller.
 */
bool erb_gru_inference_init(void);

/**
 * @brief Invoke one recurrent ERB-GRU frame.
 * @param feature Caller-owned quantized 32-band input.
 * @param state1 Caller-owned committed first recurrent state.
 * @param state2 Caller-owned committed second recurrent state.
 * @param gain Caller-owned destination for quantized band gains.
 * @param next_state1 Caller-owned destination for candidate state one.
 * @param next_state2 Caller-owned destination for candidate state two.
 * @return True only when every output is valid.
 *
 * Foreground-only and non-reentrant. Inputs and outputs are not retained. The
 * caller must commit candidate states only after a true result.
 */
bool erb_gru_inference_invoke(
    const int8_t feature[ERB_GRU_INFERENCE_BANDS],
    const int8_t state1[ERB_GRU_INFERENCE_STATE_SIZE],
    const int8_t state2[ERB_GRU_INFERENCE_STATE_SIZE],
    int8_t gain[ERB_GRU_INFERENCE_BANDS],
    int8_t next_state1[ERB_GRU_INFERENCE_STATE_SIZE],
    int8_t next_state2[ERB_GRU_INFERENCE_STATE_SIZE]);

/**
 * @brief Snapshot inference timing, memory, and error counters.
 * @param performance Caller-owned destination, ignored when NULL.
 * @return None.
 *
 * Foreground-only best-effort telemetry; the service does not retain the
 * destination.
 */
void erb_gru_inference_get_performance(erb_gru_inference_performance_t *performance);

#ifdef __cplusplus
}
#endif
