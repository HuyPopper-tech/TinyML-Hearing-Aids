/**
 * @file test_ethosu_runtime_smoke_main.c
 * @brief Standalone board and lifecycle smoke image for Ethos-U55.
 * @author Bui Minh Huy
 * @date 2026-09-14
 *
 * This image has no audio pipeline, model package, or command stream. It only
 * proves board binding, runtime health queries, validation, reset, and teardown.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "drivers/debug_uart_tx/debug_uart_tx.h"
#include "drivers/ethosu_runtime/ethosu_runtime.h"

static void ethosu_smoke_report(const char *message)
{
    if (message != NULL) {
        (void)debug_uart_tx_try_enqueue(message, strlen(message));
        debug_uart_tx_flush_blocking();
    }
}

static void ethosu_smoke_halt(const char *message)
{
    ethosu_smoke_report(message);
    for (;;) {
    }
}

int main(void)
{
    const ethosu_runtime_config_t config = {
        NULL,
        0U,
        true,
        true
    };
    ethosu_runtime_hw_info_t hw_info = {0};
    uint8_t major = 0U;
    uint8_t minor = 0U;
    uint8_t patch = 0U;
    ethosu_runtime_status_t status;

    debug_uart_tx_init();
    ethosu_smoke_report("ETHOSU SMOKE START\r\n");

    status = ethosu_runtime_init(&config);
    if (status != ETHOSU_RUNTIME_OK) {
        ethosu_smoke_halt("ETHOSU INIT FAIL\r\n");
    }
    ethosu_smoke_report("ETHOSU INIT OK\r\n");

    status = ethosu_runtime_get_driver_version(&major, &minor, &patch);
    if (status != ETHOSU_RUNTIME_OK) {
        ethosu_smoke_halt("ETHOSU VERSION FAIL\r\n");
    }
    (void)major;
    (void)minor;
    (void)patch;
    ethosu_smoke_report("ETHOSU VERSION OK\r\n");

    status = ethosu_runtime_get_hw_info(&hw_info);
    if (status != ETHOSU_RUNTIME_OK) {
        ethosu_smoke_halt("ETHOSU HW INFO FAIL\r\n");
    }
    (void)hw_info;
    ethosu_smoke_report("ETHOSU HW INFO OK\r\n");

    status = ethosu_runtime_start(NULL);
    if (status != ETHOSU_RUNTIME_INVALID_ARGUMENT) {
        ethosu_smoke_halt("ETHOSU VALIDATION FAIL\r\n");
    }
    ethosu_smoke_report("ETHOSU VALIDATION OK\r\n");

    status = ethosu_runtime_soft_reset();
    if (status != ETHOSU_RUNTIME_OK) {
        ethosu_smoke_halt("ETHOSU RESET FAIL\r\n");
    }
    ethosu_smoke_report("ETHOSU RESET OK\r\n");

    status = ethosu_runtime_deinit();
    if (status != ETHOSU_RUNTIME_OK) {
        ethosu_smoke_halt("ETHOSU DEINIT FAIL\r\n");
    }
    ethosu_smoke_report("ETHOSU SMOKE PASS\r\n");

    for (;;) {
    }
}