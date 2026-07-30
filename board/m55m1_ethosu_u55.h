/**
 * @file m55m1_ethosu_u55.h
 * @brief Board binding for the M55M1 Ethos-U55 accelerator.
 * @author Bui Minh Huy
 * @date 2026-07-28
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Bind NPU clocks, pins, memory and cache policy. @return True once foreground inference may register its driver. */
bool m55m1_ethosu_u55_board_init(void);

/** @brief Enable the board-owned NPU interrupt binding after driver registration. @return None. Foreground-only. */
void m55m1_ethosu_u55_board_enable_irq(void);

/** @brief Return the board SysTick counter used for foreground inference profiling. @return Wrap-prone cycle value. */
uint32_t m55m1_ethosu_u55_board_cycles_now(void);

/** @brief Return wrap-safe elapsed SysTick cycles for Ethos-U timing. @param start_cycle Earlier sample. @param end_cycle Later sample. @return Unsigned delta. */
uint32_t m55m1_ethosu_u55_board_cycles_elapsed(
    uint32_t start_cycle,
    uint32_t end_cycle);

#ifdef __cplusplus
}
#endif
