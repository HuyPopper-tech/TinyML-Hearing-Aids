"""Compile the project runtime against temporary mock board/core hooks."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
RUNTIME_SOURCE = ROOT / "drivers" / "ethosu_runtime" / "ethosu_runtime.c"


CORE_HEADER = r'''
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ETHOSU_SUCCESS 0
#define ETHOSU_INVALID_PARAM (-2)

struct ethosu_driver {
    int placeholder;
};
struct ethosu_driver_version {
    uint8_t major;
    uint8_t minor;
    uint8_t patch;
};
struct ethosu_hw_info {
    struct {
        uint32_t version_status;
        uint32_t version_minor;
        uint32_t version_major;
        uint32_t product_major;
        uint32_t arch_patch_rev;
        uint32_t arch_minor_rev;
        uint32_t arch_major_rev;
    } version;
    struct {
        uint32_t macs_per_cc;
        uint32_t cmd_stream_version;
        uint32_t custom_dma;
    } cfg;
};

void ethosu_irq_handler(struct ethosu_driver *driver);
int ethosu_init(struct ethosu_driver *driver, const void *base_address,
                const void *fast_memory, size_t fast_memory_size,
                uint32_t secure_enable, uint32_t privilege_enable);
void ethosu_deinit(struct ethosu_driver *driver);
int ethosu_soft_reset(struct ethosu_driver *driver);
void ethosu_get_driver_version(struct ethosu_driver_version *version);
void ethosu_get_hw_info(struct ethosu_driver *driver,
                        struct ethosu_hw_info *info);
int ethosu_invoke_async(struct ethosu_driver *driver,
                        const void *custom_data_ptr, int custom_data_size,
                        const uint64_t *base_addr,
                        const size_t *base_addr_size, int num_base_addr,
                        void *user_arg);
int ethosu_wait(struct ethosu_driver *driver, bool block);
'''

NU_MICRO_HEADER = r'''
#pragma once
#include <stddef.h>
#include <stdint.h>
extern uint32_t mock_clean_all_calls;
extern uint32_t mock_invalidate_all_calls;
extern uint32_t mock_clean_range_calls;
extern uint32_t mock_invalidate_range_calls;
static inline uint32_t __get_PRIMASK(void) { return 0U; }
static inline void __disable_irq(void) {}
static inline void __set_PRIMASK(uint32_t value) { (void)value; }
static inline void __WFE(void) {}
static inline void __SEV(void) {}
static inline void __DMB(void) {}
static inline void __DSB(void) {}
static inline void __ISB(void) {}
static inline void SCB_CleanDCache(void) { ++mock_clean_all_calls; }
static inline void SCB_InvalidateDCache(void) { ++mock_invalidate_all_calls; }
static inline void SCB_CleanDCache_by_Addr(void *address, int32_t bytes) {
    (void)address;
    (void)bytes;
    ++mock_clean_range_calls;
}
static inline void SCB_InvalidateDCache_by_Addr(void *address, int32_t bytes) {
    (void)address;
    (void)bytes;
    ++mock_invalidate_range_calls;
}
'''

BOARD_MOCK = r'''
#include <stdbool.h>
#include <stdint.h>
#include "board/m55m1_ethosu_board.h"

static uint32_t g_cycles;

bool m55m1_ethosu_board_memory_is_accessible(const void *data, size_t bytes) { return data != NULL && bytes != 0; }
bool m55m1_ethosu_board_init(void) { return true; }
void m55m1_ethosu_board_enable_irq(void) {}
void m55m1_ethosu_board_disable_irq(void) {}
const void *m55m1_ethosu_board_get_base_address(void) { return (const void *)0x1000U; }
uint32_t m55m1_ethosu_board_cycles_now(void) { return ++g_cycles; }
uint32_t m55m1_ethosu_board_cycles_elapsed(uint32_t start, uint32_t end) {
    return end - start;
}
'''

CORE_MOCK = r'''
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "ethosu_driver.h"

static bool g_running;
static bool g_polled;
static bool g_fail_wait;
uint32_t mock_clean_all_calls;
uint32_t mock_invalidate_all_calls;
uint32_t mock_clean_range_calls;
uint32_t mock_invalidate_range_calls;

extern void ethosu_inference_begin(struct ethosu_driver *driver, void *user_arg);
extern void ethosu_inference_end(struct ethosu_driver *driver, void *user_arg);

void mock_set_wait_error(bool enabled) { g_fail_wait = enabled; }

void ethosu_irq_handler(struct ethosu_driver *driver) { (void)driver; }

int ethosu_init(struct ethosu_driver *driver, const void *base_address,
                const void *fast_memory, size_t fast_memory_size,
                uint32_t secure_enable, uint32_t privilege_enable) {
    (void)driver;
    (void)base_address;
    (void)fast_memory;
    (void)fast_memory_size;
    (void)secure_enable;
    (void)privilege_enable;
    g_running = false;
    g_polled = false;
    return ETHOSU_SUCCESS;
}

void ethosu_deinit(struct ethosu_driver *driver) { (void)driver; }
int ethosu_soft_reset(struct ethosu_driver *driver) {
    (void)driver;
    g_running = false;
    g_polled = false;
    g_fail_wait = false;
    return ETHOSU_SUCCESS;
}
void ethosu_get_driver_version(struct ethosu_driver_version *version) {
    version->major = 0U;
    version->minor = 16U;
    version->patch = 0U;
}
void ethosu_get_hw_info(struct ethosu_driver *driver,
                        struct ethosu_hw_info *info) {
    (void)driver;
    info->version.version_status = 1U;
    info->version.version_minor = 0U;
    info->version.version_major = 1U;
    info->version.product_major = 55U;
    info->version.arch_patch_rev = 0U;
    info->version.arch_minor_rev = 0U;
    info->version.arch_major_rev = 1U;
    info->cfg.macs_per_cc = 8U;
    info->cfg.cmd_stream_version = 1U;
    info->cfg.custom_dma = 0U;
}
int ethosu_invoke_async(struct ethosu_driver *driver,
                        const void *custom_data_ptr, int custom_data_size,
                        const uint64_t *base_addr,
                        const size_t *base_addr_size, int num_base_addr,
                        void *user_arg) {
    (void)driver;
    (void)custom_data_ptr;
    (void)custom_data_size;
    (void)base_addr;
    (void)base_addr_size;
    (void)num_base_addr;
    (void)user_arg;
    if (g_running) {
        return -1;
    }
    g_running = true;
    g_polled = false;
    ethosu_inference_begin(driver, user_arg);
    return ETHOSU_SUCCESS;
}
int ethosu_wait(struct ethosu_driver *driver, bool block) {
    (void)driver;
    if (!g_running) {
        return -2;
    }
    if (g_fail_wait) {
        g_running = false;
        ethosu_inference_end(driver, NULL);
        return -1;
    }
    if (!block && !g_polled) {
        g_polled = true;
        return 1;
    }
    g_running = false;
    ethosu_inference_end(driver, NULL);
    return 0;
}
'''

HARNESS = r'''
#include <stdint.h>
#include "drivers/ethosu_runtime/ethosu_runtime.h"

extern void mock_set_wait_error(_Bool enabled);
extern uint32_t mock_clean_range_calls;
extern uint32_t mock_invalidate_range_calls;
extern void ethosu_flush_dcache(uint32_t *address, size_t bytes);
extern void ethosu_invalidate_dcache(uint32_t *address, size_t bytes);

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)

int main(void) {
    static uint8_t payload[16] __attribute__((aligned(16)));
    static uint8_t base_storage[16] __attribute__((aligned(16)));
    static uint8_t immutable_storage[64] __attribute__((aligned(32)));
    static uint8_t mutable_storage[32] __attribute__((aligned(32)));
    uint64_t base_addresses[ETHOSU_RUNTIME_MAX_BASE_ADDRESSES + 1U];
    size_t base_sizes[ETHOSU_RUNTIME_MAX_BASE_ADDRESSES + 1U];
    ethosu_runtime_config_t config = {0};
    ethosu_runtime_invocation_t invocation = {0};
    ethosu_runtime_result_t result = {0};
    ethosu_runtime_stats_t stats = {0};

    CHECK(ethosu_runtime_init(NULL) == ETHOSU_RUNTIME_INVALID_ARGUMENT);
    CHECK(ethosu_runtime_start(NULL) == ETHOSU_RUNTIME_NOT_INITIALIZED);
    config.immutable_memory = immutable_storage;
    CHECK(ethosu_runtime_init(&config) == ETHOSU_RUNTIME_INVALID_ARGUMENT);
    config.immutable_memory_size = sizeof(immutable_storage);
    CHECK(ethosu_runtime_init(&config) == ETHOSU_RUNTIME_OK);
    CHECK(mock_clean_range_calls == 1U);
    ethosu_flush_dcache((uint32_t *)immutable_storage,
                        sizeof(immutable_storage));
    ethosu_invalidate_dcache((uint32_t *)immutable_storage,
                             sizeof(immutable_storage));
    CHECK(mock_clean_range_calls == 1U);
    CHECK(mock_invalidate_range_calls == 0U);
    ethosu_flush_dcache((uint32_t *)mutable_storage, sizeof(mutable_storage));
    ethosu_invalidate_dcache((uint32_t *)mutable_storage,
                             sizeof(mutable_storage));
    CHECK(mock_clean_range_calls == 2U);
    CHECK(mock_invalidate_range_calls == 1U);
    CHECK(ethosu_runtime_init(&config) == ETHOSU_RUNTIME_ALREADY_INITIALIZED);

    invocation.custom_operator_payload = NULL;
    invocation.custom_operator_payload_bytes = 4U;
    CHECK(ethosu_runtime_start(&invocation) == ETHOSU_RUNTIME_INVALID_ARGUMENT);
    invocation.custom_operator_payload = payload;
    invocation.custom_operator_payload_bytes = 0U;
    CHECK(ethosu_runtime_start(&invocation) == ETHOSU_RUNTIME_INVALID_ARGUMENT);
    invocation.custom_operator_payload_bytes = 3U;
    CHECK(ethosu_runtime_start(&invocation) == ETHOSU_RUNTIME_INVALID_ARGUMENT);
    invocation.custom_operator_payload_bytes = 16U;
    invocation.custom_operator_payload = payload + 1U;
    CHECK(ethosu_runtime_start(&invocation) == ETHOSU_RUNTIME_ALIGNMENT_ERROR);
    invocation.custom_operator_payload = payload;
    invocation.base_count = 1U;
    CHECK(ethosu_runtime_start(&invocation) == ETHOSU_RUNTIME_INVALID_ARGUMENT);
    base_addresses[0] = (uint64_t)(uintptr_t)(base_storage + 4U);
    base_sizes[0] = sizeof(base_storage);
    invocation.base_addresses = base_addresses;
    invocation.base_sizes = base_sizes;
    CHECK(ethosu_runtime_start(&invocation) == ETHOSU_RUNTIME_ALIGNMENT_ERROR);
    base_addresses[0] = (uint64_t)(uintptr_t)base_storage;
    for (size_t index = 1U;
         index < ETHOSU_RUNTIME_MAX_BASE_ADDRESSES + 1U; ++index) {
        base_addresses[index] = (uint64_t)(uintptr_t)base_storage;
        base_sizes[index] = sizeof(base_storage);
    }
    invocation.base_count = ETHOSU_RUNTIME_MAX_BASE_ADDRESSES + 1U;
    CHECK(ethosu_runtime_start(&invocation) == ETHOSU_RUNTIME_INVALID_ARGUMENT);
    invocation.base_count = 1U;

    CHECK(ethosu_runtime_invoke(&invocation, NULL) ==
          ETHOSU_RUNTIME_INVALID_ARGUMENT);
    CHECK(ethosu_runtime_get_state() == ETHOSU_RUNTIME_STATE_READY);
    CHECK(ethosu_runtime_start(&invocation) == ETHOSU_RUNTIME_OK);
    CHECK(ethosu_runtime_start(&invocation) == ETHOSU_RUNTIME_BUSY);
    CHECK(ethosu_runtime_deinit() == ETHOSU_RUNTIME_BUSY);
    CHECK(ethosu_runtime_poll(&result) == ETHOSU_RUNTIME_BUSY);
    CHECK(ethosu_runtime_poll(&result) == ETHOSU_RUNTIME_OK);
    CHECK(result.status == ETHOSU_RUNTIME_OK);
    CHECK(ethosu_runtime_get_stats(&stats) == ETHOSU_RUNTIME_OK);
    CHECK(stats.started_count == 1U);
    CHECK(stats.completed_count == 1U);

    CHECK(ethosu_runtime_start(&invocation) == ETHOSU_RUNTIME_OK);
    mock_set_wait_error(1);
    CHECK(ethosu_runtime_wait(&result) == ETHOSU_RUNTIME_HW_FAULT);
    CHECK(ethosu_runtime_get_state() == ETHOSU_RUNTIME_STATE_FAULT);
    CHECK(ethosu_runtime_soft_reset() == ETHOSU_RUNTIME_OK);
    CHECK(ethosu_runtime_get_state() == ETHOSU_RUNTIME_STATE_READY);
    CHECK(ethosu_runtime_deinit() == ETHOSU_RUNTIME_OK);
    CHECK(ethosu_runtime_get_state() == ETHOSU_RUNTIME_STATE_UNINITIALIZED);
    return 0;
}
'''


class EthosURuntimeHostTests(unittest.TestCase):
    def test_runtime_contract_with_mock_core(self):
        compiler = shutil.which("gcc") or shutil.which("clang")
        if compiler is None:
            self.skipTest("no host C compiler available")

        with tempfile.TemporaryDirectory(prefix="ethosu-runtime-") as directory:
            temp = Path(directory)
            (temp / "ethosu_driver.h").write_text(CORE_HEADER, encoding="utf-8")
            (temp / "NuMicro.h").write_text(NU_MICRO_HEADER, encoding="utf-8")
            (temp / "board_mock.c").write_text(BOARD_MOCK, encoding="utf-8")
            (temp / "core_mock.c").write_text(CORE_MOCK, encoding="utf-8")
            (temp / "harness.c").write_text(HARNESS, encoding="utf-8")
            executable = temp / "runtime_contract"
            command = [
                compiler,
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-DNVT_DCACHE_ON=1",
                "-I",
                str(temp),
                "-I",
                str(ROOT),
                str(RUNTIME_SOURCE),
                str(temp / "board_mock.c"),
                str(temp / "core_mock.c"),
                str(temp / "harness.c"),
                "-o",
                str(executable),
            ]
            compile_result = subprocess.run(
                command, capture_output=True, text=True, check=False
            )
            self.assertEqual(
                compile_result.returncode,
                0,
                compile_result.stdout + compile_result.stderr,
            )
            run_result = subprocess.run(
                [str(executable)], capture_output=True, text=True, check=False
            )
            self.assertEqual(
                run_result.returncode,
                0,
                run_result.stdout + run_result.stderr,
            )


if __name__ == "__main__":
    unittest.main()
