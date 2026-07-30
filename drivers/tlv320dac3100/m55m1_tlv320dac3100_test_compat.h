/**
 * @file m55m1_tlv320dac3100_test_compat.h
 * @brief Compatibility API for existing isolated TLV320DAC3100 diagnostics.
 * @author Bui Minh Huy
 * @date 2026-07-30
 */

#pragma once

#include "board/m55m1_audio.h"
#include "drivers/tlv320dac3100/tlv320dac3100.h"

#ifdef __cplusplus
extern "C" {
#endif

#define M55M1_TLV320DAC3100_SAFE_MAX_GAIN_DB M55M1_AUDIO_SAFE_MAX_GAIN_DB
#define M55M1_TLV320DAC3100_BOARD_INIT_OK M55M1_AUDIO_BOARD_INIT_OK
#define M55M1_TLV320DAC3100_BOARD_INIT_HXT_TIMEOUT M55M1_AUDIO_BOARD_INIT_HXT_TIMEOUT
#define M55M1_TLV320DAC3100_BOARD_INIT_APLL1_TIMEOUT M55M1_AUDIO_BOARD_INIT_APLL1_TIMEOUT
#define M55M1_TLV320DAC3100_BOARD_INIT_APLL0_TIMEOUT M55M1_AUDIO_BOARD_INIT_APLL0_TIMEOUT
#define M55M1_TLV320DAC3100_BOARD_INIT_CLOCK_MISMATCH M55M1_AUDIO_BOARD_INIT_CLOCK_MISMATCH
#define M55M1_TLV320DAC3100_BOARD_INIT_I2C_STUCK M55M1_AUDIO_BOARD_INIT_I2C_STUCK

#define M55M1_TLV320DAC3100_BOARD_STAGE_ENTER M55M1_AUDIO_BOARD_STAGE_ENTER
#define M55M1_TLV320DAC3100_BOARD_STAGE_SCLK_HIRC_READY M55M1_AUDIO_BOARD_STAGE_SCLK_HIRC_READY
#define M55M1_TLV320DAC3100_BOARD_STAGE_HXT_WAIT M55M1_AUDIO_BOARD_STAGE_HXT_WAIT
#define M55M1_TLV320DAC3100_BOARD_STAGE_HXT_READY M55M1_AUDIO_BOARD_STAGE_HXT_READY
#define M55M1_TLV320DAC3100_BOARD_STAGE_APLL1_WAIT M55M1_AUDIO_BOARD_STAGE_APLL1_WAIT
#define M55M1_TLV320DAC3100_BOARD_STAGE_APLL1_READY M55M1_AUDIO_BOARD_STAGE_APLL1_READY
#define M55M1_TLV320DAC3100_BOARD_STAGE_APLL0_WAIT M55M1_AUDIO_BOARD_STAGE_APLL0_WAIT
#define M55M1_TLV320DAC3100_BOARD_STAGE_APLL0_READY M55M1_AUDIO_BOARD_STAGE_APLL0_READY
#define M55M1_TLV320DAC3100_BOARD_STAGE_SCLK_APLL0_READY M55M1_AUDIO_BOARD_STAGE_SCLK_APLL0_READY
#define M55M1_TLV320DAC3100_BOARD_STAGE_MODULE_CLOCKS_READY M55M1_AUDIO_BOARD_STAGE_MODULE_CLOCKS_READY
#define M55M1_TLV320DAC3100_BOARD_STAGE_AUDIO_PINS_READY M55M1_AUDIO_BOARD_STAGE_AUDIO_PINS_READY
#define M55M1_TLV320DAC3100_BOARD_STAGE_I2C_RECOVERY_READY M55M1_AUDIO_BOARD_STAGE_I2C_RECOVERY_READY
#define M55M1_TLV320DAC3100_BOARD_STAGE_I2C_PINS_READY M55M1_AUDIO_BOARD_STAGE_I2C_PINS_READY
#define M55M1_TLV320DAC3100_BOARD_STAGE_I2C_RESET_READY M55M1_AUDIO_BOARD_STAGE_I2C_RESET_READY
#define M55M1_TLV320DAC3100_BOARD_STAGE_CYCLE_COUNTER_READY M55M1_AUDIO_BOARD_STAGE_CYCLE_COUNTER_READY
#define M55M1_TLV320DAC3100_BOARD_STAGE_PERIPHERALS_READY M55M1_AUDIO_BOARD_STAGE_PERIPHERALS_READY

typedef m55m1_audio_board_init_result_t
    m55m1_tlv320dac3100_board_init_result_t;
typedef m55m1_audio_board_stage_t m55m1_tlv320dac3100_board_stage_t;
typedef m55m1_audio_clock_snapshot_t m55m1_tlv320dac3100_clock_snapshot_t;
typedef m55m1_audio_dmic_status_t m55m1_tlv320dac3100_dmic_status_t;
typedef m55m1_audio_dmic_block_timing_t
    m55m1_tlv320dac3100_dmic_block_timing_t;
typedef m55m1_audio_board_progress_callback_t
    m55m1_tlv320dac3100_board_progress_callback_t;

/**
 * @brief Initialize the legacy diagnostic board and codec binding.
 * @param profile Requested fixed audio profile.
 * @param config Caller-owned codec configuration destination.
 * @param callback Optional PDMA completion callback.
 * @param callback_context Caller-retained callback context.
 * @return Detailed board initialization result.
 *
 * Test foreground only. The callback context must remain valid until capture
 * and playback stop.
 */
m55m1_tlv320dac3100_board_init_result_t
m55m1_tlv320dac3100_test_board_init(
    tlv320dac3100_audio_profile_t profile,
    tlv320dac3100_config_t *config,
    tlv320dac3100_block_callback_t callback,
    void *callback_context);

/**
 * @brief Initialize the diagnostic binding with boot progress reporting.
 * @param profile Requested fixed audio profile.
 * @param config Caller-owned codec configuration destination.
 * @param callback Optional PDMA completion callback.
 * @param callback_context Caller-retained callback context.
 * @param progress_callback Optional synchronous boot-stage observer.
 * @param progress_context Caller-owned observer context used only during init.
 * @return Detailed board initialization result.
 *
 * Test foreground only. The callback context must remain valid until capture
 * and playback stop.
 */
m55m1_tlv320dac3100_board_init_result_t
m55m1_tlv320dac3100_test_board_init_with_progress(
    tlv320dac3100_audio_profile_t profile,
    tlv320dac3100_config_t *config,
    tlv320dac3100_block_callback_t callback,
    void *callback_context,
    m55m1_tlv320dac3100_board_progress_callback_t progress_callback,
    void *progress_context);

/**
 * @brief Copy the validated diagnostic clock snapshot.
 * @param snapshot Caller-owned destination.
 * @return True after successful initialization; false for NULL or invalid state.
 */
bool m55m1_tlv320dac3100_test_get_clock_snapshot(
    m55m1_tlv320dac3100_clock_snapshot_t *snapshot);

/**
 * @brief Start diagnostic DMIC capture.
 * @param profile Active fixed audio profile.
 * @return True when DMA capture starts.
 *
 * Test foreground only.
 */
bool m55m1_tlv320dac3100_test_dmic_start(
    tlv320dac3100_audio_profile_t profile);

/**
 * @brief Copy one completed diagnostic capture block.
 * @param destination Caller-owned PCM destination.
 * @param sample_count Exact sample count for the active profile.
 * @return True when a block was consumed.
 *
 * Test foreground only; the DMA ISR remains the producer.
 */
bool m55m1_tlv320dac3100_test_dmic_take_block(
    int16_t *destination,
    uint16_t sample_count);

/**
 * @brief Copy one capture block and its completion tag.
 * @param destination Caller-owned PCM destination.
 * @param sample_count Exact sample count for the active profile.
 * @param timing Caller-owned timing destination.
 * @return True when a block and tag were consumed.
 *
 * Test foreground only; the DMA ISR remains the producer.
 */
bool m55m1_tlv320dac3100_test_dmic_take_block_timed(
    int16_t *destination,
    uint16_t sample_count,
    m55m1_tlv320dac3100_dmic_block_timing_t *timing);

/**
 * @brief Check whether diagnostic capture has a completed block.
 * @return True when a block appears available.
 *
 * Best-effort ISR-shared snapshot; this call does not reserve the block.
 */
bool m55m1_tlv320dac3100_test_dmic_block_pending(void);

/**
 * @brief Read the diagnostic board cycle counter.
 * @return Current cycle value; wrap is expected.
 */
uint32_t m55m1_tlv320dac3100_test_cycles_now(void);

/**
 * @brief Calculate wrap-safe elapsed diagnostic cycles.
 * @param start_cycle Earlier counter sample.
 * @param end_cycle Later counter sample.
 * @return Unsigned elapsed cycles across at most one wrap.
 */
uint32_t m55m1_tlv320dac3100_test_cycles_elapsed(
    uint32_t start_cycle,
    uint32_t end_cycle);

/**
 * @brief Convert diagnostic cycles to microseconds.
 * @param cycles Cycle delta.
 * @return Truncated microseconds at the configured core clock.
 */
uint32_t m55m1_tlv320dac3100_test_cycles_to_us(uint32_t cycles);

/**
 * @brief Copy diagnostic DMIC health counters.
 * @param status Caller-owned destination, ignored when NULL.
 * @return None.
 *
 * Test foreground only.
 */
void m55m1_tlv320dac3100_test_dmic_get_status(
    m55m1_tlv320dac3100_dmic_status_t *status);

#ifdef __cplusplus
}
#endif
