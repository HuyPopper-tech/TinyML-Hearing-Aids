/**
 * @file enhancement_passthrough.h
 * @brief Stateless PCM16 enhancement boundary for the model-free baseline.
 * @author Bui Minh Huy
 * @date 2026-09-13
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Copies one PCM16 block without changing its samples.
 *
 * A zero-length block is valid with null pointers. Non-zero blocks require
 * both pointers and support overlapping input/output ranges.
 */
bool enhancement_passthrough_process(const int16_t *input,
                                     int16_t *output,
                                     size_t sample_count);

#ifdef __cplusplus
}
#endif
