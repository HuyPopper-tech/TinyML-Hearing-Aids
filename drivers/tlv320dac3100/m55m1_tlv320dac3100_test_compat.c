/**
 * @file m55m1_tlv320dac3100_test_compat.c
 * @brief Driver-layer compatibility for isolated audio diagnostic applications.
 * @author Bui Minh Huy
 * @date 2026-07-30
 */

#include <stddef.h>

#include "board/m55m1_timebase.h"
#include "drivers/tlv320dac3100/m55m1_tlv320dac3100_test_compat.h"

static void diagnostic_playback_irq(uint32_t completed_cycle, void *context) {
    (void)context;
    tlv320dac3100_handle_pdma_irq(completed_cycle);
}

m55m1_tlv320dac3100_board_init_result_t
m55m1_tlv320dac3100_test_board_init(
    tlv320dac3100_audio_profile_t profile,
    tlv320dac3100_config_t *config,
    tlv320dac3100_block_callback_t callback,
    void *callback_context) {
    return m55m1_tlv320dac3100_test_board_init_with_progress(
        profile, config, callback, callback_context, NULL, NULL);
}

m55m1_tlv320dac3100_board_init_result_t
m55m1_tlv320dac3100_test_board_init_with_progress(
    tlv320dac3100_audio_profile_t profile,
    tlv320dac3100_config_t *config,
    tlv320dac3100_block_callback_t callback,
    void *callback_context,
    m55m1_tlv320dac3100_board_progress_callback_t progress_callback,
    void *progress_context) {
    m55m1_audio_playback_binding_t binding;
    m55m1_audio_board_init_result_t result;

    if (config == NULL) {
        return M55M1_AUDIO_BOARD_INIT_CLOCK_MISMATCH;
    }
    result = m55m1_audio_board_init(
        (m55m1_audio_profile_t)profile,
        &binding,
        diagnostic_playback_irq,
        NULL,
        progress_callback,
        progress_context);

    if (result != M55M1_AUDIO_BOARD_INIT_OK) {
        return result;
    }
    *config = (tlv320dac3100_config_t){
        .i2c = binding.i2c,
        .i2s = binding.i2s,
        .pdma = binding.pdma,
        .pdma_channel = binding.pdma_channel,
        .profile = profile,
        .max_digital_gain_db = binding.max_digital_gain_db,
        .block_callback = callback,
        .callback_context = callback_context,
    };
    return result;
}

bool m55m1_tlv320dac3100_test_get_clock_snapshot(
    m55m1_tlv320dac3100_clock_snapshot_t *snapshot) {
    return m55m1_audio_get_clock_snapshot(snapshot);
}

bool m55m1_tlv320dac3100_test_dmic_start(
    tlv320dac3100_audio_profile_t profile) {
    return m55m1_audio_dmic_start((m55m1_audio_profile_t)profile);
}

bool m55m1_tlv320dac3100_test_dmic_take_block(
    int16_t *destination,
    uint16_t sample_count) {
    return m55m1_audio_dmic_take_block(destination, sample_count);
}

bool m55m1_tlv320dac3100_test_dmic_take_block_timed(
    int16_t *destination,
    uint16_t sample_count,
    m55m1_tlv320dac3100_dmic_block_timing_t *timing) {
    return m55m1_audio_dmic_take_block_timed(
        destination, sample_count, timing);
}

bool m55m1_tlv320dac3100_test_dmic_block_pending(void) {
    return m55m1_audio_dmic_block_pending();
}

uint32_t m55m1_tlv320dac3100_test_cycles_now(void) {
    return m55m1_timebase_cycles_now();
}

uint32_t m55m1_tlv320dac3100_test_cycles_elapsed(
    uint32_t start_cycle,
    uint32_t end_cycle) {
    return m55m1_timebase_cycles_elapsed(start_cycle, end_cycle);
}

uint32_t m55m1_tlv320dac3100_test_cycles_to_us(uint32_t cycles) {
    return m55m1_timebase_cycles_to_us(cycles);
}

void m55m1_tlv320dac3100_test_dmic_get_status(
    m55m1_tlv320dac3100_dmic_status_t *status) {
    m55m1_audio_dmic_get_status(status);
}
