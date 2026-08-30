/**
 * @file debug_uart_tx.c
 * @brief IRQ-driven UART1 telemetry transmitter with whole-message admission.
 * @author Bui Minh Huy
 * @date 2026-07-29
 */

#include "drivers/debug_uart_tx/debug_uart_tx.h"

#include "board/m55m1_debug_uart.h"
#include "common/fixed_byte_ring.h"
#include "NuMicro.h"

/* DEBUG_UART_TX_RING_BYTES 1024U was insufficient for 1536-byte snapshots. */
#define DEBUG_UART_TX_RING_BYTES 2048U

static uint8_t g_tx_storage[DEBUG_UART_TX_RING_BYTES]
    __attribute__((section(".bss.DTCM.ZeroInit")));
static fixed_byte_ring_t g_tx_ring;
static volatile uint32_t g_drop_count;
static bool g_initialized;

void debug_uart_tx_init(void) {
    if (g_initialized) {
        return;
    }

    m55m1_debug_uart_init();
    fixed_byte_ring_init(&g_tx_ring, g_tx_storage, sizeof(g_tx_storage));
    g_drop_count = 0U;
    UART_DisableInt(UART1, UART_INTEN_THREIEN_Msk);
    m55m1_debug_uart_enable_tx_irq();
    g_initialized = true;
}

bool debug_uart_tx_try_enqueue(const char *message, size_t length) {
    if (!g_initialized || !fixed_byte_ring_try_enqueue(
            &g_tx_ring, (const uint8_t *)message, length)) {
        ++g_drop_count;
        return false;
    }

    UART_EnableInt(UART1, UART_INTEN_THREIEN_Msk);
    return true;
}

uint32_t debug_uart_tx_drop_count(void) {
    return g_drop_count;
}

void UART1_IRQHandler(void) {
    uint8_t value;

    while (!UART_IS_TX_FULL(UART1) && fixed_byte_ring_try_pop(&g_tx_ring, &value)) {
        UART_WRITE(UART1, value);
    }
    if (fixed_byte_ring_count(&g_tx_ring) == 0U) {
        UART_DisableInt(UART1, UART_INTEN_THREIEN_Msk);
    }
}
