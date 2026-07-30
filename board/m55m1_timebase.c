/**
 * @file m55m1_timebase.c
 * @brief SysTick-backed production cycle timebase for the M55M1 board.
 * @author Bui Minh Huy
 * @date 2026-07-30
 */

#include <stdbool.h>
#include <stdint.h>

#include "board/m55m1_timebase.h"
#include "common/wrapping_counter.h"
#include "NuMicro.h"

#define M55M1_TIMEBASE_SYSTICK_MASK  0x00FFFFFFUL

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
    SysTick->LOAD = M55M1_TIMEBASE_SYSTICK_MASK;
    SysTick->VAL = 0U;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk;
    g_timebase_ready = true;
}

uint32_t m55m1_timebase_cycles_now(void) {
    return (M55M1_TIMEBASE_SYSTICK_MASK -
            (SysTick->VAL & M55M1_TIMEBASE_SYSTICK_MASK)) &
           M55M1_TIMEBASE_SYSTICK_MASK;
}

uint32_t m55m1_timebase_cycles_elapsed(
    uint32_t start_cycle,
    uint32_t end_cycle) {
    return wrapping_counter_elapsed(
        start_cycle, end_cycle, M55M1_TIMEBASE_SYSTICK_MASK);
}

uint32_t m55m1_timebase_cycles_to_us(uint32_t cycles) {
    return (cycles & M55M1_TIMEBASE_SYSTICK_MASK) / g_cycles_per_us;
}
