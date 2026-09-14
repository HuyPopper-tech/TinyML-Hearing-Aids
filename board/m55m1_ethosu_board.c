/**
 * @file m55m1_ethosu_board.c
 * @brief M55M1 clock, reset, interrupt, and timing binding for Ethos-U55.
 * @author Bui Minh Huy
 * @date 2026-09-14
 */

#include <stdint.h>

#include "board/m55m1_ethosu_board.h"
#include "board/m55m1_timebase.h"
#include "NuMicro.h"

#define M55M1_ETHOSU_IRQ_PRIORITY 2U

static bool g_board_ready;

bool m55m1_ethosu_board_init(void)
{
    if (g_board_ready) {
        return true;
    }

    SYS_UnlockReg();
    CLK_EnableModuleClock(FMC0_MODULE);
    CLK_EnableModuleClock(NPU0_MODULE);
    SYS_ResetModule(SYS_NPURST);
    SYS_LockReg();
    m55m1_timebase_init();
    g_board_ready = true;
    return true;
}

void m55m1_ethosu_board_enable_irq(void)
{
    NVIC_SetPriority(NPU_IRQn, M55M1_ETHOSU_IRQ_PRIORITY);
    NVIC_EnableIRQ(NPU_IRQn);
}

void m55m1_ethosu_board_disable_irq(void)
{
    NVIC_DisableIRQ(NPU_IRQn);
    NVIC_ClearPendingIRQ(NPU_IRQn);
}

const void *m55m1_ethosu_board_get_base_address(void)
{
    return (const void *)(uintptr_t)NPU_BASE;
}

uint32_t m55m1_ethosu_board_cycles_now(void)
{
    return m55m1_timebase_cycles_now();
}

uint32_t m55m1_ethosu_board_cycles_elapsed(
    uint32_t start_cycle,
    uint32_t end_cycle)
{
    return m55m1_timebase_cycles_elapsed(start_cycle, end_cycle);
}