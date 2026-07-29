/**
 * @file m55m1_tlv320dac3100_test.h
 * @brief NuMaker M55M1 test-board binding for TLV320DAC3100 playback.
 * @author Bui Minh Huy
 * @date 2026-07-20
 */

#pragma once

#include "drivers/tlv320dac3100/tlv320dac3100.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Bring-up DAC ceiling; validate headphone SPL before increasing above -3 dB. */
#define M55M1_TLV320DAC3100_SAFE_MAX_GAIN_DB  (-3)

/**
 * @brief Outcome of the board-owned external-clock and peripheral binding.
 *
 * `OK` means the HXT-derived core/audio PLLs, audio pins, peripheral clocks, and the
 * generic-driver configuration are ready. Timeout values leave `config`
 * uninitialised; callers must display a diagnostic or stop, not call the
 * generic driver with it.
 */
typedef enum {
    M55M1_TLV320DAC3100_BOARD_INIT_OK = 0,
    M55M1_TLV320DAC3100_BOARD_INIT_HXT_TIMEOUT,
    M55M1_TLV320DAC3100_BOARD_INIT_APLL1_TIMEOUT,
    M55M1_TLV320DAC3100_BOARD_INIT_APLL0_TIMEOUT,
    M55M1_TLV320DAC3100_BOARD_INIT_CLOCK_MISMATCH,
    /** PB0 remained low after GPIO I2C recovery; do not call the codec driver. */
    M55M1_TLV320DAC3100_BOARD_INIT_I2C_STUCK,
} m55m1_tlv320dac3100_board_init_result_t;

/**
 * @brief Foreground-observable boundaries inside the board audio setup.
 *
 * A stage callback runs synchronously in the caller's foreground context.
 * It is intended for bounded diagnostics such as UART logging before PDMA is
 * enabled; it must not submit PCM, change the board clock, or block for a
 * duration that could matter after playback has started.
 */
typedef enum {
    M55M1_TLV320DAC3100_BOARD_STAGE_ENTER = 0,
    M55M1_TLV320DAC3100_BOARD_STAGE_SCLK_HIRC_READY,
    M55M1_TLV320DAC3100_BOARD_STAGE_HXT_WAIT,
    M55M1_TLV320DAC3100_BOARD_STAGE_HXT_READY,
    M55M1_TLV320DAC3100_BOARD_STAGE_APLL1_WAIT,
    M55M1_TLV320DAC3100_BOARD_STAGE_APLL1_READY,
    M55M1_TLV320DAC3100_BOARD_STAGE_APLL0_WAIT,
    M55M1_TLV320DAC3100_BOARD_STAGE_APLL0_READY,
    M55M1_TLV320DAC3100_BOARD_STAGE_SCLK_APLL0_READY,
    M55M1_TLV320DAC3100_BOARD_STAGE_MODULE_CLOCKS_READY,
    M55M1_TLV320DAC3100_BOARD_STAGE_AUDIO_PINS_READY,
    M55M1_TLV320DAC3100_BOARD_STAGE_I2C_RECOVERY_READY,
    M55M1_TLV320DAC3100_BOARD_STAGE_I2C_PINS_READY,
    M55M1_TLV320DAC3100_BOARD_STAGE_I2C_RESET_READY,
    M55M1_TLV320DAC3100_BOARD_STAGE_CYCLE_COUNTER_READY,
    M55M1_TLV320DAC3100_BOARD_STAGE_PERIPHERALS_READY,
} m55m1_tlv320dac3100_board_stage_t;

/** Board-clock values measured after the profile clock tree is committed. */
typedef struct {
    uint32_t core_hz;
    uint32_t apll0_hz;
    uint32_t apll1_hz;
    uint32_t i2s_src_hz;
    uint32_t mclk_hz;
} m55m1_tlv320dac3100_clock_snapshot_t;

/**
 * @brief Snapshot of DMIC capture health counters.
 *
 * All fields are monotonically increasing from `m55m1_tlv320dac3100_test_dmic_start`
 * until the next call to that function. The LPPDMA IRQ owns the counters; callers
 * receive a coherent foreground snapshot through the getter below.
 */
typedef struct {
    /** Number of 10 ms LPPDMA microphone blocks completed. */
    uint32_t completed_blocks;
    /** Number of completions that arrived before foreground consumed the prior block. */
    uint32_t overrun_blocks;
    /** Number of LPPDMA abort conditions observed for the DMIC channel. */
    uint32_t dma_errors;
} m55m1_tlv320dac3100_dmic_status_t;

/** Timestamp attached to one DMIC block after its DMA transfer completes. */
typedef struct {
    uint32_t sequence;
    uint32_t completed_cycle;
} m55m1_tlv320dac3100_dmic_block_timing_t;

/**
 * @brief Receive one synchronous board-initialisation progress event.
 * @param stage Board boundary reached by the caller.
 * @param context Caller-owned value forwarded unchanged by the board adapter.
 *
 * The adapter does not retain either the callback or context after its
 * initialisation function returns. The callback is never invoked in IRQ
 * context.
 */
typedef void (*m55m1_tlv320dac3100_board_progress_callback_t)(
    m55m1_tlv320dac3100_board_stage_t stage, void *context);

/**
 * @brief Configure clocks, pins, PDMA0 channel 0, and the generic driver config.
 * @param profile Selected 16 kHz or 32 kHz transport profile.
 * @param config Output configuration for `tlv320dac3100_init`.
 * @param callback Lightweight callback invoked from PDMA0 IRQ context.
 * @param callback_context Context forwarded to `callback`.
 * @return Clock/peripheral binding result; `config` is valid only for `OK`.
 *
 * The caller owns `config`, callback, and callback_context. This board adapter
 * owns clock and pin-mux registers. It bounds HXT/APLL0 readiness waits so a
 * diagnostic can show a visible failure rather than appearing not to start.
 */
m55m1_tlv320dac3100_board_init_result_t
m55m1_tlv320dac3100_test_board_init(tlv320dac3100_audio_profile_t profile,
                                    tlv320dac3100_config_t *config,
                                    tlv320dac3100_block_callback_t callback,
                                    void *callback_context);

/**
 * @brief Configure the test board and report each clock/peripheral boundary.
 * @param profile Selected 16 kHz or 32 kHz transport profile.
 * @param config Output configuration for `tlv320dac3100_init` on `OK` only.
 * @param callback Lightweight PDMA0 IRQ callback for the generic driver.
 * @param callback_context Caller-owned PDMA callback context.
 * @param progress_callback Optional foreground progress observer; may be NULL.
 * @param progress_context Caller-owned value forwarded to `progress_callback`.
 * @return Clock/peripheral binding result; `config` is valid only for `OK`.
 *
 * The callback observes each clock transition, module-clock binding, audio
 * pin mux, I2C recovery, and final peripheral readiness. It is a diagnostic
 * boundary only and never changes board ownership, DMA descriptors, or codec
 * state.
 */
m55m1_tlv320dac3100_board_init_result_t
m55m1_tlv320dac3100_test_board_init_with_progress(
    tlv320dac3100_audio_profile_t profile, tlv320dac3100_config_t *config,
    tlv320dac3100_block_callback_t callback,
    void *callback_context,
    m55m1_tlv320dac3100_board_progress_callback_t progress_callback,
    void *progress_context);

/**
 * @brief Copy the validated board clock contract after successful board init.
 * @return True only when `snapshot` is non-NULL and the clock contract is valid.
 */
bool m55m1_tlv320dac3100_test_get_clock_snapshot(
    m55m1_tlv320dac3100_clock_snapshot_t *snapshot);

/**
 * @brief Start 16 kHz or 32 kHz capture from the onboard MP34DT05 through DMIC1.
 * @param profile Selected PCM profile; it must match the active TLV320DAC3100 profile.
 * @return True when DMIC, LPPDMA, and the requested 10 ms profile are ready.
 *
 * This reserves LPPDMA0 channel 0 and enables the PB2/PB3 DMIC1 pin binding.
 * It can run independently of `m55m1_tlv320dac3100_test_board_init`, which
 * lets the UART-only microphone diagnostic avoid assigning PB4/PB5 to I2S0.
 * Start it only after a foreground consumer for captured blocks is ready.
 */
bool m55m1_tlv320dac3100_test_dmic_start(tlv320dac3100_audio_profile_t profile);

/**
 * @brief Copy one completed 10 ms DMIC block into caller-owned storage.
 * @param destination Caller-owned output storage.
 * @param sample_count Number of signed-16 samples in `destination`; must match `profile` at start.
 * @return True when a completed block was copied; false when none is ready.
 *
 * Call only from foreground context. The board retains ownership of the DMA
 * buffers; this API copies the block so the caller may reuse `destination`.
 */
bool m55m1_tlv320dac3100_test_dmic_take_block(
    int16_t *destination, uint16_t sample_count);

/**
 * @brief Copy one completed DMIC block and its DMA-completion timestamp.
 *
 * This is the timestamp origin for end-to-end audio telemetry. It runs in
 * foreground; LPPDMA IRQ only writes the fixed-size timing record.
 */
bool m55m1_tlv320dac3100_test_dmic_take_block_timed(
    int16_t *destination, uint16_t sample_count,
    m55m1_tlv320dac3100_dmic_block_timing_t *timing);

/** Return true when foreground should prioritize a completed DMIC block. */
bool m55m1_tlv320dac3100_test_dmic_block_pending(void);

/** @brief Return the current free-running SysTick count used by telemetry. */
uint32_t m55m1_tlv320dac3100_test_cycles_now(void);

/** @brief Return wrap-safe elapsed cycles between two SysTick samples. */
uint32_t m55m1_tlv320dac3100_test_cycles_elapsed(
    uint32_t start_cycle, uint32_t end_cycle);

/** @brief Convert a short free-running SysTick interval to microseconds. */
uint32_t m55m1_tlv320dac3100_test_cycles_to_us(uint32_t cycles);

/**
 * @brief Copy the current DMIC capture-health counters to foreground storage.
 * @param status Caller-owned destination; ignored when NULL.
 *
 * This function does not clear counters or alter DMA ownership. It briefly
 * masks interrupts only while copying the ISR-owned counter values, so UART
 * diagnostics can distinguish capture loss from output-side PDMA errors.
 */
void m55m1_tlv320dac3100_test_dmic_get_status(
    m55m1_tlv320dac3100_dmic_status_t *status);

#ifdef __cplusplus
}
#endif
