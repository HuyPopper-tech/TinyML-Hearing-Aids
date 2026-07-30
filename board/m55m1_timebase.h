/**
 * @file m55m1_timebase.h
 * @brief Board-owned wrap-safe cycle timebase for audio and Ethos-U timing.
 * @author Bui Minh Huy
 * @date 2026-07-30
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void m55m1_timebase_init(void);
uint32_t m55m1_timebase_cycles_now(void);
uint32_t m55m1_timebase_cycles_elapsed(
    uint32_t start_cycle,
    uint32_t end_cycle);
uint32_t m55m1_timebase_cycles_to_us(uint32_t cycles);

#ifdef __cplusplus
}
#endif
