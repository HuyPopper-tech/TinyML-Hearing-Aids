/**
 * @file main.c
 * @brief Normal low-latency MP34DT05-to-TLV320DAC3100 passthrough firmware.
 * @author Bui Minh Huy
 * @date 2026-07-21
 *
 * This is the TinyML audio-transport baseline. It deliberately contains no
 * UART logging, DSP, AGC, or inference work: applications add processing
 * between capture and submit while preserving the selected 10 ms profile.
 */

#include <stdint.h>

#include "board/m55m1_tlv320dac3100_test.h"
#include "drivers/tlv320dac3100/tlv320dac3100.h"

/** Change this init-time selection when validating the 32 kHz transport profile. */
#define APP_AUDIO_PROFILE TLV320DAC3100_AUDIO_PROFILE_32KHZ

static int16_t g_dmic_block[TLV320DAC3100_MAX_SAMPLES_PER_BLOCK];

/** @brief Stop safely before audio starts when a mandatory hardware boundary failed. */
static void dmic_passthrough_halt(void)
{
    for (;;) { }
}

/** @brief Capture one 10 ms microphone block and forward it unchanged to headphone playback. */
int main(void)
{
    const tlv320dac3100_audio_profile_t profile = APP_AUDIO_PROFILE;
    const uint16_t sample_count = tlv320dac3100_profile_samples_per_block(profile);
    tlv320dac3100_config_t config;
    tlv320dac3100_status_t status;

    if ((sample_count == 0U) ||
        (m55m1_tlv320dac3100_test_board_init(profile, &config, NULL, NULL) !=
         M55M1_TLV320DAC3100_BOARD_INIT_OK))
    {
        dmic_passthrough_halt();
    }
    if (tlv320dac3100_init(&config) != TLV320DAC3100_STATUS_OK)
    {
        dmic_passthrough_halt();
    }
    if (tlv320dac3100_start() != TLV320DAC3100_STATUS_OK)
    {
        dmic_passthrough_halt();
    }
    if (!m55m1_tlv320dac3100_test_dmic_start(profile))
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
