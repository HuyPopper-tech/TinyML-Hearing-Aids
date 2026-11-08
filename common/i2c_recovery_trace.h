/**
 * @file i2c_recovery_trace.h
 * @brief Fixed-capacity, portable observations of board I2C bus recovery.
 * @author Bui Minh Huy
 * @date 2026-11-08
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define I2C_RECOVERY_TRACE_CAPACITY 22U
#define I2C_RECOVERY_TRACE_NO_EVENT 255U

typedef enum {
    I2C_RECOVERY_RELEASE = 0,
    I2C_RECOVERY_CLOCK_LOW,
    I2C_RECOVERY_CLOCK_HIGH,
    I2C_RECOVERY_STOP_SDA_LOW,
    I2C_RECOVERY_STOP_SCL_RELEASE,
    I2C_RECOVERY_STOP_SDA_RELEASE,
} i2c_recovery_phase_t;

typedef struct {
    uint32_t mfp, mode, dout, pin, dinoff, pusel, smten;
} i2c_recovery_gpio_t;

typedef struct {
    uint32_t cycle; /**< Wrapping board timebase in core-cycle units. */
    uint8_t phase, pulse, sda, scl;
} i2c_recovery_event_t;

/**
 * @brief Board-owned Debug snapshot, borrowed until the next board init.
 *
 * Foreground writes only while audio is stopped. Consumers read after recovery.
 * Reset flags can be sticky. GPIO inputs are sampled logic, not measured voltage.
 * SysTick CTRL is deliberately excluded: reading it clears COUNTFLAG.
 */
typedef struct {
    uint32_t reset_flags;
    i2c_recovery_gpio_t before, after;
    uint32_t i2c_control, i2c_status, i2c_divider;
    uint32_t gpio_clock, i2c_clock;
    uint32_t core_hz;
    uint32_t systick_load_before, systick_load_after;
    uint32_t systick_source_before, systick_source_after;
    bool attempted, released;
    uint8_t event_count, pulses, first_scl_low_event;
    i2c_recovery_event_t events[I2C_RECOVERY_TRACE_CAPACITY];
} i2c_recovery_trace_t;

#ifdef __cplusplus
}
#endif
