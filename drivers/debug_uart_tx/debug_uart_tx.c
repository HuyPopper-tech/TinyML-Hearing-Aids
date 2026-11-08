/**
 * @file debug_uart_tx.c
 * @brief Board-selected UART transmitter with whole-message admission.
 * @author Bui Minh Huy
 * @date 2026-07-29
 */

#include "drivers/debug_uart_tx/debug_uart_tx.h"

#include "board/m55m1_debug_uart.h"
#include "common/fixed_byte_ring.h"
#include "NuMicro.h"

/* DEBUG_UART_TX_RING_BYTES 1024U was insufficient for 1536-byte snapshots. */
#define DEBUG_UART_TX_RING_BYTES 2048U

#if M55M1_CUSTOM_DMIC_TEST
/* Foreground publishes; DMA reads SRAM without cache. Tail advances only on TD.
 * Non-cacheable, cache-line-aligned storage needs no clean; DMB precedes enable.
 */
static NVT_NONCACHEABLE uint8_t g_tx_storage[DEBUG_UART_TX_RING_BYTES] __ALIGNED(32);
static size_t g_dma_length;
static uint32_t g_dma_errors;
#else
static uint8_t g_tx_storage[DEBUG_UART_TX_RING_BYTES]
    __attribute__((section(".bss.DTCM.ZeroInit")));
#endif
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
    UART_DisableInt(DEBUG_PORT, UART_INTEN_THREIEN_Msk | UART_INTEN_TXPDMAEN_Msk);
#if M55M1_CUSTOM_DMIC_TEST
    m55m1_debug_uart_enable_tx_dma();
    PDMA_Open(M55M1_DEBUG_UART_TX_PDMA, 1UL << M55M1_DEBUG_UART_TX_DMA_CHANNEL);
    PDMA_DisableInt(M55M1_DEBUG_UART_TX_PDMA, M55M1_DEBUG_UART_TX_DMA_CHANNEL,
                    PDMA_INT_TRANS_DONE | PDMA_INT_TEMPTY);
    PDMA_CLR_TD_FLAG(M55M1_DEBUG_UART_TX_PDMA, 1UL << M55M1_DEBUG_UART_TX_DMA_CHANNEL);
    PDMA_CLR_ABORT_FLAG(M55M1_DEBUG_UART_TX_PDMA, 1UL << M55M1_DEBUG_UART_TX_DMA_CHANNEL);
#else
    m55m1_debug_uart_enable_tx_irq();
#endif
    g_initialized = true;
}

#if M55M1_CUSTOM_DMIC_TEST
static void debug_uart_tx_dma_fail(void) {
    UART_DisableInt(DEBUG_PORT, UART_INTEN_TXPDMAEN_Msk);
    M55M1_DEBUG_UART_TX_PDMA->CHCTL &= ~(1UL << M55M1_DEBUG_UART_TX_DMA_CHANNEL);
    g_dma_errors = 1U;
}
#endif

void debug_uart_tx_poll(void) {
#if M55M1_CUSTOM_DMIC_TEST
    const uint32_t mask = 1UL << M55M1_DEBUG_UART_TX_DMA_CHANNEL;
    const uint8_t *data;
    size_t length;

    if (!g_initialized || (g_dma_errors != 0U)) {
        return;
    }
    if ((PDMA_GET_ABORT_STS(M55M1_DEBUG_UART_TX_PDMA) & mask) != 0U) {
        PDMA_CLR_ABORT_FLAG(M55M1_DEBUG_UART_TX_PDMA, mask);
        debug_uart_tx_dma_fail();
        return;
    }
    if ((PDMA_GET_TD_STS(M55M1_DEBUG_UART_TX_PDMA) & mask) != 0U) {
        UART_DisableInt(DEBUG_PORT, UART_INTEN_TXPDMAEN_Msk);
        PDMA_CLR_TD_FLAG(M55M1_DEBUG_UART_TX_PDMA, mask);
        __DMB();
        (void)fixed_byte_ring_consume(&g_tx_ring, g_dma_length);
        g_dma_length = 0U;
    }
    if (g_dma_length != 0U) {
        return;
    }
    data = fixed_byte_ring_peek(&g_tx_ring, &length);
    if (data == NULL) {
        return;
    }
    g_dma_length = length;
    PDMA_SetTransferCnt(M55M1_DEBUG_UART_TX_PDMA, M55M1_DEBUG_UART_TX_DMA_CHANNEL, PDMA_WIDTH_8,
                        (uint32_t)length);
    PDMA_SetTransferAddr(M55M1_DEBUG_UART_TX_PDMA, M55M1_DEBUG_UART_TX_DMA_CHANNEL, (uintptr_t)data,
                         PDMA_SAR_INC, (uintptr_t)&DEBUG_PORT->DAT, PDMA_DAR_FIX);
    PDMA_SetBurstType(M55M1_DEBUG_UART_TX_PDMA, M55M1_DEBUG_UART_TX_DMA_CHANNEL, PDMA_REQ_SINGLE,
                      PDMA_BURST_1);
    PDMA_SetTransferMode(M55M1_DEBUG_UART_TX_PDMA, M55M1_DEBUG_UART_TX_DMA_CHANNEL,
                         M55M1_DEBUG_UART_TX_DMA_REQUEST, 0U, 0U);
    __DMB();
    UART_EnableInt(DEBUG_PORT, UART_INTEN_TXPDMAEN_Msk);
#endif
}

bool debug_uart_rx_try_read(uint8_t *value) {
    if (!g_initialized || (value == NULL) || (UART_GET_RX_EMPTY(DEBUG_PORT) != 0U)) {
        return false;
    }

    *value = (uint8_t)UART_READ(DEBUG_PORT);
    return true;
}

bool debug_uart_tx_try_enqueue(const char *message, size_t length) {
    debug_uart_tx_poll();
#if M55M1_CUSTOM_DMIC_TEST
    if (g_dma_errors != 0U) {
        ++g_drop_count;
        return false;
    }
#endif
    if (!g_initialized ||
        !fixed_byte_ring_try_enqueue(&g_tx_ring, (const uint8_t *)message, length)) {
        ++g_drop_count;
        return false;
    }

#if M55M1_CUSTOM_DMIC_TEST
    debug_uart_tx_poll();
#else
    UART_EnableInt(DEBUG_PORT, UART_INTEN_THREIEN_Msk);
#endif
    return true;
}

void debug_uart_tx_flush_blocking(void) {
#if M55M1_CUSTOM_DMIC_TEST
    uint32_t budget = SystemCoreClock;

    if (!g_initialized) {
        return;
    }
    /* A bounded spin budget avoids hanging boot if PDMA/UART stops progressing. */
    while (g_dma_errors == 0U) {
        debug_uart_tx_poll();
        if ((fixed_byte_ring_count(&g_tx_ring) == 0U) && UART_IS_TX_EMPTY(DEBUG_PORT)) {
            return;
        }
        if (budget-- == 0U) {
            debug_uart_tx_dma_fail();
        }
    }
#else
    uint8_t value;

    if (!g_initialized) {
        return;
    }
    UART_DisableInt(DEBUG_PORT, UART_INTEN_THREIEN_Msk);
    while (fixed_byte_ring_try_pop(&g_tx_ring, &value)) {
        while (UART_IS_TX_FULL(DEBUG_PORT)) {
        }
        UART_WRITE(DEBUG_PORT, value);
    }
    while (UART_IS_TX_FULL(DEBUG_PORT)) {
    }
#endif
}

uint32_t debug_uart_tx_dma_error_count(void) {
#if M55M1_CUSTOM_DMIC_TEST
    return g_dma_errors;
#else
    return 0U;
#endif
}

uint32_t debug_uart_tx_drop_count(void) {
    return g_drop_count;
}

#if !M55M1_CUSTOM_DMIC_TEST
void DEBUG_PORT_IRQHandler(void) {
    uint8_t value;

    while (!UART_IS_TX_FULL(DEBUG_PORT) && fixed_byte_ring_try_pop(&g_tx_ring, &value)) {
        UART_WRITE(DEBUG_PORT, value);
    }
    if (fixed_byte_ring_count(&g_tx_ring) == 0U) {
        UART_DisableInt(DEBUG_PORT, UART_INTEN_THREIEN_Msk);
    }
}
#endif
