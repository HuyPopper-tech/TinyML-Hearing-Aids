/**
 * @file test_dac_i2s_tone_main.c
 * @brief Standalone 1 kHz TLV320DAC3100 headphone-output diagnostic.
 * @author Bui Minh Huy
 * @date 2026-07-20
 *
 * Copy this entire file to `app/main.c` to build the DAC test. It maps I2S0
 * BCLK/MCLK to PB5/PB4 and therefore deliberately does not initialise UART5.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "drivers/tlv320dac3100/m55m1_tlv320dac3100_test_compat.h"
#include "drivers/tlv320dac3100/tlv320dac3100.h"

/** Frequency of the generated sine-wave diagnostic tone. */
#define TLV320DAC3100_TONE_HZ           1000UL
/** Peak signed-16 amplitude, chosen for a comfortable initial headphone level. */
#define TLV320DAC3100_TONE_AMPLITUDE    6000
#define TLV320DAC3100_TONE_PROFILE      TLV320DAC3100_AUDIO_PROFILE_16KHZ

/*
 * At 16 kHz, one 1 kHz period is exactly 16 samples. Ten periods make one
 * driver block, so the static waveform loops without a phase discontinuity.
 */
static const int16_t g_tone_block[TLV320DAC3100_MONO_SAMPLES_PER_BLOCK] = {
    0, 2296, 4243, 5543, TLV320DAC3100_TONE_AMPLITUDE, 5543, 4243, 2296,
    0, -2296, -4243, -5543, -TLV320DAC3100_TONE_AMPLITUDE, -5543, -4243, -2296,
    0, 2296, 4243, 5543, TLV320DAC3100_TONE_AMPLITUDE, 5543, 4243, 2296,
    0, -2296, -4243, -5543, -TLV320DAC3100_TONE_AMPLITUDE, -5543, -4243, -2296,
    0, 2296, 4243, 5543, TLV320DAC3100_TONE_AMPLITUDE, 5543, 4243, 2296,
    0, -2296, -4243, -5543, -TLV320DAC3100_TONE_AMPLITUDE, -5543, -4243, -2296,
    0, 2296, 4243, 5543, TLV320DAC3100_TONE_AMPLITUDE, 5543, 4243, 2296,
    0, -2296, -4243, -5543, -TLV320DAC3100_TONE_AMPLITUDE, -5543, -4243, -2296,
    0, 2296, 4243, 5543, TLV320DAC3100_TONE_AMPLITUDE, 5543, 4243, 2296,
    0, -2296, -4243, -5543, -TLV320DAC3100_TONE_AMPLITUDE, -5543, -4243, -2296,
    0, 2296, 4243, 5543, TLV320DAC3100_TONE_AMPLITUDE, 5543, 4243, 2296,
    0, -2296, -4243, -5543, -TLV320DAC3100_TONE_AMPLITUDE, -5543, -4243, -2296,
    0, 2296, 4243, 5543, TLV320DAC3100_TONE_AMPLITUDE, 5543, 4243, 2296,
    0, -2296, -4243, -5543, -TLV320DAC3100_TONE_AMPLITUDE, -5543, -4243, -2296,
    0, 2296, 4243, 5543, TLV320DAC3100_TONE_AMPLITUDE, 5543, 4243, 2296,
    0, -2296, -4243, -5543, -TLV320DAC3100_TONE_AMPLITUDE, -5543, -4243, -2296,
    0, 2296, 4243, 5543, TLV320DAC3100_TONE_AMPLITUDE, 5543, 4243, 2296,
    0, -2296, -4243, -5543, -TLV320DAC3100_TONE_AMPLITUDE, -5543, -4243, -2296,
};

/* Set by PDMA IRQ and consumed by foreground code; no I2C or logging in the ISR. */
static volatile bool g_tone_block_due;

/**
 * @brief Mark that one completed PDMA slot can accept the next tone block.
 * @param context Unused callback context supplied by the board adapter.
 *
 * This runs in PDMA0 IRQ context. It only writes a naturally aligned flag;
 * foreground code performs the buffer copy before the next 10 ms deadline.
 */
static void tlv320dac3100_tone_block_callback(void *context)
{
    (void)context;
    g_tone_block_due = true;
}

/**
 * @brief Play a continuous 1 kHz tone through the configured headphones.
 * @return Never returns.
 *
 * The two submissions before `start` prefill both PDMA descriptors. Each IRQ
 * then permits foreground code to refill one completed descriptor with the
 * same phase-continuous 10 ms PCM block.
 */
int main(void)
{
    tlv320dac3100_config_t config;

    m55m1_tlv320dac3100_test_board_init(
        TLV320DAC3100_TONE_PROFILE, &config, tlv320dac3100_tone_block_callback, NULL);
    if (tlv320dac3100_init(&config) != TLV320DAC3100_STATUS_OK)
    {
        for (;;)
        {
        }
    }
    /* The tone remains below the board's -12 dB hearing-aid safety ceiling. */
    if (tlv320dac3100_set_digital_gain_db(-12) != TLV320DAC3100_STATUS_OK)
    {
        for (;;)
        {
        }
    }

    (void)tlv320dac3100_submit_mono_block(
        g_tone_block, TLV320DAC3100_16KHZ_SAMPLES_PER_BLOCK);
    (void)tlv320dac3100_submit_mono_block(
        g_tone_block, TLV320DAC3100_16KHZ_SAMPLES_PER_BLOCK);
    if (tlv320dac3100_start() != TLV320DAC3100_STATUS_OK)
    {
        for (;;)
        {
        }
    }

    for (;;)
    {
        if (g_tone_block_due)
        {
            g_tone_block_due = false;
            /* BUSY is safe here: the driver switches an unfilled slot to silence. */
            (void)tlv320dac3100_submit_mono_block(
                g_tone_block, TLV320DAC3100_16KHZ_SAMPLES_PER_BLOCK);
        }
    }
}
