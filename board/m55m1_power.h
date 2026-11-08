/**
 * @file m55m1_power.h
 * @brief Custom product clock policy and continuous-audio shallow idle.
 * @author Bui Minh Huy
 * @date 2026-11-08
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** Apply before peripherals start, with protected registers unlocked.
 * Keeps audio, memory, NPU, watchdogs and SWD clocks/pins untouched.
 */
void m55m1_power_configure(void);

/** Foreground only, after the caller masks IRQs and rechecks pending work.
 * WFI retains peripheral clocks; the caller restores its interrupt mask.
 */
void m55m1_power_idle(void);

#ifdef __cplusplus
}
#endif
