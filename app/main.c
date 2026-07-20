/**************************************************************************//**
 * @file    main.c
 * @brief   Hardware test for reusing the J2 ICE pins as UART0.
 *
 * Connect a 3.3 V USB-to-TTL adapter RX input to J2.2 (PF0) and its GND to
 * J2.5. This firmware deliberately leaves PF0/PF1 assigned to UART0; it does
 * not restore SWD/ICE. Disconnect an attached SWD probe while it runs.
 *****************************************************************************/

#include <stdint.h>

#include "NuMicro.h"

static const uint8_t g_au8Banner[] =
    "PF0=UART0_TXD, PF1=UART0_RXD, 115200-8-N-1\r\n";

static void UART0PF01_Init(void)
{
    SYS_UnlockReg();

    CLK_EnableXtalRC(CLK_SRCCTL_HIRCEN_Msk);
    CLK_WaitClockReady(CLK_STATUS_HIRCSTB_Msk);
    CLK_SetModuleClock(UART0_MODULE, CLK_UARTSEL0_UART0SEL_HIRC,
                       CLK_UARTDIV0_UART0DIV(1));
    CLK_EnableModuleClock(UART0_MODULE);
    CLK_EnableModuleClock(GPIOF_MODULE);
    SET_UART0_TXD_PF0();
    SET_UART0_RXD_PF1();

    SYS_LockReg();

    UART_Open(UART0, 115200UL);
}

int main(void)
{
    UART0PF01_Init();

    for (;;)
    {
        (void)UART_Write(UART0, g_au8Banner, sizeof(g_au8Banner) - 1UL);
        UART_WAIT_TX_EMPTY(UART0);
        CLK_SysTickDelay(1000000UL);
    }
}
