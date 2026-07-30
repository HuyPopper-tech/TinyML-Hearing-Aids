/**
 * @file erb_gru_golden_runner.h
 * @brief Test-only finite-corpus board parity runner.
 * @author Bui Minh Huy
 * @date 2026-07-29
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint64_t signal_energy;
    uint64_t noise_energy;
    uint64_t squared_error;
    uint16_t max_absolute_error;
    uint16_t processed_frames;
} erb_gru_golden_result_t;

/**
 * @brief Run the compiled finite golden corpus through the static enhancer.
 * @param result Caller-owned aggregate destination.
 * @return True on complete parity; false for NULL or any mismatch.
 *
 * Test foreground only and non-reentrant. The runner resets model state, consumes
 * the finite corpus, flushes WOLA overlap, and does not retain result.
 */
bool erb_gru_golden_run(erb_gru_golden_result_t *result);

#ifdef __cplusplus
}
#endif
