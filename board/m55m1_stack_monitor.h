/**
 * @file m55m1_stack_monitor.h
 * @brief Board-owned diagnostic stack bounds and high-water hooks.
 * @author Bui Minh Huy
 * @date 2026-08-06
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void m55m1_stack_monitor_init(void);
uint32_t m55m1_stack_configured_bytes(void);
uint32_t m55m1_stack_high_water_bytes(void);
uint32_t m55m1_stack_headroom_bytes(void);

#ifdef __cplusplus
}
#endif
