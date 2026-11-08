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

/**
 * @brief Paint unused stack storage before the observation interval.
 * @pre Foreground boot only; never repaint an active audio session.
 */
void m55m1_stack_monitor_init(void);
/**
 * @brief Configured stack capacity.
 * @return Bytes, or zero when stack instrumentation is unavailable.
 * @note Foreground-only observation; not a proof of worst-case stack usage.
 */
uint32_t m55m1_stack_configured_bytes(void);
/**
 * @brief Estimated painted-stack high-water usage.
 * @return Bytes, or zero when stack instrumentation is unavailable.
 * @note Foreground-only observation; not a proof of worst-case stack usage.
 */
uint32_t m55m1_stack_high_water_bytes(void);
/**
 * @brief Estimated remaining painted-stack capacity.
 * @return Bytes, or zero when stack instrumentation is unavailable.
 * @note Foreground-only observation; not a proof of worst-case stack usage.
 */
uint32_t m55m1_stack_headroom_bytes(void);

#ifdef __cplusplus
}
#endif
