/**
 * @file test_dac_i2s_uart1_diagnostic_main.c
 * @brief Stage-by-stage TLV320DAC3100 headphone diagnostic using UART1.
 * @author Bui Minh Huy
 * @date 2026-07-20
 *
 * Copy this complete source to `app/main.c` to build the DAC diagnostic. UART1
 * uses PA2 as RX and PA3 as TX at 115200 baud. The two pins are camera-capture
 * data nets on NuMaker GestureAI-M55M1, so do not use this test while a CMOS
 * camera capture is active. It intentionally does not initialise UART5, CCAP,
 * or DMIC.
 */

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "drivers/tlv320dac3100/m55m1_tlv320dac3100_test_compat.h"
#include "drivers/tlv320dac3100/tlv320dac3100.h"
#include "NuMicro.h"

/** Number of completed 10 ms PDMA blocks between compact runtime reports. */
#define DAC_DIAGNOSTIC_REPORT_INTERVAL_BLOCKS  100UL
/** Bytes in the bounded foreground UART format buffer, including its NUL. */
#define DAC_DIAGNOSTIC_UART_LOG_BUFFER_SIZE    96UL
/** The diagnostic waveform is defined for this profile's 10 ms block. */
#define DAC_DIAGNOSTIC_PROFILE                 TLV320DAC3100_AUDIO_PROFILE_16KHZ

/*
 * A 1 kHz sine at 16 kHz has exactly 16 samples per period. Repeating this
 * cycle ten times gives one driver-sized 10 ms block without a phase jump.
 * The initial 6000/32767 full-scale amplitude avoids high first-power-on level.
 */
static const int16_t g_tone_cycle[16] = {
    0, 2296, 4243, 5543, 6000, 5543, 4243, 2296,
    0, -2296, -4243, -5543, -6000, -5543, -4243, -2296,
};

/* The driver copies this foreground-owned PCM data into non-cacheable DMA memory. */
static int16_t g_tone_block[TLV320DAC3100_MONO_SAMPLES_PER_BLOCK];
static char g_uart_log[DAC_DIAGNOSTIC_UART_LOG_BUFFER_SIZE];

/*
 * This is the only ISR-to-foreground hand-off. It is naturally aligned and the
 * callback writes no UART bytes because a blocking serial FIFO wait can exceed
 * the 10 ms interval before the completed PDMA slot must be refilled.
 */
static volatile bool g_tone_block_due;

/**
 * @brief Configure the independent UART1 diagnostic connection on PA2/PA3.
 *
 * PA2 is UART1_RXD and PA3 is UART1_TXD. HIRC is explicitly enabled before
 * selection because early codec diagnostics must remain available even if the
 * external audio-clock setup later fails. UART5 is deliberately untouched:
 * PB4/PB5 belong to I2S0 MCLK/BCLK during this test.
 */
static void dac_diagnostic_uart1_init(void)
{
    SYS_UnlockReg();
    CLK_EnableXtalRC(CLK_SRCCTL_HIRCEN_Msk);
    CLK_WaitClockReady(CLK_STATUS_HIRCSTB_Msk);
    CLK_SetModuleClock(UART1_MODULE, CLK_UARTSEL0_UART1SEL_HIRC,
                       CLK_UARTDIV0_UART1DIV(1));
    CLK_EnableModuleClock(UART1_MODULE);
    CLK_EnableModuleClock(GPIOA_MODULE);
    SET_UART1_RXD_PA2();
    SET_UART1_TXD_PA3();
    SYS_LockReg();

    UART_Open(UART1, 115200U);
}

/**
 * @brief Format and enqueue one bounded foreground-only UART1 log line.
 * @param format printf-style message format stored in program memory.
 * @param ... Arguments referenced by `format`.
 *
 * `UART_Write` waits only when its 16-byte FIFO is full; this function is
 * called after audio-slot refill and the runtime message is kept below 96
 * bytes. It must never run in the PDMA callback or PDMA0 IRQ context.
 */
static void dac_diagnostic_log(const char *format, ...)
{
    va_list arguments;
    int formatted_length;
    uint32_t length;

    va_start(arguments, format);
    formatted_length = vsnprintf(g_uart_log, sizeof(g_uart_log), format, arguments);
    va_end(arguments);

    if ((formatted_length > 0) &&
        ((uint32_t)formatted_length < sizeof(g_uart_log)))
    {
        length = (uint32_t)formatted_length;
        (void)UART_Write(UART1, (const uint8_t *)g_uart_log, length);
    }
}

/**
 * @brief Log and clear the reset source captured before this firmware booted.
 *
 * `RSTSTS` survives the reset that caused this invocation. Clearing precisely
 * the captured bits after logging makes a repeating line identify the newest
 * reset rather than accumulating stale power-on and watchdog causes.
 */
static void dac_diagnostic_log_reset_source(void)
{
    const uint32_t reset_source = SYS_GetResetSrc();

    dac_diagnostic_log("BOOT UART1 PA2=RX PA3=TX 115200 RSTSTS=%08lx\r\n",
                       (unsigned long)reset_source);
    if (reset_source != 0U)
    {
        SYS_UnlockReg();
        SYS_ClearResetSrc(reset_source);
        SYS_LockReg();
    }
}

/**
 * @brief Return the stable UART label for one board-initialisation boundary.
 * @param stage Board-owned boundary reported before PDMA is enabled.
 * @return Constant stage label with static lifetime.
 */
static const char *dac_diagnostic_board_stage_name(
    m55m1_tlv320dac3100_board_stage_t stage)
{
    switch (stage)
    {
        case M55M1_TLV320DAC3100_BOARD_STAGE_ENTER:
            return "ENTER";
        case M55M1_TLV320DAC3100_BOARD_STAGE_SCLK_HIRC_READY:
            return "SCLK_HIRC_READY";
        case M55M1_TLV320DAC3100_BOARD_STAGE_HXT_WAIT:
            return "HXT_WAIT";
        case M55M1_TLV320DAC3100_BOARD_STAGE_HXT_READY:
            return "HXT_READY";
        case M55M1_TLV320DAC3100_BOARD_STAGE_APLL0_WAIT:
            return "APLL0_WAIT";
        case M55M1_TLV320DAC3100_BOARD_STAGE_APLL0_READY:
            return "APLL0_READY";
        case M55M1_TLV320DAC3100_BOARD_STAGE_PERIPHERALS_READY:
            return "PERIPHERALS_READY";
        default:
            return "UNKNOWN";
    }
}

/**
 * @brief Log one synchronous board boundary through UART1.
 * @param stage Board-owned stage being entered or completed.
 * @param context Unused caller context; retained for the board callback API.
 *
 * The board invokes this only before PDMA starts, so the bounded UART write
 * cannot steal time from an audio refill deadline. It never runs in an IRQ.
 */
static void dac_diagnostic_board_progress(
    m55m1_tlv320dac3100_board_stage_t stage, void *context)
{
    (void)context;
    dac_diagnostic_log("BOARD %s s=%08lx\r\n", dac_diagnostic_board_stage_name(stage),
                       (unsigned long)CLK->SCLKSEL);
}

/**
 * @brief Return the stable UART label for a board initialisation result.
 * @param result Clock/peripheral result returned by the board adapter.
 * @return Constant board-result label with static lifetime.
 */
static const char *dac_diagnostic_board_result_name(
    m55m1_tlv320dac3100_board_init_result_t result)
{
    switch (result)
    {
        case M55M1_TLV320DAC3100_BOARD_INIT_OK:
            return "OK";
        case M55M1_TLV320DAC3100_BOARD_INIT_HXT_TIMEOUT:
            return "HXT_TIMEOUT";
        case M55M1_TLV320DAC3100_BOARD_INIT_APLL0_TIMEOUT:
            return "APLL0_TIMEOUT";
        case M55M1_TLV320DAC3100_BOARD_INIT_I2C_STUCK:
            return "I2C_STUCK";
        default:
            return "UNKNOWN";
    }
}

/**
 * @brief Convert a TLV320DAC3100 operation result to a stable log label.
 * @param status Driver result to describe.
 * @return Constant text whose lifetime is the entire program.
 */
static const char *dac_diagnostic_status_name(tlv320dac3100_status_t status)
{
    switch (status)
    {
        case TLV320DAC3100_STATUS_OK:
            return "OK";
        case TLV320DAC3100_STATUS_INVALID_ARGUMENT:
            return "INVALID_ARGUMENT";
        case TLV320DAC3100_STATUS_INVALID_STATE:
            return "INVALID_STATE";
        case TLV320DAC3100_STATUS_BUSY:
            return "BUSY";
        case TLV320DAC3100_STATUS_CLOCK_MISMATCH:
            return "CLOCK_MISMATCH";
        case TLV320DAC3100_STATUS_I2C_ERROR:
            return "I2C_ERROR";
        case TLV320DAC3100_STATUS_DMA_ERROR:
            return "DMA_ERROR";
        default:
            return "UNKNOWN";
    }
}

/**
 * @brief Expand the phase-continuous 1 kHz cycle to one 10 ms PCM block.
 *
 * This happens before DMA begins. The generic driver copies the result into
 * its own stereo non-cacheable slots on every later submission.
 */
static void dac_diagnostic_make_tone_block(void)
{
    uint32_t index;

    for (index = 0U; index < TLV320DAC3100_MONO_SAMPLES_PER_BLOCK; ++index)
    {
        g_tone_block[index] = g_tone_cycle[index & 0x0FU];
    }
}

/**
 * @brief Mark that foreground code may refill a completed PDMA audio slot.
 * @param context Unused board-provided callback context.
 *
 * This executes in PDMA0 IRQ context. The generic driver has already switched
 * the completed descriptor to silence before this callback is reached, which
 * makes a late foreground refill safe but increments the driver's underrun
 * counter. No I2C, formatted logging, or block submission is permitted here.
 */
static void dac_diagnostic_block_callback(void *context)
{
    (void)context;
    g_tone_block_due = true;
}

/**
 * @brief Atomically consume one PDMA-completion notification.
 * @return True when foreground code owns one pending refill notification.
 *
 * PDMA0 may interrupt between a normal C read and write of
 * `g_tone_block_due`. Masking interrupts only for this read-and-clear pair
 * prevents foreground code from erasing a notification raised in that window.
 * The saved PRIMASK preserves a caller's prior interrupt-masking state; the
 * slower PCM submission remains in foreground with interrupts enabled.
 */
static bool dac_diagnostic_take_block_due(void)
{
    uint32_t primask;
    bool block_due;

    primask = __get_PRIMASK();
    __disable_irq();
    block_due = g_tone_block_due;
    g_tone_block_due = false;
    __set_PRIMASK(primask);
    return block_due;
}

/**
 * @brief Stop at a named failed stage while keeping UART1 available for capture.
 * @param stage Stage label that failed, such as `CODEC_INIT`.
 * @param status Driver error returned by that stage.
 * @return Never returns.
 *
 * A deliberate non-resetting stop preserves the final stage line on UART. It
 * is used only before playback starts, so it cannot hide a PDMA timing issue.
 */
static void dac_diagnostic_halt(const char *stage, tlv320dac3100_status_t status)
{
    dac_diagnostic_log("%s HALT %s(%d)\r\n", stage,
                       dac_diagnostic_status_name(status), (int)status);

    for (;;)
    {
    }
}

/**
 * @brief Stop after a board-clock failure without passing invalid config to I2C.
 * @param result Non-OK result returned by the board initialisation adapter.
 * @return Never returns.
 *
 * The board leaves `config` uninitialised on a clock failure. Remaining in a
 * UART-visible foreground loop preserves the causal result and avoids a later
 * driver fault obscuring the HXT/APLL0 boundary.
 */
static void dac_diagnostic_board_halt(m55m1_tlv320dac3100_board_init_result_t result)
{
    dac_diagnostic_log("BOARD_RESULT %s(%d) HALT\r\n",
                       dac_diagnostic_board_result_name(result), (int)result);

    for (;;)
    {
    }
}

/**
 * @brief Run the UART1 stage-by-stage TLV320DAC3100 1 kHz tone diagnostic.
 * @return Never returns.
 *
 * The expected successful sequence is BOOT, UART_READY, BOARD_READY,
 * CODEC_INIT, CODEC_CONTROL, AUDIO_QUEUE, PLAYING, then recurring RUNNING
 * snapshots. After PLAYING, wait two seconds for codec de-pop settling before
 * deciding whether the headphones are silent.
 */
int main(void)
{
    tlv320dac3100_config_t config;
    tlv320dac3100_status_t init_status;
    tlv320dac3100_status_t gain_status;
    tlv320dac3100_status_t first_queue_status;
    tlv320dac3100_status_t second_queue_status;
    tlv320dac3100_status_t start_status;
    tlv320dac3100_driver_status_t driver_status;
    m55m1_tlv320dac3100_board_init_result_t board_result;
    uint32_t next_report_block = DAC_DIAGNOSTIC_REPORT_INTERVAL_BLOCKS;

    dac_diagnostic_uart1_init();
    dac_diagnostic_log_reset_source();
    dac_diagnostic_log("UART_READY\r\n");

    dac_diagnostic_make_tone_block();
    board_result = m55m1_tlv320dac3100_test_board_init_with_progress(
        DAC_DIAGNOSTIC_PROFILE, &config, dac_diagnostic_block_callback, NULL,
        dac_diagnostic_board_progress, NULL);
    if (board_result != M55M1_TLV320DAC3100_BOARD_INIT_OK)
    {
        dac_diagnostic_board_halt(board_result);
    }
    dac_diagnostic_log("BOARD_RESULT %s(%d)\r\n",
                       dac_diagnostic_board_result_name(board_result), (int)board_result);
    dac_diagnostic_log("BOARD_READY\r\n");
    /* Idle-high proves neither I2C pin is held low before the codec probe begins. */
    dac_diagnostic_log("I2C_LINES sda=%u scl=%u\r\n", (unsigned)PB0, (unsigned)PB1);
    dac_diagnostic_log("I2C_REGS c=%08lx s=%02lx d=%02lx div=%08lx\r\n",
                       (unsigned long)I2C1->CTL0, (unsigned long)I2C1->STATUS0,
                       (unsigned long)I2C1->DAT, (unsigned long)I2C1->CLKDIV);

    init_status = tlv320dac3100_init(&config);
    tlv320dac3100_get_status(&driver_status);
    dac_diagnostic_log("CODEC_INIT %s(%d) p=%u r=%02x ph=%u a=%u n=%lu hs=%02x hc=%08lx\r\n",
                       dac_diagnostic_status_name(init_status), (int)init_status,
                       (unsigned)driver_status.last_i2c_page,
                       (unsigned)driver_status.last_i2c_register,
                       (unsigned)driver_status.last_i2c_phase,
                       (unsigned)driver_status.last_i2c_attempts,
                       (unsigned long)driver_status.i2c_failures,
                       (unsigned)driver_status.last_i2c_status,
                       (unsigned long)driver_status.last_i2c_control);
    if (init_status != TLV320DAC3100_STATUS_OK)
    {
        dac_diagnostic_halt("CODEC_INIT", init_status);
    }

    gain_status = tlv320dac3100_set_digital_gain_db(-12);
    dac_diagnostic_log("CODEC_CONTROL %s(%d)\r\n",
                       dac_diagnostic_status_name(gain_status), (int)gain_status);
    if (gain_status != TLV320DAC3100_STATUS_OK)
    {
        dac_diagnostic_halt("CODEC_CONTROL", gain_status);
    }

    first_queue_status = tlv320dac3100_submit_mono_block(
        g_tone_block, TLV320DAC3100_16KHZ_SAMPLES_PER_BLOCK);
    second_queue_status = tlv320dac3100_submit_mono_block(
        g_tone_block, TLV320DAC3100_16KHZ_SAMPLES_PER_BLOCK);
    dac_diagnostic_log("AUDIO_QUEUE a=%s(%d) b=%s(%d)\r\n",
                       dac_diagnostic_status_name(first_queue_status),
                       (int)first_queue_status,
                       dac_diagnostic_status_name(second_queue_status),
                       (int)second_queue_status);
    if ((first_queue_status != TLV320DAC3100_STATUS_OK) ||
        (second_queue_status != TLV320DAC3100_STATUS_OK))
    {
        dac_diagnostic_halt("AUDIO_QUEUE",
                            (first_queue_status != TLV320DAC3100_STATUS_OK)
                                ? first_queue_status
                                : second_queue_status);
    }

    start_status = tlv320dac3100_start();
    dac_diagnostic_log("PLAYING %s(%d); wait 2 s for de-pop\r\n",
                       dac_diagnostic_status_name(start_status), (int)start_status);
    if (start_status != TLV320DAC3100_STATUS_OK)
    {
        dac_diagnostic_halt("PLAYING", start_status);
    }

    for (;;)
    {
        if (dac_diagnostic_take_block_due())
        {
            /*
             * Refill first. A BUSY outcome only means the two ping-pong slots
             * are currently driver-owned; the next completion will retry and
             * the driver's underrun count remains the authoritative evidence.
             */
            (void)tlv320dac3100_submit_mono_block(
                g_tone_block, TLV320DAC3100_16KHZ_SAMPLES_PER_BLOCK);
        }

        tlv320dac3100_get_status(&driver_status);
        if (driver_status.completed_blocks >= next_report_block)
        {
            /*
             * Do not delay here: a blocking 1-second wait would lose roughly
             * one hundred 10 ms refill deadlines. This compact line is emitted
             * only after the foreground code has serviced a pending refill.
             */
            next_report_block += DAC_DIAGNOSTIC_REPORT_INTERVAL_BLOCKS;
            dac_diagnostic_log(
                "RUNNING d=%lu u=%lu e=%lu i=%08lx p=%08lx t=%08lx\r\n",
                (unsigned long)driver_status.completed_blocks,
                (unsigned long)driver_status.underrun_blocks,
                (unsigned long)driver_status.dma_errors,
                (unsigned long)I2S0->STATUS0,
                (unsigned long)PDMA0->TDSTS,
                (unsigned long)PDMA0->TRGSTS);
        }
    }
}
