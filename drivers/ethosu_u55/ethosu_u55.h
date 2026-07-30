/**
 * @file ethosu_u55.h
 * @brief Project driver for the single M55M1 Ethos-U55 instance.
 * @author Bui Minh Huy
 * @date 2026-07-29
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the singleton Ethos-U driver after board binding.
 * @return True when model invocations may proceed.
 *
 * Foreground-only and idempotent after success. No resource ownership is
 * transferred to the caller.
 */
bool ethosu_u55_init(void);

/**
 * @brief Return the free-running board timer used for inference profiling.
 * @return Current cycle count; wrap is expected.
 */
uint32_t ethosu_u55_cycles_now(void);

/**
 * @brief Calculate wrapped elapsed cycles.
 * @param start_cycle Earlier sample.
 * @param end_cycle Later sample.
 * @return Unsigned delta.
 *
 * The calculation is wrap-safe across one counter period.
 */
uint32_t ethosu_u55_cycles_elapsed(uint32_t start_cycle, uint32_t end_cycle);

/**
 * @brief Delay only fault-injection recovery.
 * @param milliseconds Delay interval.
 * @return None.
 *
 * Foreground-only diagnostic hook. Never call from an ISR or production path.
 */
void ethosu_u55_fault_delay_ms(uint32_t milliseconds);

/**
 * @brief Test an elapsed deadline.
 * @param start_cycle Starting cycle.
 * @param milliseconds Limit.
 * @return True when the requested interval has elapsed.
 *
 * Foreground-only and wrap-safe for one counter period.
 */
bool ethosu_u55_deadline_exceeded(uint32_t start_cycle, uint32_t milliseconds);

#ifdef __cplusplus
}
#endif
