/**
 * @file tlv320dac3100.h
 * @brief Public API for fixed-profile TLV320DAC3100 headphone playback.
 * @author Bui Minh Huy
 * @date 2026-07-20
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "NuMicro.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief TLV320DAC3100 7-bit I2C address.
 */
#define TLV320DAC3100_I2C_ADDRESS                 0x18U
/**
 * @brief Fixed duration of each real-time PCM/DMA block.
 */
#define TLV320DAC3100_BLOCK_DURATION_MS            6U
/**
 * @brief 16 kHz profile PCM rate.
 */
#define TLV320DAC3100_16KHZ_SAMPLE_RATE_HZ         16000UL
/**
 * @brief 16 kHz profile direct codec MCLK, 256 times the PCM rate.
 */
#define TLV320DAC3100_16KHZ_MCLK_HZ                4096000UL
/**
 * @brief 16 kHz profile mono samples in one 10 ms block.
 */
#define TLV320DAC3100_16KHZ_SAMPLES_PER_BLOCK      96U
/**
 * @brief 32 kHz profile PCM rate.
 */
#define TLV320DAC3100_32KHZ_SAMPLE_RATE_HZ         32000UL
/**
 * @brief 32 kHz profile direct codec MCLK, 256 times the PCM rate.
 */
#define TLV320DAC3100_32KHZ_MCLK_HZ                8192000UL
/**
 * @brief 32 kHz profile mono samples in one 10 ms block.
 */
#define TLV320DAC3100_32KHZ_SAMPLES_PER_BLOCK      320U
/**
 * @brief Maximum foreground PCM storage required by either supported profile.
 */
#define TLV320DAC3100_MAX_SAMPLES_PER_BLOCK        TLV320DAC3100_32KHZ_SAMPLES_PER_BLOCK
/**
 * @brief Compatibility aliases for existing fixed-16-kHz applications.
 */
#define TLV320DAC3100_SAMPLE_RATE_HZ               TLV320DAC3100_16KHZ_SAMPLE_RATE_HZ
#define TLV320DAC3100_MCLK_HZ                      TLV320DAC3100_16KHZ_MCLK_HZ
#define TLV320DAC3100_MONO_SAMPLES_PER_BLOCK       TLV320DAC3100_16KHZ_SAMPLES_PER_BLOCK

/**
 * @brief Supported PCM and codec-clock profiles.
 *
 * Profile changes require a new driver initialization lifecycle.
 */
typedef enum {
    TLV320DAC3100_AUDIO_PROFILE_16KHZ = 0,
    TLV320DAC3100_AUDIO_PROFILE_32KHZ,
} tlv320dac3100_audio_profile_t;

/**
 * @brief Driver operation result.
 */
typedef enum {
    TLV320DAC3100_STATUS_OK = 0,
    TLV320DAC3100_STATUS_INVALID_ARGUMENT,
    TLV320DAC3100_STATUS_INVALID_STATE,
    TLV320DAC3100_STATUS_BUSY,
    TLV320DAC3100_STATUS_CLOCK_MISMATCH,
    TLV320DAC3100_STATUS_I2C_ERROR,
    TLV320DAC3100_STATUS_DMA_ERROR,
} tlv320dac3100_status_t;

/**
 * @brief Lightweight notification raised after a PDMA block completes.
 *
 * The callback runs in PDMA IRQ context. It may set an event flag only; it
 * must not use I2C, allocate memory, log, or submit another audio block.
 */
typedef void (*tlv320dac3100_block_callback_t)(void *context);

/**
 * @brief Hardware resources prepared by the board layer before driver init.
 *
 * The board owns peripheral clocks, pin mux, PDMA clock, and NVIC priority.
 * The driver supports either I2S0 or I2S1 but one active codec instance.
 * Its internal DMA samples and descriptors use `NVT_NONCACHEABLE` storage.
 */
typedef struct {
    I2C_T *i2c;
    I2S_T *i2s;
    PDMA_T *pdma;
    uint8_t pdma_channel;
    /**
     * @brief Selected fixed profile; the board must prepare matching I2S clocks.
     */
    tlv320dac3100_audio_profile_t profile;
    /**
     * @brief Highest allowed per-channel digital gain in dB, inclusive, from -63 to 0.
     */
    int8_t max_digital_gain_db;
    tlv320dac3100_block_callback_t block_callback;
    void *callback_context;
} tlv320dac3100_config_t;

/**
 * @brief Opaque capture timestamp and sequence transported with one PCM block.
 */
typedef struct {
    uint32_t sequence;
    uint32_t capture_completed_cycle;
} tlv320dac3100_audio_block_tag_t;

/**
 * @brief Foreground-readable completion record for one tagged DAC DMA block.
 */
typedef struct {
    uint32_t sequence;
    uint32_t capture_completed_cycle;
    uint32_t dac_completed_cycle;
} tlv320dac3100_audio_completion_t;

/**
 * @brief Runtime counters, state, and most recent failed I2C transaction.
 *
 * All fields are safe to query outside interrupt context. `last_i2c_page`,
 * `last_i2c_register`, `last_i2c_phase`, and `last_i2c_attempts` identify the
 * last exhausted I2C retry only when `i2c_failures` is nonzero. Phase 1 is the
 * page-select write and phase 2 is the requested codec-register write.
 */
typedef struct {
    /**
     * @brief Number of PDMA descriptors completed since the current init.
     */
    uint32_t completed_blocks;
    /**
     * @brief Number of PCM blocks accepted and copied into a PDMA slot.
     */
    uint32_t queued_blocks;
    /**
     * @brief Number of completed slots that played silence after a missed refill.
     */
    uint32_t underrun_blocks;
    /**
     * @brief Number of PDMA abort conditions observed by the driver's IRQ handler.
     */
    uint32_t dma_errors;
    /**
     * @brief Number of I2C operations that exhausted their bounded retry budget.
     */
    uint32_t i2c_failures;
    /**
     * @brief Codec page requested by the latest exhausted I2C operation.
     */
    uint8_t last_i2c_page;
    /**
     * @brief Codec register requested by the latest exhausted I2C operation.
     */
    uint8_t last_i2c_register;
    /**
     * @brief Failed I2C phase: 1 for page select, 2 for target register.
     */
    uint8_t last_i2c_phase;
    /**
     * @brief Number of attempts consumed by the latest exhausted I2C operation.
     */
    uint8_t last_i2c_attempts;
    /**
     * @brief I2C STATUS0 captured before the latest bounded transaction was aborted.
     */
    uint8_t last_i2c_status;
    /**
     * @brief I2C CTL0 captured before the latest bounded transaction was aborted.
     */
    uint32_t last_i2c_control;
    /**
     * @brief Bit n is one when PDMA has released stereo descriptor slot n.
     */
    uint8_t available_slot_mask;
    /**
     * @brief Bit n is one when the descriptor currently points at caller PCM.
     */
    uint8_t audio_slot_mask;
    /**
     * @brief Descriptor slot expected to complete on the next transfer-done IRQ.
     */
    uint8_t next_completed_slot;
    /**
     * @brief True after configuration completes successfully.
     */
    bool initialized;
    /**
     * @brief True while I2S transmit DMA is actively playing the descriptor ring.
     */
    bool running;
    /**
     * @brief True when both DAC channels are digitally muted.
     */
    bool muted;
} tlv320dac3100_driver_status_t;

/**
 * @brief Return the PCM rate for a supported profile.
 * @param profile Audio profile to query.
 * @return Sample rate in hertz, or zero when profile is invalid.
 */
uint32_t tlv320dac3100_profile_sample_rate_hz(tlv320dac3100_audio_profile_t profile);

/**
 * @brief Return the required direct MCLK for a supported profile.
 * @param profile Audio profile to query.
 * @return MCLK in hertz, or zero when profile is invalid.
 */
uint32_t tlv320dac3100_profile_mclk_hz(tlv320dac3100_audio_profile_t profile);

/**
 * @brief Return the mono sample count for one 10 ms block.
 * @param profile Audio profile to query.
 * @return Signed-16 sample count, or zero when profile is invalid.
 */
uint16_t tlv320dac3100_profile_samples_per_block(tlv320dac3100_audio_profile_t profile);

/**
 * @brief Configure the codec and reserve I2S/PDMA resources.
 * @param config Board-provided I2C, I2S, PDMA, and callback configuration.
 * @return `TLV320DAC3100_STATUS_OK` or an argument, clock, or I2C error.
 */
tlv320dac3100_status_t tlv320dac3100_init(const tlv320dac3100_config_t *config);

/**
 * @brief Start continuous PDMA playback using queued blocks or silence.
 * @return `TLV320DAC3100_STATUS_OK` or `TLV320DAC3100_STATUS_INVALID_STATE`.
 */
tlv320dac3100_status_t tlv320dac3100_start(void);

/**
 * @brief Mute, stop DMA, and power down the DAC/headphone blocks safely.
 * @return `TLV320DAC3100_STATUS_OK` or an I2C/state error.
 *
 * A successful or failed stop terminates the current playback session. Call
 * `tlv320dac3100_init` before submitting or starting audio again.
 */
tlv320dac3100_status_t tlv320dac3100_stop(void);

/**
 * @brief Copy one profile-sized mono PCM block into the next available stereo DMA slot.
 * @param samples Caller-owned signed-16 samples; copied before this call returns.
 * @param sample_count Number of samples at `samples`; must match the selected 10 ms profile.
 * @return `OK`, `BUSY`, or `INVALID_ARGUMENT` for a null or wrong-sized block.
 *
 * Ownership remains with the caller: the input is copied before this function
 * returns and may be reused immediately.
 */
tlv320dac3100_status_t tlv320dac3100_submit_mono_block(
    const int16_t *samples, uint16_t sample_count);

/**
 * @brief Submit PCM with a capture timestamp for end-to-end latency tracking.
 * @param samples Caller-owned mono PCM copied before return.
 * @param sample_count Number of samples; must match the selected profile.
 * @param tag Optional capture metadata copied on successful submission.
 * @return `OK`, `BUSY`, or an argument/state error.
 *
 * The tag is copied into the released PDMA slot and returned by
 * `tlv320dac3100_take_audio_completion` after that slot is played.
 */
tlv320dac3100_status_t tlv320dac3100_submit_mono_block_tagged(
    const int16_t *samples, uint16_t sample_count, const tlv320dac3100_audio_block_tag_t *tag);

/**
 * @brief Copy and consume the newest completed tagged DAC block.
 * @param completion Caller-owned destination.
 * @return True when a completion was consumed; false for NULL or no completion.
 *
 * Foreground-only; the driver does not retain the destination.
 */
bool tlv320dac3100_take_audio_completion(tlv320dac3100_audio_completion_t *completion);

/**
 * @brief Set independent left/right DAC digital gains under the board safety ceiling.
 * @param left_gain_db Requested left-channel gain in dB.
 * @param right_gain_db Requested right-channel gain in dB.
 * @return `OK`, `INVALID_ARGUMENT`, `INVALID_STATE`, or `I2C_ERROR`.
 */
tlv320dac3100_status_t tlv320dac3100_set_channel_digital_gain_db(
    int8_t left_gain_db, int8_t right_gain_db);

/**
 * @brief Set equal left/right DAC digital gain under the configured safety ceiling.
 * @param gain_db Requested whole-dB digital gain.
 * @return `OK`, `INVALID_ARGUMENT`, `INVALID_STATE`, or `I2C_ERROR`.
 */
tlv320dac3100_status_t tlv320dac3100_set_digital_gain_db(int8_t gain_db);

/**
 * @brief Digitally mute or unmute both DAC channels without stopping I2S.
 * @param enabled True to mute; false to restore audio at the configured gain.
 * @return `OK`, `INVALID_STATE`, or `I2C_ERROR`.
 */
tlv320dac3100_status_t tlv320dac3100_mute(bool enabled);

/**
 * @brief Copy driver counters and current state into caller-owned storage.
 * @param status Output destination; must not be null.
 * @return None.
 *
 * Foreground-only best-effort snapshot of IRQ-updated state.
 */
void tlv320dac3100_get_status(tlv320dac3100_driver_status_t *status);

/**
 * @brief Reapply the stored configuration after an explicit caller decision.
 * @return Same result as `tlv320dac3100_init`.
 *
 * This function never restarts playback automatically after a fault.
 */
tlv320dac3100_status_t tlv320dac3100_recover(void);

/**
 * @brief Service transfer-done and abort flags for the configured PDMA channel.
 * @param completed_cycle Board cycle captured for the completed descriptor.
 * @return None.
 *
 * Call only from the matching PDMA IRQ handler. The handler marks a completed
 * descriptor free and switches it to silence before notifying the callback.
 */
void tlv320dac3100_handle_pdma_irq(uint32_t completed_cycle);

#ifdef __cplusplus
}
#endif
