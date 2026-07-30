/**
 * @file audio_transport.h
 * @brief Production 16 kHz M55M1 DMIC-to-TLV320DAC3100 transport driver.
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
#define AUDIO_TRANSPORT_SAMPLES_PER_BLOCK    160U
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

bool audio_transport_init(
    audio_transport_init_report_t *report,
    audio_transport_progress_callback_t progress_callback,
    void *progress_context);
audio_transport_status_t audio_transport_start(void);
void audio_transport_stop_safe(void);
bool audio_transport_take_capture(
    int16_t destination[AUDIO_TRANSPORT_SAMPLES_PER_BLOCK],
    audio_transport_capture_timing_t *timing);
bool audio_transport_capture_pending(void);
audio_transport_status_t audio_transport_submit(
    const int16_t samples[AUDIO_TRANSPORT_SAMPLES_PER_BLOCK]);
audio_transport_status_t audio_transport_submit_tagged(
    const int16_t samples[AUDIO_TRANSPORT_SAMPLES_PER_BLOCK],
    const audio_transport_block_tag_t *tag);
bool audio_transport_take_completion(audio_transport_completion_t *completion);
void audio_transport_get_health(audio_transport_health_t *health);
uint32_t audio_transport_cycles_now(void);
uint32_t audio_transport_cycles_elapsed(
    uint32_t start_cycle,
    uint32_t end_cycle);
uint32_t audio_transport_cycles_to_us(uint32_t cycles);

#ifdef __cplusplus
}
#endif
