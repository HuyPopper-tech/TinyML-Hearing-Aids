/**
 * @file m55m1_timebase.c
 * @brief Idle-running SysTick timebase, expressed in core-cycle units.
 * @author Bui Minh Huy
 * @date 2026-07-30
 */

#include <stdbool.h>
#include <stdint.h>

#include "board/m55m1_timebase.h"
#include "common/wrapping_counter.h"
#include "NuMicro.h"

#define M55M1_TIMEBASE_SYSTICK_MASK 0x00FFFFFFUL
#define M55M1_TIMEBASE_CYCLE_MASK 0x01FFFFFFUL

static uint32_t g_cycles_per_us = 1U;
static bool g_timebase_ready;

void m55m1_timebase_init(void) {
    if (g_timebase_ready) {
        return;
    }

    g_cycles_per_us = SystemCoreClock / 1000000UL;
    if (g_cycles_per_us == 0U) {
        g_cycles_per_us = 1U;
    }
    const bool registers_locked = (SYS_IsRegLocked() != 0U);
    SYS_UnlockReg();
    CLK_SetSysTickClockSrc(CLK_STSEL_ST0SEL_ACLK_DIV2);
    CLK_EnableModuleClock(ST0_MODULE);
    if (registers_locked) {
        SYS_LockReg();
    }
    SysTick->LOAD = M55M1_TIMEBASE_SYSTICK_MASK;
    SysTick->VAL = 0U;
    /* ACLK runs in Idle; CPU-clock SysTick would hide WFI/WFE elapsed time. */
    SysTick->CTRL = SysTick_CTRL_ENABLE_Msk;
    g_timebase_ready = true;
}

uint32_t m55m1_timebase_cycles_now(void) {
    return (M55M1_TIMEBASE_SYSTICK_MASK - (SysTick->VAL & M55M1_TIMEBASE_SYSTICK_MASK)) << 1U;
}

uint32_t m55m1_timebase_cycles_elapsed(uint32_t start_cycle, uint32_t end_cycle) {
    return wrapping_counter_elapsed(start_cycle, end_cycle, M55M1_TIMEBASE_CYCLE_MASK);
}

uint32_t m55m1_timebase_cycles_to_us(uint32_t cycles) {
    return (cycles & M55M1_TIMEBASE_CYCLE_MASK) / g_cycles_per_us;
}

void m55m1_timebase_delay_us(uint32_t microseconds) {
    m55m1_timebase_init();
    /* Chunks remain below one counter wrap, including the 800 ms codec depop. */
    while (microseconds != 0U) {
        const uint32_t chunk_us = (microseconds > 10000U) ? 10000U : microseconds;
        const uint32_t start = m55m1_timebase_cycles_now();
        while (m55m1_timebase_cycles_elapsed(start, m55m1_timebase_cycles_now()) <
               chunk_us * g_cycles_per_us) {
            __NOP();
        }
        microseconds -= chunk_us;
    }
}
