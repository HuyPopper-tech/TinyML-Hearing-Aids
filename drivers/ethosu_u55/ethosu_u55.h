/**
 * @file ethosu_u55.h
 * @brief Project driver for the single M55M1 Ethos-U55 instance.
 * @author Bui Minh Huy
 * @date 2026-07-29
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool ethosu_u55_init(void);

/** @brief Return the free-running board timer used for inference profiling. */
uint32_t ethosu_u55_cycles_now(void);
uint32_t ethosu_u55_cycles_elapsed(uint32_t start_cycle, uint32_t end_cycle);
void ethosu_u55_fault_delay_ms(uint32_t milliseconds);
bool ethosu_u55_deadline_exceeded(uint32_t start_cycle, uint32_t milliseconds);

#ifdef __cplusplus
}
#endif
