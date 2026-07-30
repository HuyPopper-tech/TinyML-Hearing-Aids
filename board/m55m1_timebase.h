/**
 * @file m55m1_timebase.h
 * @brief Board-owned wrap-safe cycle timebase for audio and Ethos-U timing.
 * @author Bui Minh Huy
 * @date 2026-07-30
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the board cycle counter before foreground timing clients run.
 * @return None.
 *
 * Foreground-only during board initialization.
 */
void m55m1_timebase_init(void);

/**
 * @brief Read the free-running counter.
 * @return Current cycle value; wrap is expected.
 *
 * Safe for foreground timing snapshots.
 */
uint32_t m55m1_timebase_cycles_now(void);

/**
 * @brief Calculate wrap-safe elapsed cycles.
 * @param start_cycle Earlier sample.
 * @param end_cycle Later sample.
 * @return Unsigned wrapped delta.
 *
 * The calculation is valid across one counter wrap.
 */
uint32_t m55m1_timebase_cycles_elapsed(
    uint32_t start_cycle,
    uint32_t end_cycle);

/**
 * @brief Convert cycles at the configured core clock.
 * @param cycles Cycle delta.
 * @return Truncated microseconds.
 *
 * Uses the board clock configured by m55m1_timebase_init().
 */
uint32_t m55m1_timebase_cycles_to_us(uint32_t cycles);

#ifdef __cplusplus
}
#endif
