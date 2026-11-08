/**
 * @file m55m1_audio.h
 * @brief Production M55M1 audio clocks, pins, DMA bindings, and capture queue.
 * @author Bui Minh Huy
 * @date 2026-07-30
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "NuMicro.h"

#ifdef __cplusplus
extern "C" {
#endif

#define M55M1_AUDIO_SAFE_MAX_GAIN_DB (-12)
#ifndef M55M1_DMIC_GAIN_DB
#define M55M1_DMIC_GAIN_DB 30
#endif

typedef enum {
    M55M1_AUDIO_PROFILE_16KHZ = 0,
    M55M1_AUDIO_PROFILE_32KHZ,
} m55m1_audio_profile_t;

typedef enum {
    M55M1_AUDIO_BOARD_INIT_OK = 0,
    M55M1_AUDIO_BOARD_INIT_HXT_TIMEOUT,
    M55M1_AUDIO_BOARD_INIT_APLL1_TIMEOUT,
    M55M1_AUDIO_BOARD_INIT_APLL0_TIMEOUT,
    M55M1_AUDIO_BOARD_INIT_CLOCK_MISMATCH,
    M55M1_AUDIO_BOARD_INIT_I2C_STUCK,
} m55m1_audio_board_init_result_t;

typedef enum {
    M55M1_AUDIO_BOARD_STAGE_ENTER = 0,
    M55M1_AUDIO_BOARD_STAGE_SCLK_HIRC_READY,
    M55M1_AUDIO_BOARD_STAGE_HXT_WAIT,
    M55M1_AUDIO_BOARD_STAGE_HXT_READY,
    M55M1_AUDIO_BOARD_STAGE_APLL1_WAIT,
    M55M1_AUDIO_BOARD_STAGE_APLL1_READY,
    M55M1_AUDIO_BOARD_STAGE_APLL0_WAIT,
    M55M1_AUDIO_BOARD_STAGE_APLL0_READY,
    M55M1_AUDIO_BOARD_STAGE_SCLK_APLL0_READY,
    M55M1_AUDIO_BOARD_STAGE_MODULE_CLOCKS_READY,
    M55M1_AUDIO_BOARD_STAGE_AUDIO_PINS_READY,
    M55M1_AUDIO_BOARD_STAGE_I2C_RECOVERY_READY,
    M55M1_AUDIO_BOARD_STAGE_I2C_PINS_READY,
    M55M1_AUDIO_BOARD_STAGE_I2C_RESET_READY,
    M55M1_AUDIO_BOARD_STAGE_CYCLE_COUNTER_READY,
    M55M1_AUDIO_BOARD_STAGE_PERIPHERALS_READY,
} m55m1_audio_board_stage_t;

typedef struct {
    uint32_t core_hz;
    uint32_t apll0_hz;
    uint32_t apll1_hz;
    uint32_t i2s_src_hz;
    uint32_t mclk_hz;
} m55m1_audio_clock_snapshot_t;

typedef struct {
    I2C_T *i2c;
    I2S_T *i2s;
    PDMA_T *pdma;
    uint8_t pdma_channel;
    int8_t max_digital_gain_db;
} m55m1_audio_playback_binding_t;

typedef struct {
    uint32_t completed_blocks;
    uint32_t overrun_blocks;
    uint32_t dma_errors;
} m55m1_audio_dmic_status_t;

typedef struct {
    uint32_t sequence;
    uint32_t completed_cycle;
} m55m1_audio_dmic_block_timing_t;

typedef void (*m55m1_audio_board_progress_callback_t)(m55m1_audio_board_stage_t stage,
                                                      void *context);

typedef void (*m55m1_audio_playback_irq_callback_t)(uint32_t completed_cycle, void *context);

/**
 * @brief Configure this PCB's clocks, pins, DMA and codec binding.
 * @param profile Requested 16/32 kHz profile.
 * @param binding Output vendor peripheral binding.
 * @param playback_irq_callback ISR callback.
 * @param playback_irq_context Caller-retained callback context.
 * @param progress_callback Optional synchronous foreground observer.
 * @param progress_context Observer context.
 * @return Detailed board-init result.
 *
 * Foreground-only before audio starts; binding/context remain valid for the active lifecycle.
 */
m55m1_audio_board_init_result_t m55m1_audio_board_init(
    m55m1_audio_profile_t profile, m55m1_audio_playback_binding_t *binding,
    m55m1_audio_playback_irq_callback_t playback_irq_callback, void *playback_irq_context,
    m55m1_audio_board_progress_callback_t progress_callback, void *progress_context);

/**
 * @brief Copy validated board clock values.
 * @param snapshot Destination.
 * @return True after board init; false for NULL or an uninitialized board.
 *
 * Foreground-only. The caller owns the destination storage.
 */
bool m55m1_audio_get_clock_snapshot(m55m1_audio_clock_snapshot_t *snapshot);

/**
 * @brief Start double-buffered DMIC DMA.
 * @param profile Active audio profile.
 * @return True when capture begins.
 *
 * Foreground-only. Pair a successful start with m55m1_audio_dmic_stop().
 */
bool m55m1_audio_dmic_start(m55m1_audio_profile_t profile);

/** Configure and clear capture while disabled, then enable beside the TX start.
 * Foreground only. Shared APLL1 gives equal RX/TX sample rates; physical phase
 * and FIFO lead must still be measured on the custom PCB. */
bool m55m1_audio_dmic_prepare(m55m1_audio_profile_t profile);
/**
 * @brief Enable capture immediately beside TX start after successful prepare.
 * @pre Foreground only, descriptors and IRQ are ready and transport is stopped.
 */
void m55m1_audio_dmic_enable(void);

/**
 * @brief Quiesce DMIC capture, LPPDMA requests, and the capture IRQ.
 *
 * Foreground-only. The function is idempotent and leaves no completed block
 * pending. A later capture session must call m55m1_audio_dmic_start().
 */
void m55m1_audio_dmic_stop(void);

/**
 * @brief Copy the completed capture block.
 * @param destination Caller storage.
 * @param sample_count Exact configured count.
 * @return True when a block was consumed.
 *
 * Foreground-only; the DMA ISR remains the producer. The caller retains
 * ownership of destination.
 */
bool m55m1_audio_dmic_take_block(int16_t *destination, uint16_t sample_count);

/**
 * @brief Copy a block and its capture tag.
 * @param destination Caller storage.
 * @param sample_count Exact configured count.
 * @param timing Output sequence/timestamp.
 * @return True when a block and timing tag were consumed.
 *
 * Foreground-only. The caller retains ownership of both output buffers.
 */
bool m55m1_audio_dmic_take_block_timed(int16_t *destination, uint16_t sample_count,
                                       m55m1_audio_dmic_block_timing_t *timing);

/**
 * @brief Check capture availability.
 * @return True when foreground may take a block.
 *
 * This is a best-effort snapshot of ISR-shared state and does not reserve the
 * pending block.
 */
bool m55m1_audio_dmic_block_pending(void);

/**
 * @brief Copy DMIC health counters.
 * @param status Destination, ignored when NULL.
 * @return None.
 *
 * Foreground-only snapshot. The caller owns the destination storage.
 */
void m55m1_audio_dmic_get_status(m55m1_audio_dmic_status_t *status);

#ifdef __cplusplus
}
#endif
