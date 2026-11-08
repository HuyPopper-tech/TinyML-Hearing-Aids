/**
 * @file m55m1_debug_uart.c
 * @brief M55M1 console binding: UART2 custom PCB.
 * @author Bui Minh Huy
 * @date 2026-07-28
 */

#include <stdbool.h>

#include "board/m55m1_debug_uart.h"
#include "NuMicro.h"

static bool g_debug_uart_initialized;

void SetDebugUartMFP(void) {
    SET_UART2_RXD_PC4();
    SET_UART2_TXD_PC5();
}

void InitDebugUart(void) {
    UART_Open(DEBUG_PORT, 115200U);
}

void m55m1_debug_uart_init(void) {
    if (g_debug_uart_initialized) {
        return;
    }

    SYS_UnlockReg();
    CLK_EnableXtalRC(CLK_SRCCTL_HIRCEN_Msk);
    CLK_WaitClockReady(CLK_STATUS_HIRCSTB_Msk);
    CLK_EnableModuleClock(GPIOC_MODULE);
    SetDebugUartMFP();
    CLK_SetModuleClock(DEBUG_PORT_MODULE, DEBUG_PORT_CLKSEL, DEBUG_PORT_CLKDIV);
    CLK_EnableModuleClock(DEBUG_PORT_MODULE);
    SYS_ResetModule(DEBUG_PORT_RST);
    SYS_LockReg();
    InitDebugUart();
    g_debug_uart_initialized = true;
}

void m55m1_debug_uart_enable_tx_irq(void) {
    NVIC_SetPriority(DEBUG_PORT_IRQn, 3U);
    NVIC_EnableIRQ(DEBUG_PORT_IRQn);
}

#if M55M1_CUSTOM_DMIC_TEST
void m55m1_debug_uart_enable_tx_dma(void) {
    SYS_UnlockReg();
    CLK_EnableModuleClock(PDMA1_MODULE);
    SYS_LockReg();
}
#endif
