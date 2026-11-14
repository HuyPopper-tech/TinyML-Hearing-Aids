/**
 * @file ethosu_runtime.h
 * @brief Model-neutral single-flight runtime API for the M55M1 Ethos-U55.
 * @author Bui Minh Huy
 * @date 2026-09-14
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ETHOSU_RUNTIME_MAX_BASE_ADDRESSES 8U

typedef enum {
    ETHOSU_RUNTIME_OK = 0,
    ETHOSU_RUNTIME_INVALID_ARGUMENT,
    ETHOSU_RUNTIME_NOT_INITIALIZED,
    ETHOSU_RUNTIME_ALREADY_INITIALIZED,
    ETHOSU_RUNTIME_BUSY,
    ETHOSU_RUNTIME_INVALID_STATE,
    ETHOSU_RUNTIME_ALIGNMENT_ERROR,
    ETHOSU_RUNTIME_CORE_ERROR,
    ETHOSU_RUNTIME_HW_FAULT
} ethosu_runtime_status_t;

typedef enum {
    ETHOSU_RUNTIME_STATE_UNINITIALIZED = 0,
    ETHOSU_RUNTIME_STATE_READY,
    ETHOSU_RUNTIME_STATE_RUNNING,
    ETHOSU_RUNTIME_STATE_FAULT
} ethosu_runtime_state_t;

typedef struct {
    const void *fast_memory;
    size_t fast_memory_size;
    const void *immutable_memory;
    size_t immutable_memory_size;
    bool secure_enable;
    bool privilege_enable;
} ethosu_runtime_config_t;

/**
 * @brief One Vela custom-operator payload and its caller-owned base buffers.
 *
 * The payload and arrays must remain valid and unchanged until wait() returns.
 * base_addresses is writable because the core driver may install its fast
 * memory address at the reserved base index. base_count must not exceed
 * ETHOSU_RUNTIME_MAX_BASE_ADDRESSES.
 */
typedef struct {
    const void *custom_operator_payload;
    size_t custom_operator_payload_bytes;
    uint64_t *base_addresses;
    const size_t *base_sizes;
    size_t base_count;
} ethosu_runtime_invocation_t;

typedef struct {
    ethosu_runtime_status_t status;
    uint32_t duration_cycles;
} ethosu_runtime_result_t;

typedef struct {
    uint32_t version_status;
    uint32_t version_minor;
    uint32_t version_major;
    uint32_t product_major;
    uint32_t arch_patch_rev;
    uint32_t arch_minor_rev;
    uint32_t arch_major_rev;
    uint32_t macs_per_cc;
    uint32_t cmd_stream_version;
    uint32_t custom_dma;
} ethosu_runtime_hw_info_t;

typedef struct {
    uint32_t started_count;
    uint32_t completed_count;
    uint32_t irq_count;
    uint32_t reset_count;
    uint32_t last_duration_cycles;
    int32_t last_core_result;
    ethosu_runtime_state_t state;
} ethosu_runtime_stats_t;

/**
 * @brief Initialize the board binding and singleton core driver.
 * @param config Caller-owned configuration consumed during initialization.
 * @return Runtime status.
 *
 * Foreground-only and must complete before audio starts. A second call while
 * initialized returns ETHOSU_RUNTIME_ALREADY_INITIALIZED.
 */
ethosu_runtime_status_t ethosu_runtime_init(const ethosu_runtime_config_t *config);

/**
 * @brief Deinitialize the singleton driver after it is idle.
 * @return Runtime status.
 *
 * Foreground-only. No invocation may be running.
 */
ethosu_runtime_status_t ethosu_runtime_deinit(void);

/**
 * @brief Read the current runtime lifecycle state.
 * @return State snapshot.
 */
ethosu_runtime_state_t ethosu_runtime_get_state(void);

/**
 * @brief Read the vendored Arm core-driver version.
 * @param major Destination for major version.
 * @param minor Destination for minor version.
 * @param patch Destination for patch version.
 * @return Runtime status.
 */
ethosu_runtime_status_t ethosu_runtime_get_driver_version(uint8_t *major, uint8_t *minor,
                                                          uint8_t *patch);

/**
 * @brief Read hardware identification/configuration after initialization.
 * @param info Destination hardware information.
 * @return Runtime status.
 */
ethosu_runtime_status_t ethosu_runtime_get_hw_info(ethosu_runtime_hw_info_t *info);

/**
 * @brief Start one non-blocking invocation.
 * @param invocation Caller-owned payload and base arrays.
 * @return OK when the NPU job was submitted, otherwise an error status.
 */
ethosu_runtime_status_t ethosu_runtime_start(const ethosu_runtime_invocation_t *invocation);

/**
 * @brief Poll one active invocation without blocking.
 * @param result Destination result structure.
 * @return BUSY while running, OK when complete, or a terminal error.
 */
ethosu_runtime_status_t ethosu_runtime_poll(ethosu_runtime_result_t *result);

/**
 * @brief Wait for one active invocation in the foreground.
 * @param result Destination result structure.
 * @return OK when complete or a terminal error.
 */
ethosu_runtime_status_t ethosu_runtime_wait(ethosu_runtime_result_t *result);

/**
 * @brief Submit and wait for one invocation.
 * @param invocation Caller-owned payload and base arrays.
 * @param result Destination result structure.
 * @return Invocation result.
 */
ethosu_runtime_status_t ethosu_runtime_invoke(const ethosu_runtime_invocation_t *invocation,
                                              ethosu_runtime_result_t *result);

/**
 * @brief Reset an idle or faulted NPU job and return to READY.
 * @return Runtime status.
 */
ethosu_runtime_status_t ethosu_runtime_soft_reset(void);

/**
 * @brief Snapshot model-neutral lifecycle and timing counters.
 * @param stats Destination statistics structure.
 * @return Runtime status.
 */
ethosu_runtime_status_t ethosu_runtime_get_stats(ethosu_runtime_stats_t *stats);

/** Foreground validation of an entire NPU-accessible SRAM range; no ownership transfer. */
bool ethosu_runtime_memory_is_accessible(const void *data, size_t bytes);

#ifdef __cplusplus
}
#endif
