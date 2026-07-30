/**
 * @file m55m1_debug_uart.h
 * @brief Board-owned UART1 console bootstrap for foreground diagnostics.
 * @author Bui Minh Huy
 * @date 2026-07-28
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Configure PA2/PA3 and UART1 at 115200-8N1.
 * @return None.
 *
 * Call from foreground boot code before diagnostic output. The function binds
 * UART1 to the C library retarget and does not transfer resource ownership.
 */
void m55m1_debug_uart_init(void);

/**
 * @brief Enable the UART1 interrupt at the board-assigned background priority.
 * @return None.
 *
 * Call from foreground after the diagnostic TX driver is initialized.
 */
void m55m1_debug_uart_enable_tx_irq(void);

#ifdef __cplusplus
}
#endif
