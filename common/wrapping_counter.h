/**
 * @file wrapping_counter.h
 * @brief Portable elapsed arithmetic for fixed-width wrapping counters.
 * @author Bui Minh Huy
 * @date 2026-07-29
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Return modular elapsed ticks between two masked counter samples.
 * @param start Earlier counter sample.
 * @param end Later counter sample.
 * @param mask Bit mask describing the counter width.
 * @return Elapsed ticks modulo `mask + 1`.
 */
static inline uint32_t wrapping_counter_elapsed(
    uint32_t start, uint32_t end, uint32_t mask) {
    return (end - start) & mask;
}

#ifdef __cplusplus
}
#endif
