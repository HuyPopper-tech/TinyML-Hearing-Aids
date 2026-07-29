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

void debug_uart_tx_init(void);
bool debug_uart_tx_try_enqueue(const char *message, size_t length);
uint32_t debug_uart_tx_drop_count(void);

#ifdef __cplusplus
}
#endif
