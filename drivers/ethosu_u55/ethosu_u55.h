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

/** @brief Initialize the singleton Ethos-U driver after board binding. @return True when invokes may proceed. Foreground-only, idempotent on success; no ownership transfer. */
bool ethosu_u55_init(void);

/** @brief Return the free-running board timer used for inference profiling. @return Wrap-prone cycle count. */
uint32_t ethosu_u55_cycles_now(void);
/** @brief Calculate wrapped elapsed cycles. @param start_cycle Earlier sample. @param end_cycle Later sample. @return Unsigned delta. */
uint32_t ethosu_u55_cycles_elapsed(uint32_t start_cycle, uint32_t end_cycle);
/** @brief Delay only fault-injection recovery. @param milliseconds Delay interval. @return None. Foreground-only; never call from ISR. */
void ethosu_u55_fault_delay_ms(uint32_t milliseconds);
/** @brief Test an elapsed deadline. @param start_cycle Starting cycle. @param milliseconds Limit. @return True on expiry. Foreground-only. */
bool ethosu_u55_deadline_exceeded(uint32_t start_cycle, uint32_t milliseconds);

#ifdef __cplusplus
}
#endif
