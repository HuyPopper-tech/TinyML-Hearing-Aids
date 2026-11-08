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
 * @return Core-cycle units, 2-cycle resolution, wrapping at 2^25 units.
 *
 * ACLK/2 SysTick keeps counting through CPU Idle. Safe in foreground and ISR.
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
uint32_t m55m1_timebase_cycles_elapsed(uint32_t start_cycle, uint32_t end_cycle);

/**
 * @brief Convert cycles at the configured core clock.
 * @param cycles Cycle delta.
 * @return Truncated microseconds.
 *
 * Uses the board clock configured by m55m1_timebase_init().
 */
uint32_t m55m1_timebase_cycles_to_us(uint32_t cycles);

/** Foreground-only startup/control delay in microseconds. Preserves the shared
 * counter; vendor SysTick delay APIs must not reprogram it after initialization.
 * Not for the realtime path. Long delays are split across counter wraps.
 */
void m55m1_timebase_delay_us(uint32_t microseconds);

#ifdef __cplusplus
}
#endif
