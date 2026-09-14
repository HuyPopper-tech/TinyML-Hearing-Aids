/**
 * @file m55m1_ethosu_board.h
 * @brief Board binding for the single M55M1 Ethos-U55 instance.
 * @author Bui Minh Huy
 * @date 2026-09-14
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Enable the NPU clock, reset the NPU, and prepare the timebase.
 * @return True when the board binding is ready for runtime initialization.
 *
 * Foreground-only and idempotent. The NPU interrupt remains disabled until
 * the runtime has successfully registered its core driver.
 */
bool m55m1_ethosu_board_init(void);

/**
 * @brief Enable the board-owned NPU interrupt after driver registration.
 * @return None.
 *
 * Foreground-only. The runtime must be initialized before this call.
 */
void m55m1_ethosu_board_enable_irq(void);

/**
 * @brief Disable and clear the board-owned NPU interrupt.
 * @return None.
 *
 * Foreground-only and safe when the interrupt is already disabled.
 */
void m55m1_ethosu_board_disable_irq(void);

/**
 * @brief Return the memory-mapped NPU register base address.
 * @return Board-owned NPU register base; no ownership is transferred.
 */
const void *m55m1_ethosu_board_get_base_address(void);

/**
 * @brief Read the board cycle counter used for runtime measurements.
 * @return Current wrap-safe cycle value.
 */
uint32_t m55m1_ethosu_board_cycles_now(void);

/**
 * @brief Calculate elapsed cycles across one counter wrap.
 * @param start_cycle Earlier sample.
 * @param end_cycle Later sample.
 * @return Unsigned wrap-safe cycle delta.
 */
uint32_t m55m1_ethosu_board_cycles_elapsed(
    uint32_t start_cycle,
    uint32_t end_cycle);

#ifdef __cplusplus
}
#endif