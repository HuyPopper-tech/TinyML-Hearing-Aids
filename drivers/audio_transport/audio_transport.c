/**
 * @file audio_transport.c
 * @brief Production binding of M55M1 audio resources to the codec driver.
 * @author Bui Minh Huy
 * @date 2026-07-30
 */

#include <stddef.h>

#include "board/m55m1_audio.h"
#include "board/m55m1_timebase.h"
#include "drivers/audio_transport/audio_transport.h"
#include "drivers/tlv320dac3100/tlv320dac3100.h"

typedef struct {
    audio_transport_progress_callback_t callback;
    void *context;
} audio_transport_progress_context_t;

static void audio_transport_playback_irq(uint32_t completed_cycle, void *context)
{
    (void)context;
    tlv320dac3100_handle_pdma_irq(completed_cycle);
}

static void audio_transport_board_progress(
    m55m1_audio_board_stage_t stage,
    void *context)
{
    audio_transport_progress_context_t *progress =
        (audio_transport_progress_context_t *)context;

    progress->callback((audio_transport_board_stage_t)stage, progress->context);
}

static audio_transport_status_t audio_transport_map_status(
    tlv320dac3100_status_t status)
{
    if (status == TLV320DAC3100_STATUS_OK) {
        return AUDIO_TRANSPORT_STATUS_OK;
    }
    if (status == TLV320DAC3100_STATUS_BUSY) {
        return AUDIO_TRANSPORT_STATUS_BUSY;
    }
    return AUDIO_TRANSPORT_STATUS_ERROR;
}

bool audio_transport_init(
    audio_transport_init_report_t *report,
    audio_transport_progress_callback_t progress_callback,
    void *progress_context)
{
    m55m1_audio_playback_binding_t binding;
    m55m1_audio_clock_snapshot_t clocks;
    m55m1_audio_board_init_result_t board_status;
    tlv320dac3100_config_t config;
    tlv320dac3100_status_t codec_status;
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
        &binding,
        audio_transport_playback_irq,
        NULL,
        (progress_callback != NULL) ? audio_transport_board_progress : NULL,
        (progress_callback != NULL) ? &progress : NULL);
    report->board_status = (int)board_status;
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
        return false;
    }
    return true;
}

audio_transport_status_t audio_transport_start(void)
{
    const tlv320dac3100_status_t status = tlv320dac3100_start();

    if (status != TLV320DAC3100_STATUS_OK) {
        return audio_transport_map_status(status);
    }
    if (!m55m1_audio_dmic_start(M55M1_AUDIO_PROFILE_16KHZ)) {
        (void)tlv320dac3100_stop();
        return AUDIO_TRANSPORT_STATUS_ERROR;
    }
    return AUDIO_TRANSPORT_STATUS_OK;
}

void audio_transport_stop_safe(void)
{
    m55m1_audio_dmic_stop();
    (void)tlv320dac3100_stop();
}

bool audio_transport_take_capture(
    int16_t destination[AUDIO_TRANSPORT_SAMPLES_PER_BLOCK],
    audio_transport_capture_timing_t *timing)
{
    m55m1_audio_dmic_block_timing_t board_timing;

    if (!m55m1_audio_dmic_take_block_timed(
            destination, AUDIO_TRANSPORT_SAMPLES_PER_BLOCK, &board_timing)) {
        return false;
    }
    if (timing != NULL) {
        timing->sequence = board_timing.sequence;
        timing->completed_cycle = board_timing.completed_cycle;
    }
    return true;
}

bool audio_transport_capture_pending(void)
{
    return m55m1_audio_dmic_block_pending();
}

audio_transport_status_t audio_transport_submit(
    const int16_t samples[AUDIO_TRANSPORT_SAMPLES_PER_BLOCK])
{
    return audio_transport_map_status(tlv320dac3100_submit_mono_block(
        samples, AUDIO_TRANSPORT_SAMPLES_PER_BLOCK));
}

audio_transport_status_t audio_transport_submit_tagged(
    const int16_t samples[AUDIO_TRANSPORT_SAMPLES_PER_BLOCK],
    const audio_transport_block_tag_t *tag)
{
    if (tag == NULL) {
        return AUDIO_TRANSPORT_STATUS_ERROR;
    }
    return audio_transport_map_status(tlv320dac3100_submit_mono_block_tagged(
        samples,
        AUDIO_TRANSPORT_SAMPLES_PER_BLOCK,
        &(tlv320dac3100_audio_block_tag_t){
            .sequence = tag->sequence,
            .capture_completed_cycle = tag->capture_completed_cycle,
        }));
}

bool audio_transport_take_completion(audio_transport_completion_t *completion)
{
    tlv320dac3100_audio_completion_t driver_completion;

    if ((completion == NULL) ||
        !tlv320dac3100_take_audio_completion(&driver_completion)) {
        return false;
    }
    completion->sequence = driver_completion.sequence;
    completion->capture_completed_cycle =
        driver_completion.capture_completed_cycle;
    completion->dac_completed_cycle = driver_completion.dac_completed_cycle;
    return true;
}

void audio_transport_get_health(audio_transport_health_t *health)
{
    m55m1_audio_dmic_status_t dmic;
    tlv320dac3100_driver_status_t dac;

    if (health == NULL) {
        return;
    }
    m55m1_audio_dmic_get_status(&dmic);
    tlv320dac3100_get_status(&dac);
    *health = (audio_transport_health_t){
        .dmic_overrun_blocks = dmic.overrun_blocks,
        .dmic_dma_errors = dmic.dma_errors,
        .dac_underrun_blocks = dac.underrun_blocks,
        .dac_dma_errors = dac.dma_errors,
    };
}

uint32_t audio_transport_cycles_now(void)
{
    return m55m1_timebase_cycles_now();
}

uint32_t audio_transport_cycles_elapsed(
    uint32_t start_cycle,
    uint32_t end_cycle)
{
    return m55m1_timebase_cycles_elapsed(start_cycle, end_cycle);
}

uint32_t audio_transport_cycles_to_us(uint32_t cycles)
{
    return m55m1_timebase_cycles_to_us(cycles);
}
