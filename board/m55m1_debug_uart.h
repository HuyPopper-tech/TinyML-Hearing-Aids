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

/** Configure PA2/PA3 and UART1 at 115200-8N1 for the C library retarget. */
void m55m1_debug_uart_init(void);

/** Enable the UART1 interrupt at the board-assigned background priority. */
void m55m1_debug_uart_enable_tx_irq(void);

#ifdef __cplusplus
}
#endif
