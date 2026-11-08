/**
 * @file m55m1_stack_monitor.c
 * @brief Board-owned diagnostic stack bounds and high-water hooks.
 * @author Bui Minh Huy
 * @date 2026-08-06
 */
#include <stdint.h>

#include "board/m55m1_stack_monitor.h"

#if defined(__ARMCC_VERSION)
#include "NuMicro.h"
extern uint8_t Image$$ARM_LIB_STACK$$ZI$$Base;
extern uint8_t Image$$ARM_LIB_STACK$$ZI$$Limit;
#endif

#define M55M1_STACK_PAINT 0xA5U
#define M55M1_STACK_GUARD_BYTES 64U
static uint8_t *g_stack_base;
static uint8_t *g_stack_limit;
static uint32_t g_stack_high_water;

void m55m1_stack_monitor_init(void) {
#if defined(__ARMCC_VERSION)
    const uintptr_t base = (uintptr_t)&Image$$ARM_LIB_STACK$$ZI$$Base;
    const uintptr_t limit = (uintptr_t)&Image$$ARM_LIB_STACK$$ZI$$Limit;
    const uintptr_t msp = (uintptr_t)__get_MSP();
    g_stack_base = (uint8_t *)base;
    g_stack_limit = (uint8_t *)limit;
    g_stack_high_water = 0U;
    if ((limit > base) && (msp > base + M55M1_STACK_GUARD_BYTES) && (msp <= limit)) {
        // Leave the active stack frame and a guard untouched while painting unused space.
        for (uintptr_t address = base; address < msp - M55M1_STACK_GUARD_BYTES; ++address) {
            *(uint8_t *)address = M55M1_STACK_PAINT;
        }
    }
#else
    g_stack_base = NULL;
    g_stack_limit = NULL;
    g_stack_high_water = 0U;
#endif
}

uint32_t m55m1_stack_configured_bytes(void) {
    return (g_stack_base == NULL || g_stack_limit == NULL)
               ? 0U
               : (uint32_t)(g_stack_limit - g_stack_base);
}

uint32_t m55m1_stack_high_water_bytes(void) {
    if ((g_stack_base == NULL) || (g_stack_limit == NULL)) {
        return 0U;
    }
    uint8_t *cursor = g_stack_base;
    while ((cursor < g_stack_limit) && (*cursor == M55M1_STACK_PAINT)) {
        ++cursor;
    }
    const uint32_t configured = m55m1_stack_configured_bytes();
    const uint32_t untouched = (uint32_t)(cursor - g_stack_base);
    g_stack_high_water = (configured > untouched) ? (configured - untouched) : 0U;
    return g_stack_high_water;
}

uint32_t m55m1_stack_headroom_bytes(void) {
    const uint32_t configured = m55m1_stack_configured_bytes();
    const uint32_t used = m55m1_stack_high_water_bytes();
    return (configured > used) ? (configured - used) : 0U;
}
