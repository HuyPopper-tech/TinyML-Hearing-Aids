/**
 * @file test_dac_i2s_led_diagnostic_main.c
 * @brief PA7 LED stage and fault diagnostic for TLV320DAC3100 I2S playback.
 * @author Bui Minh Huy
 * @date 2026-07-20
 *
 * Copy this complete source to `app/main.c` to build the LED diagnostic.
 * PA7 drives the on-board yellow LED and is active-low. This source preserves
 * the fixed I2S0/I2C1/PDMA0 playback test and uses no serial diagnostic path.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "drivers/tlv320dac3100/m55m1_tlv320dac3100_test_compat.h"
#include "drivers/tlv320dac3100/tlv320dac3100.h"
#include "NuMicro.h"

/** Duration of one visible LED on or off pulse segment. */
#define LED_PULSE_MS                     150UL
/** Gap separating the stage and cause parts of a fault frame. */
#define LED_FAULT_SECTION_GAP_MS         400UL
/** Gap separating successive repeated fault frames. */
#define LED_FAULT_FRAME_GAP_MS           1000UL
/** Healthy LED toggle interval; it produces a visible 1 Hz full heartbeat. */
#define LED_HEARTBEAT_HALF_PERIOD_MS     500UL
/** No PDMA completion for this long after start is a playback stall. */
#define LED_PLAYBACK_STALL_MS             100UL
/** The LED tone is a phase-continuous 160-sample waveform for this profile. */
#define DAC_LED_DIAGNOSTIC_PROFILE         TLV320DAC3100_AUDIO_PROFILE_16KHZ

/** Fault stage: HXT/APLL0 clock setup and fixed audio peripheral binding. */
#define LED_STAGE_BOARD_CLOCK             1U
/** Fault stage: codec initialisation, including I2C setup. */
#define LED_STAGE_CODEC_INIT              2U
/** Fault stage: digital gain configuration. */
#define LED_STAGE_CODEC_CONTROL           3U
/** Fault stage: initial two-block PCM queueing. */
#define LED_STAGE_AUDIO_QUEUE             4U
/** Fault stage: playback start. */
#define LED_STAGE_PLAYING                 5U
/** Fault stage: PDMA/I2S runtime health monitoring. */
#define LED_STAGE_RUNTIME                 6U

/** Failure cause: TLV320DAC3100_STATUS_INVALID_ARGUMENT. */
#define LED_CAUSE_INVALID_ARGUMENT       1U
/** Failure cause: TLV320DAC3100_STATUS_INVALID_STATE. */
#define LED_CAUSE_INVALID_STATE           2U
/** Failure cause: TLV320DAC3100_STATUS_BUSY. */
#define LED_CAUSE_BUSY                    3U
/** Failure cause: TLV320DAC3100_STATUS_CLOCK_MISMATCH. */
#define LED_CAUSE_CLOCK_MISMATCH          4U
/** Failure cause: TLV320DAC3100_STATUS_I2C_ERROR. */
#define LED_CAUSE_I2C_ERROR               5U
/** Failure cause: driver PDMA error counter or STATUS_DMA_ERROR. */
#define LED_CAUSE_DMA_ERROR               6U
/** Failure cause: driver underrun counter advanced. */
#define LED_CAUSE_UNDERRUN                7U
/** Failure cause: no completed PDMA block for LED_PLAYBACK_STALL_MS. */
#define LED_CAUSE_STALL                   8U
/** Failure cause: external 24 MHz HXT did not become ready in the bounded wait. */
#define LED_CAUSE_HXT_TIMEOUT             9U
/** Failure cause: APLL0 did not lock after the bounded SDK wait. */
#define LED_CAUSE_APLL_TIMEOUT            10U

/** Foreground LED machine modes. */
typedef enum {
    LED_MODE_STARTUP,
    LED_MODE_HEARTBEAT,
    LED_MODE_FAULT,
} dac_led_mode_t;

/** Fine-grained non-blocking phases for the repeated fault frame. */
typedef enum {
    LED_FAULT_STAGE_ON,
    LED_FAULT_STAGE_OFF,
    LED_FAULT_SECTION_GAP,
    LED_FAULT_CAUSE_ON,
    LED_FAULT_CAUSE_OFF,
    LED_FAULT_FRAME_GAP,
} dac_led_fault_phase_t;

/**
 * @brief Complete LED state owned only by foreground code.
 *
 * SysTick IRQ increments g_tick_ms but never accesses this state. Foreground
 * code updates PA7 from scheduled deadlines, which avoids a delay in the 10 ms
 * DMA refill path.
 */
typedef struct {
    dac_led_mode_t mode;
    dac_led_fault_phase_t fault_phase;
    uint8_t stage;
    uint8_t cause;
    uint8_t pulses_remaining;
    bool led_on;
    uint32_t deadline_ms;
} dac_led_state_t;

/*
 * One 1 kHz period at 16 kHz has 16 samples. This repeating cycle makes the
 * driver-sized 160-sample block phase continuous and starts at a safe level.
 */
static const int16_t g_tone_cycle[16] = {
    0, 2296, 4243, 5543, 6000, 5543, 4243, 2296,
    0, -2296, -4243, -5543, -6000, -5543, -4243, -2296,
};

/* The driver copies this foreground-owned data into its non-cacheable slots. */
static int16_t g_tone_block[TLV320DAC3100_MONO_SAMPLES_PER_BLOCK];

/* PDMA callback writes this notification only; foreground atomically consumes it. */
static volatile bool g_tone_block_due;

/* SysTick IRQ writes this natural-width counter only; foreground reads it. */
static volatile uint32_t g_tick_ms;
static dac_led_state_t g_led_state;

/**
 * @brief Increment the 1 ms LED scheduler timebase.
 *
 * No GPIO write, driver call, PCM submission, or fault decision occurs here.
 * Keeping this handler bounded prevents it from delaying the higher-priority
 * PDMA completion IRQ or the foreground audio refill hand-off.
 */
void SysTick_Handler(void)
{
    ++g_tick_ms;
}

/**
 * @brief Drive the PA7 user LED with its board-specific active-low polarity.
 * @param enabled True to illuminate the LED; false to turn it off.
 *
 * The schematic routes VDD through R23 and the LED into PA7, so PA7 must sink
 * current for illumination. This helper centralises polarity to avoid inverted
 * diagnostic interpretation.
 */
static void dac_led_set(bool enabled)
{
    if (enabled)
    {
        PA7 = 0U;
    }
    else
    {
        PA7 = 1U;
    }
    g_led_state.led_on = enabled;
}

/**
 * @brief Configure PA7 as the isolated active-low diagnostic LED output.
 *
 * GPIOA is enabled independently of the board audio adapter. PA7 is not an
 * I2S, I2C, or PDMA pin in the fixed TLV320DAC3100 test profile.
 */
static void dac_led_init(void)
{
    SYS_UnlockReg();
    CLK_EnableModuleClock(GPIOA_MODULE);
    SET_GPIO_PA7();
    SYS_LockReg();

    GPIO_SetMode(PA, BIT7, GPIO_MODE_OUTPUT);
    PA7 = 1U;
    g_led_state = (dac_led_state_t){0};
}

/**
 * @brief Emit a visible pre-play stage-success pulse group.
 * @param count Number of 150 ms active-low pulses to show.
 *
 * This helper is called only before PDMA playback starts, when no 10 ms audio
 * deadline exists. Runtime diagnostics must instead use dac_led_update.
 */
static void dac_led_emit_startup_pulses(uint8_t count)
{
    uint8_t pulse;

    for (pulse = 0U; pulse < count; ++pulse)
    {
        PA7 = 0U;
        CLK_SysTickDelay(LED_PULSE_MS * 1000UL);
        PA7 = 1U;
        CLK_SysTickDelay(LED_PULSE_MS * 1000UL);
    }
}

/**
 * @brief Start the 1 ms timebase after all blocking pre-play delays are done.
 *
 * CLK_SysTickDelay temporarily owns SysTick during codec setup and startup
 * pulse groups. SysTick_Config is therefore called only after the final
 * playback-start decision or when entering a pre-play permanent fault.
 */
static void dac_led_start_tick(void)
{
    g_tick_ms = 0U;
    (void)SysTick_Config(SystemCoreClock / 1000UL);
}

/**
 * @brief Return true when an unsigned deadline has expired across tick wrap.
 * @param now_ms Current g_tick_ms snapshot.
 * @param deadline_ms Scheduled event deadline.
 * @return True when now_ms is at or beyond deadline_ms.
 */
static bool dac_led_deadline_reached(uint32_t now_ms, uint32_t deadline_ms)
{
    return ((int32_t)(now_ms - deadline_ms) >= 0);
}

/**
 * @brief Build the fixed tone block before any DMA descriptor owns it.
 */
static void dac_led_make_tone_block(void)
{
    uint32_t index;

    for (index = 0U; index < TLV320DAC3100_MONO_SAMPLES_PER_BLOCK; ++index)
    {
        g_tone_block[index] = g_tone_cycle[index & 0x0FU];
    }
}

/**
 * @brief Mark that foreground code may refill one completed PDMA audio slot.
 * @param context Unused board callback context.
 *
 * The driver has already returned the completed descriptor to silence. This
 * callback must not update LED state because its execution is in PDMA0 IRQ
 * context; foreground handles both refill and diagnostics.
 */
static void dac_led_block_callback(void *context)
{
    (void)context;
    g_tone_block_due = true;
}

/**
 * @brief Atomically consume one pending PDMA completion notification.
 * @return True when foreground owns a refill notification.
 *
 * Without this PRIMASK-protected read-and-clear, an IRQ between a normal read
 * and clear could be lost and create a misleading underrun. The short critical
 * section does not include the slower PCM submission.
 */
static bool dac_led_take_block_due(void)
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
 * @brief Convert a driver operation failure to its approved LED cause code.
 * @param status Non-OK TLV320DAC3100 driver result.
 * @return Cause pulse count in the range LED_CAUSE_INVALID_ARGUMENT..DMA_ERROR.
 */
static uint8_t dac_led_cause_from_status(tlv320dac3100_status_t status)
{
    switch (status)
    {
        case TLV320DAC3100_STATUS_INVALID_ARGUMENT:
            return LED_CAUSE_INVALID_ARGUMENT;
        case TLV320DAC3100_STATUS_INVALID_STATE:
            return LED_CAUSE_INVALID_STATE;
        case TLV320DAC3100_STATUS_BUSY:
            return LED_CAUSE_BUSY;
        case TLV320DAC3100_STATUS_CLOCK_MISMATCH:
            return LED_CAUSE_CLOCK_MISMATCH;
        case TLV320DAC3100_STATUS_I2C_ERROR:
            return LED_CAUSE_I2C_ERROR;
        case TLV320DAC3100_STATUS_DMA_ERROR:
            return LED_CAUSE_DMA_ERROR;
        default:
            return LED_CAUSE_INVALID_ARGUMENT;
    }
}

/**
 * @brief Convert a board-clock result to a distinct visible LED cause code.
 * @param result Result returned by the board-owned clock/pin setup.
 * @return HXT or APLL timeout cause; unexpected values use invalid argument.
 *
 * This mapping separates the two physical clock boundaries before I2C or the
 * codec is touched, so a silent first startup group no longer hides them.
 */
static uint8_t dac_led_cause_from_board_init_result(
    m55m1_tlv320dac3100_board_init_result_t result)
{
    switch (result)
    {
        case M55M1_TLV320DAC3100_BOARD_INIT_HXT_TIMEOUT:
            return LED_CAUSE_HXT_TIMEOUT;
        case M55M1_TLV320DAC3100_BOARD_INIT_APLL0_TIMEOUT:
            return LED_CAUSE_APLL_TIMEOUT;
        case M55M1_TLV320DAC3100_BOARD_INIT_I2C_STUCK:
            return LED_CAUSE_I2C_ERROR;
        default:
            return LED_CAUSE_INVALID_ARGUMENT;
    }
}

/**
 * @brief Latch the first fault and initialise its non-blocking repeated frame.
 * @param stage Stage pulse count in the range 1..6.
 * @param cause Cause pulse count in the range 1..10.
 *
 * First-fault latching avoids alternating patterns when a stalled stream also
 * produces later underruns. The resulting visible code preserves the earliest
 * boundary failure for manual diagnosis.
 */
static void dac_led_latch_fault(uint8_t stage, uint8_t cause)
{
    if (g_led_state.mode == LED_MODE_FAULT)
    {
        return;
    }

    g_led_state.mode = LED_MODE_FAULT;
    g_led_state.stage = stage;
    g_led_state.cause = cause;
    g_led_state.pulses_remaining = stage;
    g_led_state.fault_phase = LED_FAULT_STAGE_ON;
    g_led_state.deadline_ms = g_tick_ms;
    dac_led_set(false);
}

/**
 * @brief Start the non-blocking six-pulse playback-success sequence.
 *
 * After the sequence completes, the state machine changes to the regular
 * heartbeat. This avoids a blocking five-pulse delay after PDMA starts.
 */
static void dac_led_begin_playback_indication(void)
{
    g_led_state.mode = LED_MODE_STARTUP;
    g_led_state.pulses_remaining = LED_STAGE_RUNTIME;
    g_led_state.deadline_ms = g_tick_ms;
    dac_led_set(false);
}

/**
 * @brief Advance the scheduled LED state without waiting.
 * @param now_ms Foreground snapshot of g_tick_ms.
 *
 * In healthy mode the LED toggles every 500 ms. Fault mode repeats stage
 * pulses, a 400 ms section gap, cause pulses, and a one-second frame gap.
 * The function has no loops proportional to time and never calls a driver API.
 */
static void dac_led_update(uint32_t now_ms)
{
    if (!dac_led_deadline_reached(now_ms, g_led_state.deadline_ms))
    {
        return;
    }

    if (g_led_state.mode == LED_MODE_HEARTBEAT)
    {
        dac_led_set(!g_led_state.led_on);
        g_led_state.deadline_ms = now_ms + LED_HEARTBEAT_HALF_PERIOD_MS;
        return;
    }

    if (g_led_state.mode == LED_MODE_STARTUP)
    {
        if (g_led_state.led_on)
        {
            dac_led_set(false);
            --g_led_state.pulses_remaining;
            if (g_led_state.pulses_remaining == 0U)
            {
                g_led_state.mode = LED_MODE_HEARTBEAT;
                g_led_state.deadline_ms = now_ms + LED_HEARTBEAT_HALF_PERIOD_MS;
            }
            else
            {
                g_led_state.deadline_ms = now_ms + LED_PULSE_MS;
            }
        }
        else
        {
            dac_led_set(true);
            g_led_state.deadline_ms = now_ms + LED_PULSE_MS;
        }
        return;
    }

    switch (g_led_state.fault_phase)
    {
        case LED_FAULT_STAGE_ON:
            dac_led_set(true);
            g_led_state.fault_phase = LED_FAULT_STAGE_OFF;
            g_led_state.deadline_ms = now_ms + LED_PULSE_MS;
            break;

        case LED_FAULT_STAGE_OFF:
            dac_led_set(false);
            --g_led_state.pulses_remaining;
            if (g_led_state.pulses_remaining == 0U)
            {
                g_led_state.fault_phase = LED_FAULT_SECTION_GAP;
                g_led_state.deadline_ms = now_ms + LED_FAULT_SECTION_GAP_MS;
            }
            else
            {
                g_led_state.fault_phase = LED_FAULT_STAGE_ON;
                g_led_state.deadline_ms = now_ms + LED_PULSE_MS;
            }
            break;

        case LED_FAULT_SECTION_GAP:
            g_led_state.pulses_remaining = g_led_state.cause;
            g_led_state.fault_phase = LED_FAULT_CAUSE_ON;
            g_led_state.deadline_ms = now_ms;
            break;

        case LED_FAULT_CAUSE_ON:
            dac_led_set(true);
            g_led_state.fault_phase = LED_FAULT_CAUSE_OFF;
            g_led_state.deadline_ms = now_ms + LED_PULSE_MS;
            break;

        case LED_FAULT_CAUSE_OFF:
            dac_led_set(false);
            --g_led_state.pulses_remaining;
            if (g_led_state.pulses_remaining == 0U)
            {
                g_led_state.fault_phase = LED_FAULT_FRAME_GAP;
                g_led_state.deadline_ms = now_ms + LED_FAULT_FRAME_GAP_MS;
            }
            else
            {
                g_led_state.fault_phase = LED_FAULT_CAUSE_ON;
                g_led_state.deadline_ms = now_ms + LED_PULSE_MS;
            }
            break;

        case LED_FAULT_FRAME_GAP:
            g_led_state.pulses_remaining = g_led_state.stage;
            g_led_state.fault_phase = LED_FAULT_STAGE_ON;
            g_led_state.deadline_ms = now_ms;
            break;

        default:
            dac_led_set(false);
            g_led_state.fault_phase = LED_FAULT_FRAME_GAP;
            g_led_state.deadline_ms = now_ms + LED_FAULT_FRAME_GAP_MS;
            break;
    }
}

/**
 * @brief Show a permanent repeating pre-play stage/cause fault code.
 * @param stage Approved failing-stage pulse count.
 * @param status Non-OK driver status from the failing operation.
 * @return Never returns.
 */
static void dac_led_preplay_fault_cause_forever(uint8_t stage, uint8_t cause)
{
    dac_led_start_tick();
    dac_led_latch_fault(stage, cause);

    for (;;)
    {
        dac_led_update(g_tick_ms);
    }
}

/**
 * @brief Show a permanent pre-play fault from a generic-driver operation.
 * @param stage Approved failing-stage pulse count.
 * @param status Non-OK driver status from the failing operation.
 * @return Never returns.
 */
static void dac_led_preplay_fault_forever(uint8_t stage, tlv320dac3100_status_t status)
{
    dac_led_preplay_fault_cause_forever(stage, dac_led_cause_from_status(status));
}

/**
 * @brief Run the PA7 LED diagnostic for TLV320DAC3100 1 kHz playback.
 * @return Never returns.
 *
 * One pulse immediately proves PA7 and main before external-clock use. Two
 * through five pre-play groups identify successful audio setup stages. After
 * playback begins, six non-blocking pulses lead into a one-hertz
 * heartbeat. A repeated two-frame code then identifies the first fault.
 */
int main(void)
{
    tlv320dac3100_config_t config;
    m55m1_tlv320dac3100_board_init_result_t board_result;
    tlv320dac3100_status_t status;
    tlv320dac3100_status_t first_queue_status;
    tlv320dac3100_driver_status_t driver_status;
    uint32_t last_completed_blocks = 0U;
    uint32_t last_underrun_blocks = 0U;
    uint32_t last_dma_errors = 0U;
    uint32_t last_progress_ms = 0U;

    dac_led_init();
    /* This first indication must precede HXT/APLL0 use to prove PA7 and main. */
    dac_led_emit_startup_pulses(1U);
    dac_led_make_tone_block();
    board_result = m55m1_tlv320dac3100_test_board_init(
        DAC_LED_DIAGNOSTIC_PROFILE, &config, dac_led_block_callback, NULL);
    if (board_result != M55M1_TLV320DAC3100_BOARD_INIT_OK)
    {
        dac_led_preplay_fault_cause_forever(
            LED_STAGE_BOARD_CLOCK, dac_led_cause_from_board_init_result(board_result));
    }
    dac_led_emit_startup_pulses(2U);

    status = tlv320dac3100_init(&config);
    if (status != TLV320DAC3100_STATUS_OK)
    {
        dac_led_preplay_fault_forever(LED_STAGE_CODEC_INIT, status);
    }
    dac_led_emit_startup_pulses(3U);

    status = tlv320dac3100_set_digital_gain_db(-12);
    if (status != TLV320DAC3100_STATUS_OK)
    {
        dac_led_preplay_fault_forever(LED_STAGE_CODEC_CONTROL, status);
    }
    dac_led_emit_startup_pulses(4U);

    first_queue_status = tlv320dac3100_submit_mono_block(
        g_tone_block, TLV320DAC3100_16KHZ_SAMPLES_PER_BLOCK);
    status = tlv320dac3100_submit_mono_block(
        g_tone_block, TLV320DAC3100_16KHZ_SAMPLES_PER_BLOCK);
    if ((first_queue_status != TLV320DAC3100_STATUS_OK) ||
        (status != TLV320DAC3100_STATUS_OK))
    {
        dac_led_preplay_fault_forever(
            LED_STAGE_AUDIO_QUEUE,
            (first_queue_status != TLV320DAC3100_STATUS_OK) ? first_queue_status : status);
    }
    dac_led_emit_startup_pulses(5U);

    status = tlv320dac3100_start();
    if (status != TLV320DAC3100_STATUS_OK)
    {
        dac_led_preplay_fault_forever(LED_STAGE_PLAYING, status);
    }

    dac_led_start_tick();
    dac_led_begin_playback_indication();

    for (;;)
    {
        const uint32_t now_ms = g_tick_ms;

        /*
         * Refill precedes diagnostic work. The driver changes a late slot to
         * silence safely, so its underrun counter remains the fault authority.
         */
        if (dac_led_take_block_due())
        {
            (void)tlv320dac3100_submit_mono_block(
                g_tone_block, TLV320DAC3100_16KHZ_SAMPLES_PER_BLOCK);
        }

        tlv320dac3100_get_status(&driver_status);
        if (driver_status.dma_errors != last_dma_errors)
        {
            dac_led_latch_fault(LED_STAGE_RUNTIME, LED_CAUSE_DMA_ERROR);
        }
        else if (driver_status.underrun_blocks != last_underrun_blocks)
        {
            dac_led_latch_fault(LED_STAGE_RUNTIME, LED_CAUSE_UNDERRUN);
        }
        else if (driver_status.completed_blocks != last_completed_blocks)
        {
            last_completed_blocks = driver_status.completed_blocks;
            last_progress_ms = now_ms;
        }
        else if ((now_ms - last_progress_ms) >= LED_PLAYBACK_STALL_MS)
        {
            dac_led_latch_fault(LED_STAGE_RUNTIME, LED_CAUSE_STALL);
        }

        last_dma_errors = driver_status.dma_errors;
        last_underrun_blocks = driver_status.underrun_blocks;
        dac_led_update(now_ms);
    }
}
