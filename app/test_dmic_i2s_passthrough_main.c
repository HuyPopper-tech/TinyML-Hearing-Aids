/**
 * @file test_dmic_i2s_passthrough_main.c
 * @brief Standalone normal 16 kHz or 32 kHz DMIC-to-headphone passthrough test.
 * @author Bui Minh Huy
 * @date 2026-07-21
 *
 * Copy this source to app/main.c when an isolated transport test is preferred
 * over the product application's TinyML processing pipeline.
 */

#include <stdint.h>

#include "drivers/tlv320dac3100/m55m1_tlv320dac3100_test_compat.h"
#include "drivers/tlv320dac3100/tlv320dac3100.h"

#define DMIC_PASSTHROUGH_PROFILE TLV320DAC3100_AUDIO_PROFILE_16KHZ

static int16_t g_dmic_block[TLV320DAC3100_MAX_SAMPLES_PER_BLOCK];

/** @brief Halt only after a pre-playback setup error or impossible driver state. */
static void dmic_passthrough_halt(void)
{
    for (;;) { }
}

/** @brief Run profile-matched microphone capture and headphone output without UART traffic. */
int main(void)
{
    const tlv320dac3100_audio_profile_t profile = DMIC_PASSTHROUGH_PROFILE;
    const uint16_t sample_count = tlv320dac3100_profile_samples_per_block(profile);
    tlv320dac3100_config_t config;
    tlv320dac3100_status_t status;

    if ((sample_count == 0U) ||
        (m55m1_tlv320dac3100_test_board_init(profile, &config, NULL, NULL) !=
         M55M1_TLV320DAC3100_BOARD_INIT_OK) ||
        (tlv320dac3100_init(&config) != TLV320DAC3100_STATUS_OK) ||
        (tlv320dac3100_start() != TLV320DAC3100_STATUS_OK) ||
        !m55m1_tlv320dac3100_test_dmic_start(profile))
    {
        dmic_passthrough_halt();
    }

    for (;;)
    {
        if (m55m1_tlv320dac3100_test_dmic_take_block(g_dmic_block, sample_count))
        {
            status = tlv320dac3100_submit_mono_block(g_dmic_block, sample_count);
            if ((status != TLV320DAC3100_STATUS_OK) &&
                (status != TLV320DAC3100_STATUS_BUSY))
            {
                dmic_passthrough_halt();
            }
        }
    }
}
