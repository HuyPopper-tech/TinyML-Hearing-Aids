/**
 * @file debug_uart_tx.h
 * @brief Non-blocking board-selected UART telemetry transmitter.
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
 * @brief Initialize the telemetry queue and board-selected IRQ/DMA transmitter.
 * @return None.
 *
 * Call once from foreground before enqueueing or enabling the UART TX IRQ.
 */
void debug_uart_tx_init(void);

/**
 * @brief Reap DMA completion and start the next queued span without blocking.
 *
 * Foreground-only; call from the custom diagnostic event loop. The NuMaker
 * IRQ backend requires no polling. No message storage is released before DMA
 * completion. An abort or exhausted boot-flush budget latches a fault until reset.
 */
void debug_uart_tx_poll(void);

/**
 * @brief Read one received board-selected UART byte without blocking.
 * @param value Destination for the byte.
 * @return True when a byte was available and copied; false otherwise.
 *
 * Foreground-only; used by finite board diagnostics for a host trigger.
 */
bool debug_uart_rx_try_read(uint8_t *value);

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
 * @brief Drain all accepted board-selected UART telemetry before a finite diagnostic exits.
 *
 * Foreground-only. This deliberately blocks and is intended only for bounded
 * boot diagnostics, never the live audio path. The custom DMA backend waits
 * for the final stop bit, with a finite spin budget; inspect the DMA error
 * count if the flush returns after a fault.
 */
void debug_uart_tx_flush_blocking(void);

/** @brief Return DMA abort/stall faults; zero for the NuMaker IRQ backend. */
uint32_t debug_uart_tx_dma_error_count(void);

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
