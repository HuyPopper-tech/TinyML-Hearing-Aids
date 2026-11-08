/**
 * @file m55m1_debug_uart.h
 * @brief Board-owned console bootstrap for foreground diagnostics.
 * @author Bui Minh Huy
 * @date 2026-07-28
 */

#pragma once

#if M55M1_CUSTOM_DMIC_TEST
// PDMA1 channel 0 is reserved for custom-PCB UART2 TX; mic uses LPPDMA0.
#define M55M1_DEBUG_UART_TX_PDMA PDMA1
#define M55M1_DEBUG_UART_TX_DMA_CHANNEL 0U
#define M55M1_DEBUG_UART_TX_DMA_REQUEST PDMA_UART2_TX

#endif

#ifdef __cplusplus
extern "C" {
#endif

#if M55M1_CUSTOM_DMIC_TEST
/** @brief Enable the custom console's dedicated PDMA1 module clock. */
void m55m1_debug_uart_enable_tx_dma(void);
#endif

/**
 * @brief Configure custom-PCB UART2 PC4/PC5 at 115200-8N1.
 * @return None.
 *
 * Call from foreground boot code before diagnostic output. The function binds
 * the selected UART to the C library retarget without transferring ownership.
 */
void m55m1_debug_uart_init(void);

/**
 * @brief Enable the selected UART IRQ at the board's background priority.
 * @return None.
 *
 * Call from foreground after the diagnostic TX driver is initialized.
 */
void m55m1_debug_uart_enable_tx_irq(void);

#ifdef __cplusplus
}
#endif
