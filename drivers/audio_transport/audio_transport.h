/**
 * @file audio_transport.h
 * @brief Production 16 kHz M55M1 DMIC transport with optional DAC/I2S output.
 * @author Bui Minh Huy
 * @date 2026-07-30
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_TRANSPORT_SAMPLE_RATE_HZ       16000UL
#define AUDIO_TRANSPORT_SAMPLES_PER_BLOCK    16U
#ifndef M55M1_DMIC_GAIN_DB
#define M55M1_DMIC_GAIN_DB 30
#endif
#define AUDIO_TRANSPORT_MICROPHONE_DSP_GAIN_DB M55M1_DMIC_GAIN_DB
#define AUDIO_TRANSPORT_SAFE_MAX_GAIN_DB     (-3)

typedef enum {
    AUDIO_TRANSPORT_STATUS_OK = 0,
    AUDIO_TRANSPORT_STATUS_BUSY,
    AUDIO_TRANSPORT_STATUS_ERROR,
} audio_transport_status_t;

typedef enum {
    AUDIO_TRANSPORT_BOARD_STAGE_ENTER = 0,
    AUDIO_TRANSPORT_BOARD_STAGE_SCLK_HIRC_READY,
    AUDIO_TRANSPORT_BOARD_STAGE_HXT_WAIT,
    AUDIO_TRANSPORT_BOARD_STAGE_HXT_READY,
    AUDIO_TRANSPORT_BOARD_STAGE_APLL1_WAIT,
    AUDIO_TRANSPORT_BOARD_STAGE_APLL1_READY,
    AUDIO_TRANSPORT_BOARD_STAGE_APLL0_WAIT,
    AUDIO_TRANSPORT_BOARD_STAGE_APLL0_READY,
    AUDIO_TRANSPORT_BOARD_STAGE_SCLK_APLL0_READY,
    AUDIO_TRANSPORT_BOARD_STAGE_MODULE_CLOCKS_READY,
    AUDIO_TRANSPORT_BOARD_STAGE_AUDIO_PINS_READY,
    AUDIO_TRANSPORT_BOARD_STAGE_I2C_RECOVERY_READY,
    AUDIO_TRANSPORT_BOARD_STAGE_I2C_PINS_READY,
    AUDIO_TRANSPORT_BOARD_STAGE_I2C_RESET_READY,
    AUDIO_TRANSPORT_BOARD_STAGE_CYCLE_COUNTER_READY,
    AUDIO_TRANSPORT_BOARD_STAGE_PERIPHERALS_READY,
} audio_transport_board_stage_t;

typedef struct {
    uint32_t core_hz;
    uint32_t apll0_hz;
    uint32_t apll1_hz;
    uint32_t i2s_src_hz;
    uint32_t mclk_hz;
    int board_status;
    int codec_status;
    bool clocks_valid;
} audio_transport_init_report_t;

typedef struct {
    uint32_t sequence;
    uint32_t completed_cycle;
} audio_transport_capture_timing_t;

typedef struct {
    uint32_t sequence;
    uint32_t capture_completed_cycle;
} audio_transport_block_tag_t;

typedef struct {
    uint32_t sequence;
    uint32_t capture_completed_cycle;
    uint32_t dac_completed_cycle;
} audio_transport_completion_t;

typedef struct {
    uint32_t dmic_overrun_blocks;
    uint32_t dmic_dma_errors;
    uint32_t dac_underrun_blocks;
    uint32_t dac_dma_errors;
} audio_transport_health_t;

typedef void (*audio_transport_progress_callback_t)(
    audio_transport_board_stage_t stage,
    void *context);

/**
 * @brief Bind the production board and optional codec transport.
 * @param report Optional init result destination.
 * @param progress_callback Optional synchronous foreground observer.
 * @param progress_context Caller-owned observer context retained only during init.
 * @return True when the transport is ready for audio_transport_start().
 *
 * Foreground-only before audio starts. The callback context remains caller-owned
 * and is retained only for the duration of this call.
 */
bool audio_transport_init(
    audio_transport_init_report_t *report,
    audio_transport_progress_callback_t progress_callback,
    void *progress_context);

/**
 * @brief Start DMIC capture and, when enabled, DAC DMA.
 * @return OK when the active lifecycle starts; otherwise BUSY or ERROR.
 *
 * Foreground-only. The transport must be initialized first.
 */
audio_transport_status_t audio_transport_start(void);

/**
 * @brief Quiesce capture and optional playback before terminating DMA ownership.
 * @return None.
 *
 * Foreground-only and idempotent. A new lifecycle requires initialization.
 */
void audio_transport_stop_safe(void);

/**
 * @brief Take a completed DMIC block and timestamp.
 * @param destination Exact-size caller buffer.
 * @param timing Output capture tag.
 * @return True when a completed capture block was copied.
 *
 * Foreground-only; the ISR publishes blocks. Both destinations remain
 * caller-owned.
 */
bool audio_transport_take_capture(
    int16_t destination[AUDIO_TRANSPORT_SAMPLES_PER_BLOCK],
    audio_transport_capture_timing_t *timing);

/**
 * @brief Check for a completed capture block.
 * @return True when a capture block appears available.
 *
 * This best-effort ISR-shared snapshot does not reserve the block.
 */
bool audio_transport_capture_pending(void);

/**
 * @brief Submit one playback block.
 * @param samples Exact-size caller buffer copied before return.
 * @return OK, BUSY, or ERROR.
 *
 * Foreground-only and non-blocking. The caller may reuse samples after return.
 */
audio_transport_status_t audio_transport_submit(
    const int16_t samples[AUDIO_TRANSPORT_SAMPLES_PER_BLOCK]);

/**
 * @brief Submit playback with its prior capture source tag.
 * @param samples Copied block.
 * @param tag Optional caller-owned source metadata copied on success.
 * @return OK, BUSY, or ERROR.
 *
 * Foreground-only and non-blocking. Both inputs remain caller-owned.
 */
audio_transport_status_t audio_transport_submit_tagged(
    const int16_t samples[AUDIO_TRANSPORT_SAMPLES_PER_BLOCK],
    const audio_transport_block_tag_t *tag);

/**
 * @brief Take one DAC completion tag.
 * @param completion Destination.
 * @return True when a completion tag was consumed.
 *
 * Foreground-only. The caller owns the destination storage.
 */
bool audio_transport_take_completion(audio_transport_completion_t *completion);

/**
 * @brief Snapshot transport health.
 * @param health Destination, ignored when NULL.
 * @return None.
 *
 * Foreground-only best-effort snapshot of ISR-updated counters.
 */
void audio_transport_get_health(audio_transport_health_t *health);

/**
 * @brief Read profiling timebase.
 * @return Current cycle count; wrap is expected.
 */
uint32_t audio_transport_cycles_now(void);

/**
 * @brief Calculate a wrap-safe elapsed interval on the shared board timebase.
 * @param start_cycle Earlier captured cycle value.
 * @param end_cycle Later captured cycle value.
 * @return Unsigned elapsed cycles across at most one counter wrap.
 */
uint32_t audio_transport_cycles_elapsed(
    uint32_t start_cycle,
    uint32_t end_cycle);

/**
 * @brief Convert a cycle delta to microseconds.
 * @param cycles Delta.
 * @return Truncated microseconds.
 *
 * Uses the board timebase configured during transport initialization.
 */
uint32_t audio_transport_cycles_to_us(uint32_t cycles);

uint32_t audio_transport_stack_configured_bytes(void);
uint32_t audio_transport_stack_high_water_bytes(void);
uint32_t audio_transport_stack_headroom_bytes(void);

#ifdef __cplusplus
}
#endif
