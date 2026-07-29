/**
 * @file m55m1_ethosu_u55.c
 * @brief M55M1 clock, interrupt, and cache ownership for Ethos-U55.
 * @author Bui Minh Huy
 * @date 2026-07-28
 */

#include <stdint.h>

#include "board/m55m1_ethosu_u55.h"
#include "board/m55m1_tlv320dac3100_test.h"
#include "NuMicro.h"

static bool g_board_ready;

bool m55m1_ethosu_u55_board_init(void)
{
    if (g_board_ready)
    {
        return true;
    }

    SYS_UnlockReg();
    CLK_EnableModuleClock(FMC0_MODULE);
    CLK_EnableModuleClock(NPU0_MODULE);
    SYS_ResetModule(SYS_NPURST);
    SYS_LockReg();
    g_board_ready = true;
    return true;
}

void m55m1_ethosu_u55_board_enable_irq(void)
{
    NVIC_SetPriority(NPU_IRQn, 2U);
    NVIC_EnableIRQ(NPU_IRQn);
}

uint32_t m55m1_ethosu_u55_board_cycles_now(void)
{
    return m55m1_tlv320dac3100_test_cycles_now();
}

uint32_t m55m1_ethosu_u55_board_cycles_elapsed(
    uint32_t start_cycle,
    uint32_t end_cycle)
{
    return m55m1_tlv320dac3100_test_cycles_elapsed(start_cycle, end_cycle);
}
