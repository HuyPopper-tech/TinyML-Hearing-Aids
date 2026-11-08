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

bool m55m1_ethosu_board_init(void) {
    if (g_board_ready) {
        return true;
    }

    SYS_UnlockReg();
    CLK_EnableModuleClock(FMC0_MODULE);
    CLK_EnableModuleClock(NPU0_MODULE);
    SYS_ResetModule(SYS_NPURST);
    NVIC_DisableIRQ(NPU_IRQn);
    NVIC_ClearPendingIRQ(NPU_IRQn);
    SYS_LockReg();
    m55m1_timebase_init();
    g_board_ready = true;
    return true;
}

void m55m1_ethosu_board_enable_irq(void) {
    NVIC_SetPriority(NPU_IRQn, M55M1_ETHOSU_IRQ_PRIORITY);
    NVIC_EnableIRQ(NPU_IRQn);
}

void m55m1_ethosu_board_disable_irq(void) {
    NVIC_DisableIRQ(NPU_IRQn);
    NVIC_ClearPendingIRQ(NPU_IRQn);
}

const void *m55m1_ethosu_board_get_base_address(void) {
    return (const void *)(uintptr_t)NPU_BASE;
}

uint32_t m55m1_ethosu_board_cycles_now(void) {
    return m55m1_timebase_cycles_now();
}

uint32_t m55m1_ethosu_board_cycles_elapsed(uint32_t start_cycle, uint32_t end_cycle) {
    return m55m1_timebase_cycles_elapsed(start_cycle, end_cycle);
}
bool m55m1_ethosu_board_memory_is_accessible(const void *data, size_t bytes) {
    const uintptr_t begin = (uintptr_t)data;
    return data != NULL && bytes != 0u && begin >= 0x20100000u && begin < 0x20230000u &&
           bytes <= 0x20230000u - begin;
}
