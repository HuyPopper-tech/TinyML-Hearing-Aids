/**
 * @file test_dmic_uart5_main.c
 * @brief Standalone MP34DT05 capture diagnostic with UART5 reporting.
 * @author Bui Minh Huy
 * @date 2026-07-20
 *
 * Copy this entire file to `app/main.c` to build the microphone test. It owns
 * PB5 as UART5 TX and PB4 as UART5 RX, so it deliberately does not initialise
 * I2S0 or the TLV320DAC3100 playback driver.
 */

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "drivers/tlv320dac3100/m55m1_tlv320dac3100_test_compat.h"
#include "NuMicro.h"

/** UART5 baud rate for the host-terminal microphone diagnostics. */
#define DMIC_UART5_BAUD                 115200UL
/** Number of 10 ms capture blocks between UART reports (250 ms). */
#define DMIC_LOG_INTERVAL_BLOCKS        25UL
/** Storage for one formatted UART status line, including the trailing NUL. */
#define DMIC_UART_LOG_BUFFER_SIZE       96UL
/** Select a complete capture profile before DMIC/LPPDMA is armed. */
#define DMIC_UART_PROFILE               TLV320DAC3100_AUDIO_PROFILE_16KHZ

/* The board owns DMA storage; this foreground block is safe to inspect and reuse. */
static int16_t g_dmic_block[TLV320DAC3100_MAX_SAMPLES_PER_BLOCK];
static char g_uart_log[DMIC_UART_LOG_BUFFER_SIZE];

/**
 * @brief Configure UART5 on the PB5/PB4 board header connection.
 *
 * PB5 is TXD and PB4 is RXD at 115200 baud. This assignment must not coexist
 * with the DAC I2S0 test because that test maps the same pins to BCLK/MCLK.
 */
static void dmic_uart5_init(void)
{
    SYS_UnlockReg();
    CLK_EnableXtalRC(CLK_SRCCTL_HIRCEN_Msk);
    CLK_WaitClockReady(CLK_STATUS_HIRCSTB_Msk);
    CLK_SetModuleClock(UART5_MODULE, CLK_UARTSEL0_UART5SEL_HIRC,
                       CLK_UARTDIV0_UART5DIV(1));
    CLK_EnableModuleClock(UART5_MODULE);
    CLK_EnableModuleClock(GPIOB_MODULE);
    SET_UART5_TXD_PB5();
    SET_UART5_RXD_PB4();
    SYS_LockReg();

    UART_Open(UART5, DMIC_UART5_BAUD);
}

/**
 * @brief Return the non-overflowing absolute magnitude of one PCM sample.
 * @param sample Signed-16 microphone sample.
 * @return Magnitude in the inclusive range 0..32768.
 */
static uint32_t dmic_sample_magnitude(int16_t sample)
{
    return (sample < 0) ? (uint32_t)(-(int32_t)sample) : (uint32_t)sample;
}

/**
 * @brief Format and transmit one aggregate capture report.
 * @param blocks Total captured 10 ms blocks at the reporting instant.
 *
 * This function is called only in foreground context. The short line is sent
 * every 250 ms, far less often than the 10 ms LPPDMA capture deadline.
 */
static void dmic_log_capture(uint32_t blocks, uint16_t sample_count)
{
    int16_t minimum = INT16_MAX;
    int16_t maximum = INT16_MIN;
    uint32_t peak = 0U;
    uint32_t sum_magnitude = 0U;
    uint32_t index;
    int formatted_length;
    uint32_t length;

    for (index = 0U; index < sample_count; ++index)
    {
        const int16_t sample = g_dmic_block[index];
        const uint32_t magnitude = dmic_sample_magnitude(sample);

        if (sample < minimum)
        {
            minimum = sample;
        }
        if (sample > maximum)
        {
            maximum = sample;
        }
        if (magnitude > peak)
        {
            peak = magnitude;
        }
        sum_magnitude += magnitude;
    }

    formatted_length = snprintf(g_uart_log, sizeof(g_uart_log),
                                "DMIC b=%lu min=%d max=%d peak=%lu mean=%lu\r\n",
                                (unsigned long)blocks, (int)minimum, (int)maximum,
                                (unsigned long)peak,
                                (unsigned long)(sum_magnitude /
                                                sample_count));
    if ((formatted_length > 0) && ((uint32_t)formatted_length < sizeof(g_uart_log)))
    {
        length = (uint32_t)formatted_length;
        (void)UART_Write(UART5, (const uint8_t *)g_uart_log, length);
        UART_WAIT_TX_EMPTY(UART5);
    }
}

/**
 * @brief Continuously capture 16 kHz DMIC blocks and report their level.
 * @return Never returns.
 *
 * Near-zero `min`, `max`, `peak`, and `mean` values indicate no usable DMIC
 * signal. Speaking or tapping near the microphone should increase `peak` and
 * `mean`, and normally produces both negative and positive sample values.
 */
int main(void)
{
    const uint16_t sample_count = tlv320dac3100_profile_samples_per_block(DMIC_UART_PROFILE);
    uint32_t captured_blocks = 0U;

    dmic_uart5_init();
    if ((sample_count == 0U) || !m55m1_tlv320dac3100_test_dmic_start(DMIC_UART_PROFILE))
    {
        const uint8_t failure[] = "DMIC start failed\r\n";

        (void)UART_Write(UART5, failure, sizeof(failure) - 1U);
        UART_WAIT_TX_EMPTY(UART5);
        for (;;)
        {
        }
    }

    for (;;)
    {
        if (m55m1_tlv320dac3100_test_dmic_take_block(g_dmic_block, sample_count))
        {
            ++captured_blocks;
            if ((captured_blocks % DMIC_LOG_INTERVAL_BLOCKS) == 0U)
            {
                dmic_log_capture(captured_blocks, sample_count);
            }
        }
    }
}
