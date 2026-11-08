/**
 * @file m55m1_power.c
 * @brief Disable unused custom-product clocks and retain audio during CPU Idle.
 * @author Bui Minh Huy
 * @date 2026-11-08
 */
#include <stddef.h>
#include <stdint.h>

#include "board/m55m1_power.h"
#include "NuMicro.h"

void m55m1_power_configure(void) {
    /* Named peripheral gates only: do not gate SRAM/FMC/NPU, GPIO or watchdogs. */
    static const uint64_t unused_modules[] = {
        ACMP01_MODULE,  ACMP23_MODULE,  AWF0_MODULE,    BPWM0_MODULE,  BPWM1_MODULE,
        EPWM0_MODULE,   EPWM1_MODULE,   CANFD0_MODULE,  CANFD1_MODULE, CCAP0_MODULE,
        CRC0_MODULE,    CRYPTO0_MODULE, DAC01_MODULE,   EADC0_MODULE,  EBI0_MODULE,
        ECAP0_MODULE,   ECAP1_MODULE,   ECAP2_MODULE,   ECAP3_MODULE,  EQEI0_MODULE,
        EQEI1_MODULE,   EQEI2_MODULE,   EQEI3_MODULE,   EMAC0_MODULE,  GDMA0_MODULE,
        HSOTG0_MODULE,  HSUSBD0_MODULE, HSUSBH0_MODULE, I2C1_MODULE,   I2C2_MODULE,
        I2C3_MODULE,    I2S0_MODULE,    I3C0_MODULE,    KDF0_MODULE,   KPI0_MODULE,
        LPADC0_MODULE,  LPI2C0_MODULE,  LPSPI0_MODULE,  LPTMR0_MODULE, LPTMR1_MODULE,
        LPUART0_MODULE, OTG0_MODULE,    PDMA1_MODULE,   PSIO0_MODULE,  QSPI0_MODULE,
        QSPI1_MODULE,   SC0_MODULE,     SC1_MODULE,     SC2_MODULE,    SDH0_MODULE,
        SDH1_MODULE,    SPI0_MODULE,    SPI1_MODULE,    SPI2_MODULE,   SPI3_MODULE,
        SPIM0_MODULE,   TMR0_MODULE,    TMR1_MODULE,    TMR2_MODULE,   TMR3_MODULE,
        TTMR0_MODULE,   TTMR1_MODULE,   USBD0_MODULE,   USBH0_MODULE,  USCI0_MODULE,
        UTCPD0_MODULE,  UART0_MODULE,   UART1_MODULE,   UART3_MODULE,  UART4_MODULE,
        UART5_MODULE,   UART6_MODULE,   UART7_MODULE,   UART8_MODULE,  UART9_MODULE,
#if !M55M1_DEBUG_UART_ENABLED
        UART2_MODULE,
#endif
    };
    static const IRQn_Type unused_uart_irqs[] = {
        UART0_IRQn, UART1_IRQn, UART3_IRQn, UART4_IRQn, UART5_IRQn,
        UART6_IRQn, UART7_IRQn, UART8_IRQn, UART9_IRQn, LPUART0_IRQn,
#if !M55M1_DEBUG_UART_ENABLED
        UART2_IRQn,
#endif
    };

    for (size_t i = 0U; i < sizeof(unused_uart_irqs) / sizeof(unused_uart_irqs[0]); ++i) {
        NVIC_DisableIRQ(unused_uart_irqs[i]);
        NVIC_ClearPendingIRQ(unused_uart_irqs[i]);
    }
    for (size_t i = 0U; i < sizeof(unused_modules) / sizeof(unused_modules[0]); ++i) {
        CLK_DisableModuleClock(unused_modules[i]);
    }
    /* Deep sleep would stop the two audio/core PLLs; no sleep-on-ISR-return. */
    SCB->SCR &= ~(SCB_SCR_SLEEPDEEP_Msk | SCB_SCR_SLEEPONEXIT_Msk);
}

void m55m1_power_idle(void) {
    SCB->SCR &= ~(SCB_SCR_SLEEPDEEP_Msk | SCB_SCR_SLEEPONEXIT_Msk);
    __DSB();
    __WFI();
    __ISB();
}
