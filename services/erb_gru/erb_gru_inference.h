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
    uint32_t deadline_miss_count;
    size_t arena_used_bytes;
    size_t arena_capacity_bytes;
} erb_gru_inference_performance_t;

bool erb_gru_inference_init(void);
bool erb_gru_inference_invoke(
    const int8_t feature[ERB_GRU_INFERENCE_BANDS],
    const int8_t state1[ERB_GRU_INFERENCE_STATE_SIZE],
    const int8_t state2[ERB_GRU_INFERENCE_STATE_SIZE],
    int8_t gain[ERB_GRU_INFERENCE_BANDS],
    int8_t next_state1[ERB_GRU_INFERENCE_STATE_SIZE],
    int8_t next_state2[ERB_GRU_INFERENCE_STATE_SIZE]);
void erb_gru_inference_get_performance(erb_gru_inference_performance_t *performance);

#ifdef __cplusplus
}
#endif
