/**
 * @file debug_uart_tx.h
 * @brief Non-blocking UART1 telemetry transmitter.
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

/**
 * @brief Initialize the foreground-producer/UART-ISR-consumer telemetry queue.
 * @return None.
 *
 * Call once from foreground before enqueueing or enabling the UART TX IRQ.
 */
void debug_uart_tx_init(void);

/**
 * @brief Copy a telemetry message without blocking.
 * @param message Caller-owned bytes.
 * @param length Byte count.
 * @return True if copied; false if full/invalid, with all-or-drop semantics.
 *
 * Foreground-only producer. The message is copied before publication and the
 * caller retains ownership.
 */
bool debug_uart_tx_try_enqueue(const char *message, size_t length);

/**
 * @brief Return rejected telemetry messages.
 * @return Monotonic best-effort drop count.
 *
 * Foreground diagnostic snapshot; the ISR never updates this counter.
 */
uint32_t debug_uart_tx_drop_count(void);

#ifdef __cplusplus
}
#endif
