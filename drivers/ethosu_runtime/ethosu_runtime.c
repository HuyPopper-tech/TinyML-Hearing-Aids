/**
 * @file ethosu_runtime.c
 * @brief Model-neutral M55M1 Ethos-U55 lifecycle and board adapter.
 * @author Bui Minh Huy
 * @date 2026-09-14
 */

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#include "board/m55m1_ethosu_board.h"
#include "drivers/ethosu_runtime/ethosu_runtime.h"
#include "NuMicro.h"
#include <ethosu_driver.h>

#ifndef NVT_DCACHE_ON
#define NVT_DCACHE_ON 1
#endif

#ifndef DCACHE_LINE_SIZE
#define DCACHE_LINE_SIZE 32U
#endif

#define ETHOSU_RUNTIME_ALIGNMENT 16U
#define ETHOSU_RUNTIME_PAYLOAD_ALIGNMENT 4U

typedef struct {
    volatile uint32_t count;
} ethosu_runtime_semaphore_t;

typedef struct {
    volatile uint32_t locked;
} ethosu_runtime_mutex_t;

static struct ethosu_driver g_ethosu_driver;
static ethosu_runtime_state_t g_runtime_state =
    ETHOSU_RUNTIME_STATE_UNINITIALIZED;
static ethosu_runtime_stats_t g_runtime_stats;
static bool g_core_initialized;
static uint32_t g_inference_start_cycle;
static ethosu_runtime_semaphore_t g_global_semaphore;
static ethosu_runtime_semaphore_t g_driver_semaphore;
static ethosu_runtime_mutex_t g_runtime_mutex;
static bool g_global_semaphore_created;
static bool g_driver_semaphore_created;

static uint32_t ethosu_runtime_enter_critical(void)
{
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

static void ethosu_runtime_exit_critical(uint32_t primask)
{
    __set_PRIMASK(primask);
}

static bool ethosu_runtime_is_aligned(
    const void *address,
    uintptr_t alignment)
{
    return ((uintptr_t)address % alignment) == 0U;
}

static bool ethosu_runtime_config_is_valid(
    const ethosu_runtime_config_t *config)
{
    if (config == NULL) {
        return false;
    }
    if ((config->fast_memory == NULL) != (config->fast_memory_size == 0U)) {
        return false;
    }
    if ((config->fast_memory != NULL) &&
        !ethosu_runtime_is_aligned(config->fast_memory,
                                      ETHOSU_RUNTIME_ALIGNMENT)) {
        return false;
    }
    return true;
}

static ethosu_runtime_status_t ethosu_runtime_validate_invocation(
    const ethosu_runtime_invocation_t *invocation)
{
    size_t index;

    if ((invocation == NULL) ||
        (invocation->custom_operator_payload == NULL) ||
        (invocation->custom_operator_payload_bytes == 0U) ||
        (invocation->custom_operator_payload_bytes > (size_t)INT_MAX)) {
        return ETHOSU_RUNTIME_INVALID_ARGUMENT;
    }
    if ((invocation->custom_operator_payload_bytes % sizeof(uint32_t)) != 0U) {
        return ETHOSU_RUNTIME_INVALID_ARGUMENT;
    }
    if (!ethosu_runtime_is_aligned(
            invocation->custom_operator_payload,
            ETHOSU_RUNTIME_PAYLOAD_ALIGNMENT)) {
        return ETHOSU_RUNTIME_ALIGNMENT_ERROR;
    }
    if (invocation->base_count > (size_t)INT_MAX) {
        return ETHOSU_RUNTIME_INVALID_ARGUMENT;
    }
    if (invocation->base_count == 0U) {
        return ETHOSU_RUNTIME_OK;
    }
    if ((invocation->base_addresses == NULL) ||
        (invocation->base_sizes == NULL)) {
        return ETHOSU_RUNTIME_INVALID_ARGUMENT;
    }

    for (index = 0U; index < invocation->base_count; ++index) {
        if (invocation->base_addresses[index] == 0U) {
            return ETHOSU_RUNTIME_INVALID_ARGUMENT;
        }
        if ((invocation->base_addresses[index] % ETHOSU_RUNTIME_ALIGNMENT) !=
            0U) {
            return ETHOSU_RUNTIME_ALIGNMENT_ERROR;
        }
    }
    return ETHOSU_RUNTIME_OK;
}

static ethosu_runtime_status_t ethosu_runtime_map_core_result(int result)
{
    if (result == ETHOSU_SUCCESS) {
        return ETHOSU_RUNTIME_OK;
    }
    if (result == ETHOSU_INVALID_PARAM) {
        return ETHOSU_RUNTIME_INVALID_ARGUMENT;
    }
    if (result == 1) {
        return ETHOSU_RUNTIME_BUSY;
    }
    return ETHOSU_RUNTIME_HW_FAULT;
}

static void ethosu_runtime_finish(
    int core_result,
    ethosu_runtime_result_t *result)
{
    const ethosu_runtime_status_t status =
        ethosu_runtime_map_core_result(core_result);

    g_runtime_stats.last_core_result = core_result;
    if (status == ETHOSU_RUNTIME_OK) {
        g_runtime_state = ETHOSU_RUNTIME_STATE_READY;
        result->status = ETHOSU_RUNTIME_OK;
        result->duration_cycles = g_runtime_stats.last_duration_cycles;
    } else {
        g_runtime_state = ETHOSU_RUNTIME_STATE_FAULT;
        result->status = status;
        result->duration_cycles = 0U;
    }
}

void *ethosu_mutex_create(void)
{
    g_runtime_mutex.locked = 0U;
    return &g_runtime_mutex;
}

void ethosu_mutex_destroy(void *mutex)
{
    (void)mutex;
}

int ethosu_mutex_lock(void *mutex)
{
    ethosu_runtime_mutex_t *value = (ethosu_runtime_mutex_t *)mutex;

    if (value == NULL) {
        return -1;
    }
    for (;;) {
        const uint32_t primask = ethosu_runtime_enter_critical();
        if (value->locked == 0U) {
            value->locked = 1U;
            ethosu_runtime_exit_critical(primask);
            return 0;
        }
        ethosu_runtime_exit_critical(primask);
        __WFE();
    }
}

int ethosu_mutex_unlock(void *mutex)
{
    ethosu_runtime_mutex_t *value = (ethosu_runtime_mutex_t *)mutex;

    if (value == NULL) {
        return -1;
    }
    value->locked = 0U;
    __SEV();
    return 0;
}

void *ethosu_semaphore_create(void)
{
    if (!g_global_semaphore_created) {
        g_global_semaphore.count = 0U;
        g_global_semaphore_created = true;
        return &g_global_semaphore;
    }
    if (!g_driver_semaphore_created) {
        g_driver_semaphore.count = 0U;
        g_driver_semaphore_created = true;
        return &g_driver_semaphore;
    }
    return NULL;
}

void ethosu_semaphore_destroy(void *semaphore)
{
    if (semaphore == &g_driver_semaphore) {
        g_driver_semaphore.count = 0U;
        g_driver_semaphore_created = false;
    }
}

int ethosu_semaphore_take(void *semaphore)
{
    ethosu_runtime_semaphore_t *value =
        (ethosu_runtime_semaphore_t *)semaphore;

    if (value == NULL) {
        return -1;
    }
    for (;;) {
        const uint32_t primask = ethosu_runtime_enter_critical();
        if (value->count != 0U) {
            value->count = 0U;
            ethosu_runtime_exit_critical(primask);
            return 0;
        }
        ethosu_runtime_exit_critical(primask);
        __WFE();
    }
}

int ethosu_semaphore_give(void *semaphore)
{
    ethosu_runtime_semaphore_t *value =
        (ethosu_runtime_semaphore_t *)semaphore;

    if (value == NULL) {
        return -1;
    }
    value->count = 1U;
    __SEV();
    return 0;
}

int ethosu_semaphore_give_from_ISR(void *semaphore)
{
    ethosu_runtime_semaphore_t *value =
        (ethosu_runtime_semaphore_t *)semaphore;

    if (value == NULL) {
        return -1;
    }
    __DMB();
    value->count = 1U;
    ++g_runtime_stats.irq_count;
    __SEV();
    return 0;
}

static void ethosu_runtime_cache_maintain(
    uint32_t *address,
    size_t bytes,
    bool clean)
{
#if (NVT_DCACHE_ON == 0)
    (void)address;
    (void)bytes;
    (void)clean;
    __DMB();
#else
    uintptr_t start;
    uintptr_t aligned_start;
    uintptr_t span;
    size_t aligned_bytes;

    if ((address == NULL) || (bytes == 0U)) {
        if (clean) {
            SCB_CleanDCache();
        } else {
            SCB_InvalidateDCache();
        }
        return;
    }

    start = (uintptr_t)address;
    aligned_start = start & ~((uintptr_t)DCACHE_LINE_SIZE - 1U);
    span = start - aligned_start;
    if ((bytes > UINTPTR_MAX - start) ||
        (bytes > UINTPTR_MAX - span)) {
        if (clean) {
            SCB_CleanDCache();
        } else {
            SCB_InvalidateDCache();
        }
        return;
    }
    span += bytes;
    if ((span > (uintptr_t)INT32_MAX) ||
        (span > UINTPTR_MAX - ((uintptr_t)DCACHE_LINE_SIZE - 1U))) {
        if (clean) {
            SCB_CleanDCache();
        } else {
            SCB_InvalidateDCache();
        }
        return;
    }
    aligned_bytes = (size_t)((span + DCACHE_LINE_SIZE - 1U) &
                             ~((uintptr_t)DCACHE_LINE_SIZE - 1U));
    if (aligned_bytes > (size_t)INT32_MAX) {
        if (clean) {
            SCB_CleanDCache();
        } else {
            SCB_InvalidateDCache();
        }
        return;
    }
    if (clean) {
        SCB_CleanDCache_by_Addr(
            (void *)aligned_start, (int32_t)aligned_bytes);
    } else {
        SCB_InvalidateDCache_by_Addr(
            (void *)aligned_start, (int32_t)aligned_bytes);
    }
    __DMB();
#endif
}

void ethosu_flush_dcache(uint32_t *address, size_t bytes)
{
    ethosu_runtime_cache_maintain(address, bytes, true);
}

void ethosu_invalidate_dcache(uint32_t *address, size_t bytes)
{
    ethosu_runtime_cache_maintain(address, bytes, false);
}

void ethosu_inference_begin(struct ethosu_driver *driver, void *user_arg)
{
    (void)driver;
    (void)user_arg;
    g_inference_start_cycle = m55m1_ethosu_board_cycles_now();
    ++g_runtime_stats.started_count;
}

void ethosu_inference_end(struct ethosu_driver *driver, void *user_arg)
{
    (void)driver;
    (void)user_arg;
    g_runtime_stats.last_duration_cycles =
        m55m1_ethosu_board_cycles_elapsed(
            g_inference_start_cycle,
            m55m1_ethosu_board_cycles_now());
    ++g_runtime_stats.completed_count;
}

void NPU_IRQHandler(void)
{
    ethosu_irq_handler(&g_ethosu_driver);
    __DSB();
    __ISB();
}

ethosu_runtime_status_t ethosu_runtime_init(
    const ethosu_runtime_config_t *config)
{
    int result;

    if (!ethosu_runtime_config_is_valid(config)) {
        return ETHOSU_RUNTIME_INVALID_ARGUMENT;
    }
    if (g_runtime_state != ETHOSU_RUNTIME_STATE_UNINITIALIZED) {
        return ETHOSU_RUNTIME_ALREADY_INITIALIZED;
    }
    if (!m55m1_ethosu_board_init()) {
        return ETHOSU_RUNTIME_CORE_ERROR;
    }

    result = ethosu_init(
        &g_ethosu_driver,
        m55m1_ethosu_board_get_base_address(),
        config->fast_memory,
        config->fast_memory_size,
        config->secure_enable ? 1U : 0U,
        config->privilege_enable ? 1U : 0U);
    if (result != ETHOSU_SUCCESS) {
        m55m1_ethosu_board_disable_irq();
        g_runtime_state = ETHOSU_RUNTIME_STATE_UNINITIALIZED;
        g_runtime_stats.last_core_result = result;
        return ETHOSU_RUNTIME_CORE_ERROR;
    }

    g_core_initialized = true;
    g_runtime_stats = (ethosu_runtime_stats_t){0};
    g_runtime_stats.state = ETHOSU_RUNTIME_STATE_READY;
    g_runtime_state = ETHOSU_RUNTIME_STATE_READY;
    m55m1_ethosu_board_enable_irq();
    return ETHOSU_RUNTIME_OK;
}

ethosu_runtime_status_t ethosu_runtime_deinit(void)
{
    if (g_runtime_state == ETHOSU_RUNTIME_STATE_UNINITIALIZED) {
        return ETHOSU_RUNTIME_NOT_INITIALIZED;
    }
    if (g_runtime_state == ETHOSU_RUNTIME_STATE_RUNNING) {
        return ETHOSU_RUNTIME_BUSY;
    }

    m55m1_ethosu_board_disable_irq();
    if (g_core_initialized) {
        ethosu_deinit(&g_ethosu_driver);
        g_core_initialized = false;
    }
    g_driver_semaphore_created = false;
    g_runtime_state = ETHOSU_RUNTIME_STATE_UNINITIALIZED;
    g_runtime_stats.state = ETHOSU_RUNTIME_STATE_UNINITIALIZED;
    g_ethosu_driver = (struct ethosu_driver){0};
    return ETHOSU_RUNTIME_OK;
}

ethosu_runtime_state_t ethosu_runtime_get_state(void)
{
    return g_runtime_state;
}

ethosu_runtime_status_t ethosu_runtime_get_driver_version(
    uint8_t *major,
    uint8_t *minor,
    uint8_t *patch)
{
    struct ethosu_driver_version version;

    if ((major == NULL) || (minor == NULL) || (patch == NULL)) {
        return ETHOSU_RUNTIME_INVALID_ARGUMENT;
    }
    ethosu_get_driver_version(&version);
    *major = version.major;
    *minor = version.minor;
    *patch = version.patch;
    return ETHOSU_RUNTIME_OK;
}

ethosu_runtime_status_t ethosu_runtime_get_hw_info(
    ethosu_runtime_hw_info_t *info)
{
    struct ethosu_hw_info hardware;

    if (info == NULL) {
        return ETHOSU_RUNTIME_INVALID_ARGUMENT;
    }
    if (g_runtime_state == ETHOSU_RUNTIME_STATE_UNINITIALIZED) {
        return ETHOSU_RUNTIME_NOT_INITIALIZED;
    }
    ethosu_get_hw_info(&g_ethosu_driver, &hardware);
    info->version_status = hardware.version.version_status;
    info->version_minor = hardware.version.version_minor;
    info->version_major = hardware.version.version_major;
    info->product_major = hardware.version.product_major;
    info->arch_patch_rev = hardware.version.arch_patch_rev;
    info->arch_minor_rev = hardware.version.arch_minor_rev;
    info->arch_major_rev = hardware.version.arch_major_rev;
    info->macs_per_cc = hardware.cfg.macs_per_cc;
    info->cmd_stream_version = hardware.cfg.cmd_stream_version;
    info->custom_dma = hardware.cfg.custom_dma;
    return ETHOSU_RUNTIME_OK;
}

ethosu_runtime_status_t ethosu_runtime_start(
    const ethosu_runtime_invocation_t *invocation)
{
    ethosu_runtime_status_t validation;
    int result;

    if (g_runtime_state == ETHOSU_RUNTIME_STATE_UNINITIALIZED) {
        return ETHOSU_RUNTIME_NOT_INITIALIZED;
    }
    if (g_runtime_state == ETHOSU_RUNTIME_STATE_RUNNING) {
        return ETHOSU_RUNTIME_BUSY;
    }
    if (g_runtime_state == ETHOSU_RUNTIME_STATE_FAULT) {
        return ETHOSU_RUNTIME_INVALID_STATE;
    }

    validation = ethosu_runtime_validate_invocation(invocation);
    if (validation != ETHOSU_RUNTIME_OK) {
        return validation;
    }

    result = ethosu_invoke_async(
        &g_ethosu_driver,
        invocation->custom_operator_payload,
        (int)invocation->custom_operator_payload_bytes,
        invocation->base_addresses,
        invocation->base_sizes,
        (int)invocation->base_count,
        NULL);
    g_runtime_stats.last_core_result = result;
    if (result != ETHOSU_SUCCESS) {
        g_runtime_state = ETHOSU_RUNTIME_STATE_FAULT;
        g_runtime_stats.state = ETHOSU_RUNTIME_STATE_FAULT;
        return ethosu_runtime_map_core_result(result);
    }
    g_runtime_state = ETHOSU_RUNTIME_STATE_RUNNING;
    g_runtime_stats.state = ETHOSU_RUNTIME_STATE_RUNNING;
    return ETHOSU_RUNTIME_OK;
}

static ethosu_runtime_status_t ethosu_runtime_wait_internal(
    ethosu_runtime_result_t *result,
    bool block)
{
    int core_result;

    if (result == NULL) {
        return ETHOSU_RUNTIME_INVALID_ARGUMENT;
    }
    if (g_runtime_state == ETHOSU_RUNTIME_STATE_UNINITIALIZED) {
        return ETHOSU_RUNTIME_NOT_INITIALIZED;
    }
    if (g_runtime_state != ETHOSU_RUNTIME_STATE_RUNNING) {
        return ETHOSU_RUNTIME_INVALID_STATE;
    }

    core_result = ethosu_wait(&g_ethosu_driver, block);
    if ((!block) && (core_result == 1)) {
        result->status = ETHOSU_RUNTIME_BUSY;
        result->duration_cycles = 0U;
        return ETHOSU_RUNTIME_BUSY;
    }
    ethosu_runtime_finish(core_result, result);
    g_runtime_stats.state = g_runtime_state;
    return result->status;
}

ethosu_runtime_status_t ethosu_runtime_poll(
    ethosu_runtime_result_t *result)
{
    return ethosu_runtime_wait_internal(result, false);
}

ethosu_runtime_status_t ethosu_runtime_wait(
    ethosu_runtime_result_t *result)
{
    return ethosu_runtime_wait_internal(result, true);
}

ethosu_runtime_status_t ethosu_runtime_invoke(
    const ethosu_runtime_invocation_t *invocation,
    ethosu_runtime_result_t *result)
{
    ethosu_runtime_status_t status;
    if (result == NULL) {
        return ETHOSU_RUNTIME_INVALID_ARGUMENT;
    }
    status = ethosu_runtime_start(invocation);

    if (status != ETHOSU_RUNTIME_OK) {
        result->status = status;
        result->duration_cycles = 0U;
        return status;
    }
    return ethosu_runtime_wait(result);
}

ethosu_runtime_status_t ethosu_runtime_soft_reset(void)
{
    int result;

    if (g_runtime_state == ETHOSU_RUNTIME_STATE_UNINITIALIZED) {
        return ETHOSU_RUNTIME_NOT_INITIALIZED;
    }
    if (g_runtime_state == ETHOSU_RUNTIME_STATE_RUNNING) {
        return ETHOSU_RUNTIME_BUSY;
    }
    result = ethosu_soft_reset(&g_ethosu_driver);
    g_runtime_stats.last_core_result = result;
    if (result != ETHOSU_SUCCESS) {
        g_runtime_state = ETHOSU_RUNTIME_STATE_FAULT;
        g_runtime_stats.state = ETHOSU_RUNTIME_STATE_FAULT;
        return ETHOSU_RUNTIME_HW_FAULT;
    }
    ++g_runtime_stats.reset_count;
    g_runtime_state = ETHOSU_RUNTIME_STATE_READY;
    g_runtime_stats.state = ETHOSU_RUNTIME_STATE_READY;
    return ETHOSU_RUNTIME_OK;
}

ethosu_runtime_status_t ethosu_runtime_get_stats(
    ethosu_runtime_stats_t *stats)
{
    if (stats == NULL) {
        return ETHOSU_RUNTIME_INVALID_ARGUMENT;
    }
    if (g_runtime_state == ETHOSU_RUNTIME_STATE_UNINITIALIZED) {
        return ETHOSU_RUNTIME_NOT_INITIALIZED;
    }
    *stats = g_runtime_stats;
    stats->state = g_runtime_state;
    return ETHOSU_RUNTIME_OK;
}