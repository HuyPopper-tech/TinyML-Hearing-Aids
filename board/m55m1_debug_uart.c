/**
 * @file m55m1_debug_uart.c
 * @brief M55M1 board binding for the UART1 foreground console.
 * @author Bui Minh Huy
 * @date 2026-07-28
 */

#include <stdbool.h>

#include "board/m55m1_debug_uart.h"
#include "NuMicro.h"

static bool g_debug_uart_initialized;

void SetDebugUartMFP(void)
{
    SET_UART1_RXD_PA2();
    SET_UART1_TXD_PA3();
}

void m55m1_debug_uart_init(void)
{
    if (g_debug_uart_initialized)
    {
        return;
    }

    SYS_UnlockReg();
    CLK_EnableXtalRC(CLK_SRCCTL_HIRCEN_Msk);
    CLK_WaitClockReady(CLK_STATUS_HIRCSTB_Msk);
    SetDebugUartMFP();
    CLK_SetModuleClock(DEBUG_PORT_MODULE, DEBUG_PORT_CLKSEL, DEBUG_PORT_CLKDIV);
    CLK_EnableModuleClock(DEBUG_PORT_MODULE);
    SYS_ResetModule(DEBUG_PORT_RST);
    SYS_LockReg();
    InitDebugUart();
    g_debug_uart_initialized = true;
}

void m55m1_debug_uart_enable_tx_irq(void)
{
    NVIC_SetPriority(UART1_IRQn, 3U);
    NVIC_EnableIRQ(UART1_IRQn);
}
