/**
 * @file audio_transport.c
 * @brief Production binding of M55M1 audio resources to the codec driver.
 * @author Bui Minh Huy
 * @date 2026-07-30
 */

#include <stddef.h>

#include "board/m55m1_audio.h"
#include "board/m55m1_stack_monitor.h"
#include "board/m55m1_timebase.h"
#include "board/m55m1_power.h"
#ifndef M55M1_ENABLE_DAC_I2S_OUTPUT
#define M55M1_ENABLE_DAC_I2S_OUTPUT 0
#endif

#include "drivers/audio_transport/audio_transport.h"
#if M55M1_ENABLE_DAC_I2S_OUTPUT
#include "drivers/tlv320dac3100/tlv320dac3100.h"
#endif
#include "NuMicro.h"

typedef struct {
    audio_transport_progress_callback_t callback;
    void *context;
} audio_transport_progress_context_t;

#if M55M1_RUNTIME_TELEMETRY || M55M1_AUDIO_RELEASE_PROBE
const volatile audio_startup_trace_t *audio_transport_get_startup_trace(void) {
    return m55m1_audio_get_startup_trace();
}
#endif

#if M55M1_ENABLE_DAC_I2S_OUTPUT
static void audio_transport_playback_irq(uint32_t completed_cycle, void *context) {
    (void)context;
    tlv320dac3100_handle_pdma_irq(completed_cycle);
}
#endif

static void audio_transport_board_progress(m55m1_audio_board_stage_t stage, void *context) {
    audio_transport_progress_context_t *progress = (audio_transport_progress_context_t *)context;

    progress->callback((audio_transport_board_stage_t)stage, progress->context);
}

#if M55M1_ENABLE_DAC_I2S_OUTPUT
static audio_transport_status_t audio_transport_map_status(tlv320dac3100_status_t status) {
    if (status == TLV320DAC3100_STATUS_OK) {
        return AUDIO_TRANSPORT_STATUS_OK;
    }
    if (status == TLV320DAC3100_STATUS_BUSY) {
        return AUDIO_TRANSPORT_STATUS_BUSY;
    }
    return AUDIO_TRANSPORT_STATUS_ERROR;
}
#endif

bool audio_transport_init(audio_transport_init_report_t *report,
                          audio_transport_progress_callback_t progress_callback,
                          void *progress_context) {
    m55m1_audio_clock_snapshot_t clocks;
    m55m1_audio_board_init_result_t board_status;
#if M55M1_ENABLE_DAC_I2S_OUTPUT
    m55m1_audio_playback_binding_t binding;
    tlv320dac3100_config_t config;
    tlv320dac3100_status_t codec_status;
#endif
    audio_transport_progress_context_t progress = {
        .callback = progress_callback,
        .context = progress_context,
    };

    if (report == NULL) {
        return false;
    }
    *report = (audio_transport_init_report_t){0};
    board_status = m55m1_audio_board_init(
        M55M1_AUDIO_PROFILE_16KHZ,
#if M55M1_ENABLE_DAC_I2S_OUTPUT
        &binding, audio_transport_playback_irq,
#else
        NULL, NULL,
#endif
        NULL, (progress_callback != NULL) ? audio_transport_board_progress : NULL,
        (progress_callback != NULL) ? &progress : NULL);
    report->board_status = (int)board_status;
#if M55M1_RUNTIME_TELEMETRY
    report->i2c_trace = m55m1_audio_get_i2c_trace();
#endif
    if (board_status != M55M1_AUDIO_BOARD_INIT_OK) {
        return false;
    }
    if (m55m1_audio_get_clock_snapshot(&clocks)) {
        report->core_hz = clocks.core_hz;
        report->apll0_hz = clocks.apll0_hz;
        report->apll1_hz = clocks.apll1_hz;
        report->i2s_src_hz = clocks.i2s_src_hz;
        report->mclk_hz = clocks.mclk_hz;
        report->clocks_valid = true;
    }

#if M55M1_ENABLE_DAC_I2S_OUTPUT
    config = (tlv320dac3100_config_t){
        .i2c = binding.i2c,
        .i2s = binding.i2s,
        .pdma = binding.pdma,
        .pdma_channel = binding.pdma_channel,
        .profile = TLV320DAC3100_AUDIO_PROFILE_16KHZ,
        .max_digital_gain_db = binding.max_digital_gain_db,
        .block_callback = NULL,
        .callback_context = NULL,
    };
    codec_status = tlv320dac3100_init(&config);
    report->codec_status = (int)codec_status;
    if (codec_status != TLV320DAC3100_STATUS_OK) {
#if M55M1_RUNTIME_TELEMETRY
        tlv320dac3100_driver_status_t status;
        tlv320dac3100_get_status(&status);
        report->codec_i2c_failures = status.i2c_failures;
        report->codec_i2c_control = status.last_i2c_control;
        report->codec_i2c_page = status.last_i2c_page;
        report->codec_i2c_register = status.last_i2c_register;
        report->codec_i2c_phase = status.last_i2c_phase;
        report->codec_i2c_attempts = status.last_i2c_attempts;
        report->codec_i2c_status = status.last_i2c_status;
        report->codec_i2c_expected_status = status.last_i2c_expected_status;
        report->codec_i2c_step = status.last_i2c_step;
        report->codec_i2c_timed_out = status.last_i2c_timed_out;
        report->codec_i2c_stop_timed_out = status.last_i2c_stop_timed_out;
#endif
        return false;
    }
#else
    report->codec_status = 0;
#endif
    return true;
}

audio_transport_status_t audio_transport_start(void) {
    if (!m55m1_audio_dmic_prepare(M55M1_AUDIO_PROFILE_16KHZ)) {
        m55m1_audio_dmic_stop();
        return AUDIO_TRANSPORT_STATUS_ERROR;
    }
#if M55M1_ENABLE_DAC_I2S_OUTPUT
    const tlv320dac3100_status_t status = tlv320dac3100_start();

    if (status != TLV320DAC3100_STATUS_OK) {
        m55m1_audio_dmic_stop();
        return audio_transport_map_status(status);
    }
#endif
    m55m1_audio_dmic_enable();
    return AUDIO_TRANSPORT_STATUS_OK;
}

void audio_transport_stop_safe(void) {
    m55m1_audio_dmic_stop();
#if M55M1_ENABLE_DAC_I2S_OUTPUT
    (void)tlv320dac3100_stop();
#endif
}

bool audio_transport_take_capture(int32_t destination[AUDIO_TRANSPORT_SAMPLES_PER_BLOCK],
                                  audio_transport_capture_timing_t *timing) {
    m55m1_audio_dmic_block_timing_t board_timing;

    if (!m55m1_audio_dmic_take_block_timed(destination, AUDIO_TRANSPORT_SAMPLES_PER_BLOCK,
                                           &board_timing)) {
        return false;
    }
    if (timing != NULL) {
        timing->sequence = board_timing.sequence;
        timing->completed_cycle = board_timing.completed_cycle;
    }
    return true;
}

bool audio_transport_capture_pending(void) {
    return m55m1_audio_dmic_block_pending();
}

void audio_transport_wait_for_work(void) {
    const uint32_t primask = __get_PRIMASK();
    audio_transport_health_t health;

    if ((primask != 0U) || (__get_IPSR() != 0U) || (__get_BASEPRI() != 0U) ||
        (__get_FAULTMASK() != 0U)) {
        return;
    }
    __disable_irq();
    audio_transport_get_health(&health);
    if (!audio_transport_capture_pending() && (health.dmic_overrun_blocks == 0U) &&
        (health.dmic_dma_errors == 0U) && (health.dac_underrun_blocks == 0U) &&
        (health.dac_dma_errors == 0U)) {
        /* PRIMASK keeps an arriving IRQ pending until after WFI, avoiding lost wake. */
        m55m1_power_idle();
    }
    __set_PRIMASK(primask);
}

audio_transport_status_t
audio_transport_submit_tagged(const int16_t samples[AUDIO_TRANSPORT_SAMPLES_PER_BLOCK],
                              const audio_transport_block_tag_t *tag) {
    if (tag == NULL) {
        return AUDIO_TRANSPORT_STATUS_ERROR;
    }
#if M55M1_ENABLE_DAC_I2S_OUTPUT
    return audio_transport_map_status(tlv320dac3100_submit_mono_block_tagged(
        samples, AUDIO_TRANSPORT_SAMPLES_PER_BLOCK,
        &(tlv320dac3100_audio_block_tag_t){
            .sequence = tag->sequence,
            .capture_completed_cycle = tag->capture_completed_cycle,
        }));
#else
    (void)samples;
    return AUDIO_TRANSPORT_STATUS_ERROR;
#endif
}

bool audio_transport_take_completion(audio_transport_completion_t *completion) {
#if M55M1_ENABLE_DAC_I2S_OUTPUT
    tlv320dac3100_audio_completion_t driver_completion;

    if ((completion == NULL) || !tlv320dac3100_take_audio_completion(&driver_completion)) {
        return false;
    }
    completion->sequence = driver_completion.sequence;
    completion->capture_completed_cycle = driver_completion.capture_completed_cycle;
    completion->dac_completed_cycle = driver_completion.dac_completed_cycle;
    completion->playback_slot = driver_completion.playback_slot;
    return true;
#else
    (void)completion;
    return false;
#endif
}

void audio_transport_get_health(audio_transport_health_t *health) {
    m55m1_audio_dmic_status_t dmic;
#if M55M1_ENABLE_DAC_I2S_OUTPUT
    tlv320dac3100_driver_status_t dac;
#endif

    if (health == NULL) {
        return;
    }
    m55m1_audio_dmic_get_status(&dmic);
#if M55M1_ENABLE_DAC_I2S_OUTPUT
    tlv320dac3100_get_status(&dac);
#endif
    *health = (audio_transport_health_t){
        .dmic_overrun_blocks = dmic.overrun_blocks,
        .dmic_dma_errors = dmic.dma_errors,
#if M55M1_ENABLE_DAC_I2S_OUTPUT
        .dac_underrun_blocks = dac.underrun_blocks,
        .dac_dma_errors = dac.dma_errors,
#else
        .dac_underrun_blocks = 0U,
        .dac_dma_errors = 0U,
#endif
    };
}

uint32_t audio_transport_cycles_now(void) {
    return m55m1_timebase_cycles_now();
}

uint32_t audio_transport_cycles_elapsed(uint32_t start_cycle, uint32_t end_cycle) {
    return m55m1_timebase_cycles_elapsed(start_cycle, end_cycle);
}

uint32_t audio_transport_cycles_to_us(uint32_t cycles) {
    return m55m1_timebase_cycles_to_us(cycles);
}

uint32_t audio_transport_stack_configured_bytes(void) {
    return m55m1_stack_configured_bytes();
}

uint32_t audio_transport_stack_high_water_bytes(void) {
    return m55m1_stack_high_water_bytes();
}

uint32_t audio_transport_stack_headroom_bytes(void) {
    return m55m1_stack_headroom_bytes();
}
