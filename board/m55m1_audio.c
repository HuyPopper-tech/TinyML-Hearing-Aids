/**
 * @file m55m1_audio.c
 * @brief Production M55M1 audio clock, pin, DMA, and peripheral binding.
 * @author Bui Minh Huy
 * @date 2026-07-30
 */

#include <stddef.h>

#include "board/m55m1_audio.h"
#include "board/m55m1_timebase.h"
#if M55M1_PRODUCT_AUDIO
#include "board/m55m1_power.h"
#endif
#include "NuMicro.h"

#ifndef M55M1_ENABLE_DAC_I2S_OUTPUT
#define M55M1_ENABLE_DAC_I2S_OUTPUT 0
#endif

#define M55M1_AUDIO_CORE_HZ 220000000UL
#if M55M1_CUSTOM_DMIC_TEST && !M55M1_DMIC_PRODUCT_STARTUP
#define M55M1_AUDIO_AUDIO_APLL_HZ DMIC_APLL1_FREQ_196608KHZ
#else
#define M55M1_AUDIO_AUDIO_APLL_HZ 475136006UL
#endif
/* TLV320DAC3100 supports I2C Fast-mode; control transfers do not gate PDMA audio. */
#define M55M1_AUDIO_I2C_HZ 400000UL
#define M55M1_AUDIO_PDMA_CH 0UL
#define M55M1_AUDIO_LPPDMA_CH 0UL
#define M55M1_AUDIO_I2C_RECOVERY_PULSES 9U
#define M55M1_AUDIO_I2C_RECOVERY_DELAY_US 5UL
#if M55M1_PRODUCT_AUDIO
#define M55M1_AUDIO_MAX_SAMPLES_PER_BLOCK 16U
#else
#define M55M1_AUDIO_MAX_SAMPLES_PER_BLOCK 320U
#endif

#if M55M1_ENABLE_DAC_I2S_OUTPUT
#define M55M1_AUDIO_I2C_SDA PA4
#define M55M1_AUDIO_I2C_SCL PA5
#endif

typedef struct {
    volatile bool block_ready;
    volatile uint8_t active_slot;
    volatile uint8_t completed_slot;
    uint16_t samples_per_block;
    volatile uint32_t completed_blocks;
    volatile uint32_t dma_errors;
    volatile uint32_t overrun_blocks;
    volatile uint32_t last_completed_cycle;
} m55m1_audio_dmic_state_t;

/*
 * LPPDMA accesses this ring while the data cache is enabled. Its descriptors
 * and sample slots must therefore be non-cacheable, otherwise a microphone
 * block can be replayed from stale cache lines instead of physical SRAM.
 */
static NVT_NONCACHEABLE int16_t g_dmic_buffers[2][M55M1_AUDIO_MAX_SAMPLES_PER_BLOCK];
static NVT_NONCACHEABLE LPDSCT_T g_dmic_descriptors[2] __ALIGNED(4);
static m55m1_audio_dmic_state_t g_dmic_state;
static m55m1_audio_clock_snapshot_t g_clock_snapshot;
static bool g_clock_snapshot_valid;
#if M55M1_RUNTIME_TELEMETRY
static i2c_recovery_trace_t g_i2c_recovery_trace;
#endif
#if M55M1_ENABLE_DAC_I2S_OUTPUT
static m55m1_audio_playback_irq_callback_t g_playback_irq_callback;
static void *g_playback_irq_context;

static uint32_t m55m1_audio_profile_mclk_hz(m55m1_audio_profile_t profile) {
    if (profile == M55M1_AUDIO_PROFILE_16KHZ) {
        return 4096000UL;
    }
    if (profile == M55M1_AUDIO_PROFILE_32KHZ) {
        return 8192000UL;
    }
    return 0U;
}
#endif

static uint32_t m55m1_audio_profile_sample_rate_hz(m55m1_audio_profile_t profile) {
    if (profile == M55M1_AUDIO_PROFILE_16KHZ) {
        return 16000UL;
    }
    if (profile == M55M1_AUDIO_PROFILE_32KHZ) {
        return 32000UL;
    }
    return 0U;
}

static uint16_t m55m1_audio_profile_samples_per_block(m55m1_audio_profile_t profile) {
    if (profile == M55M1_AUDIO_PROFILE_16KHZ) {
        return 16U;
    }
    if (profile == M55M1_AUDIO_PROFILE_32KHZ) {
        return 320U;
    }
    return 0U;
}

#if M55M1_RUNTIME_TELEMETRY
#if M55M1_ENABLE_DAC_I2S_OUTPUT
static i2c_recovery_gpio_t m55m1_audio_trace_i2c_gpio(void) {
    return (i2c_recovery_gpio_t){.mfp = SYS->GPA_MFP1,
                                 .mode = PA->MODE,
                                 .dout = PA->DOUT,
                                 .pin = PA->PIN,
                                 .dinoff = PA->DINOFF,
                                 .pusel = PA->PUSEL,
                                 .smten = PA->SMTEN};
}

static void m55m1_audio_trace_i2c_begin(void) {
    g_i2c_recovery_trace.attempted = true;
    g_i2c_recovery_trace.first_scl_low_event = I2C_RECOVERY_TRACE_NO_EVENT;
    g_i2c_recovery_trace.before = m55m1_audio_trace_i2c_gpio();
    g_i2c_recovery_trace.i2c_control = I2C0->CTL0;
    g_i2c_recovery_trace.i2c_status = I2C0->STATUS0;
    g_i2c_recovery_trace.i2c_divider = I2C0->CLKDIV;
    g_i2c_recovery_trace.gpio_clock = CLK->GPIOCTL;
    g_i2c_recovery_trace.i2c_clock = CLK->I2CCTL;
    g_i2c_recovery_trace.core_hz = SystemCoreClock;
    g_i2c_recovery_trace.systick_load_before = SysTick->LOAD;
    g_i2c_recovery_trace.systick_source_before = CLK->STSEL;
}

static void m55m1_audio_trace_i2c_event(i2c_recovery_phase_t phase, uint8_t pulse) {
    if (g_i2c_recovery_trace.event_count >= I2C_RECOVERY_TRACE_CAPACITY) {
        return;
    }
    const uint32_t cycle = m55m1_timebase_cycles_now();
    const uint32_t pin = PA->PIN;
    const uint8_t index = g_i2c_recovery_trace.event_count++;
    g_i2c_recovery_trace.events[index] = (i2c_recovery_event_t){.cycle = cycle,
                                                                .phase = (uint8_t)phase,
                                                                .pulse = pulse,
                                                                .sda = (pin & BIT4) != 0U,
                                                                .scl = (pin & BIT5) != 0U};
    // Only clock-low deliberately drives SCL low; other low readings need investigation.
    if (phase != I2C_RECOVERY_CLOCK_LOW && (pin & BIT5) == 0U &&
        g_i2c_recovery_trace.first_scl_low_event == I2C_RECOVERY_TRACE_NO_EVENT) {
        g_i2c_recovery_trace.first_scl_low_event = index;
    }
}

static void m55m1_audio_trace_i2c_end(uint8_t pulses, bool released) {
    g_i2c_recovery_trace.pulses = pulses;
    g_i2c_recovery_trace.released = released;
    g_i2c_recovery_trace.after = m55m1_audio_trace_i2c_gpio();
    g_i2c_recovery_trace.systick_load_after = SysTick->LOAD;
    g_i2c_recovery_trace.systick_source_after = CLK->STSEL;
}
#endif

const i2c_recovery_trace_t *m55m1_audio_get_i2c_trace(void) {
    return g_i2c_recovery_trace.attempted ? &g_i2c_recovery_trace : NULL;
}
#endif

/**
 * @brief Release a slave that retained SDA low across an MCU-only reset.
 * @return True only when both I2C bus lines are released after recovery.
 *
 * The codec may remain powered while the M55M1 is manually reset during an
 * I2C transfer. Temporarily using SDA/SCL as open-drain GPIO permits the
 * standard maximum of nine SCL pulses, followed by a STOP, before I2C owns
 * the pins. No push-pull high level is ever driven onto this shared bus.
 */
#if M55M1_ENABLE_DAC_I2S_OUTPUT
static bool m55m1_audio_recover_i2c_bus(void) {
    uint32_t pulse;

#if M55M1_RUNTIME_TELEMETRY
    m55m1_audio_trace_i2c_begin();
#endif
    SET_GPIO_PA4();
    SET_GPIO_PA5();
    GPIO_SetMode(PA, BIT4 | BIT5, GPIO_MODE_OPEN_DRAIN);
    M55M1_AUDIO_I2C_SDA = 1U;
    M55M1_AUDIO_I2C_SCL = 1U;
    m55m1_timebase_delay_us(M55M1_AUDIO_I2C_RECOVERY_DELAY_US);

#if M55M1_RUNTIME_TELEMETRY
    m55m1_audio_trace_i2c_event(I2C_RECOVERY_RELEASE, 0U);
#endif

    for (pulse = 0U; (pulse < M55M1_AUDIO_I2C_RECOVERY_PULSES) && (M55M1_AUDIO_I2C_SDA == 0U);
         ++pulse) {
        M55M1_AUDIO_I2C_SCL = 0U;
        m55m1_timebase_delay_us(M55M1_AUDIO_I2C_RECOVERY_DELAY_US);
#if M55M1_RUNTIME_TELEMETRY
        m55m1_audio_trace_i2c_event(I2C_RECOVERY_CLOCK_LOW, (uint8_t)(pulse + 1U));
#endif
        M55M1_AUDIO_I2C_SCL = 1U;
        m55m1_timebase_delay_us(M55M1_AUDIO_I2C_RECOVERY_DELAY_US);
#if M55M1_RUNTIME_TELEMETRY
        m55m1_audio_trace_i2c_event(I2C_RECOVERY_CLOCK_HIGH, (uint8_t)(pulse + 1U));
#endif
    }

    /* A STOP releases SDA while SCL is high, resetting compliant slave bus state. */
    M55M1_AUDIO_I2C_SDA = 0U;
    m55m1_timebase_delay_us(M55M1_AUDIO_I2C_RECOVERY_DELAY_US);
#if M55M1_RUNTIME_TELEMETRY
    m55m1_audio_trace_i2c_event(I2C_RECOVERY_STOP_SDA_LOW, (uint8_t)pulse);
#endif
    M55M1_AUDIO_I2C_SCL = 1U;
    m55m1_timebase_delay_us(M55M1_AUDIO_I2C_RECOVERY_DELAY_US);
#if M55M1_RUNTIME_TELEMETRY
    m55m1_audio_trace_i2c_event(I2C_RECOVERY_STOP_SCL_RELEASE, (uint8_t)pulse);
#endif
    M55M1_AUDIO_I2C_SDA = 1U;
    m55m1_timebase_delay_us(M55M1_AUDIO_I2C_RECOVERY_DELAY_US);
#if M55M1_RUNTIME_TELEMETRY
    m55m1_audio_trace_i2c_event(I2C_RECOVERY_STOP_SDA_RELEASE, (uint8_t)pulse);
#endif

    const bool released = (M55M1_AUDIO_I2C_SDA != 0U) && (M55M1_AUDIO_I2C_SCL != 0U);
#if M55M1_RUNTIME_TELEMETRY
    m55m1_audio_trace_i2c_end((uint8_t)pulse, released);
#endif
    return released;
}
#endif

/**
 * @brief Notify an optional foreground diagnostic observer of a board boundary.
 * @param callback Optional observer supplied by the board-init caller.
 * @param context Caller-owned observer context.
 * @param stage Boundary reached before progressing to the next hardware step.
 *
 * This helper is used only before PDMA0 is enabled. It deliberately has no
 * retry or buffering policy: a diagnostic callback either records the exact
 * boundary synchronously or is absent, preserving the normal board path.
 */
static void m55m1_audio_notify_progress(m55m1_audio_board_progress_callback_t callback,
                                        void *context, m55m1_audio_board_stage_t stage) {
    if (callback != NULL) {
        callback(stage, context);
    }
}

static void m55m1_audio_configure_dmic_descriptor(uint8_t slot) {
    const uint8_t next_slot = (uint8_t)(slot ^ 1U);

    g_dmic_descriptors[slot].CTL =
        LPPDMA_OP_SCATTER | LPPDMA_REQ_SINGLE | LPPDMA_WIDTH_16 | LPPDMA_SAR_FIX | LPPDMA_DAR_INC |
        LPPDMA_TBINTDIS_ENABLE |
        (((uint32_t)g_dmic_state.samples_per_block - 1UL) << LPPDMA_DSCT_CTL_TXCNT_Pos);
    g_dmic_descriptors[slot].SA = (uint32_t)(uintptr_t)&DMIC0->FIFO;
    g_dmic_descriptors[slot].DA = (uint32_t)(uintptr_t)g_dmic_buffers[slot];
    g_dmic_descriptors[slot].NEXT = (uint32_t)(uintptr_t)&g_dmic_descriptors[next_slot];
}

m55m1_audio_board_init_result_t m55m1_audio_board_init(
    m55m1_audio_profile_t profile, m55m1_audio_playback_binding_t *binding,
    m55m1_audio_playback_irq_callback_t playback_irq_callback, void *playback_irq_context,
    m55m1_audio_board_progress_callback_t progress_callback, void *progress_context) {
#if M55M1_ENABLE_DAC_I2S_OUTPUT
    uint32_t i2s_module_divider;
    uint32_t required_mclk_hz;
#endif

#if M55M1_RUNTIME_TELEMETRY
    // Preserve sticky flags as evidence; invalidate the previous attempt even on argument failure.
    g_i2c_recovery_trace = (i2c_recovery_trace_t){.reset_flags = SYS->RSTSTS};
#endif
#if M55M1_ENABLE_DAC_I2S_OUTPUT
    required_mclk_hz = m55m1_audio_profile_mclk_hz(profile);
    if ((binding == NULL) || (playback_irq_callback == NULL) || (required_mclk_hz == 0UL)) {
        return M55M1_AUDIO_BOARD_INIT_CLOCK_MISMATCH;
    }
#else
    (void)binding;
    (void)playback_irq_callback;
    (void)playback_irq_context;
    if (m55m1_audio_profile_sample_rate_hz(profile) == 0UL) {
        return M55M1_AUDIO_BOARD_INIT_CLOCK_MISMATCH;
    }
#endif
    g_clock_snapshot_valid = false;
#if M55M1_ENABLE_DAC_I2S_OUTPUT
    i2s_module_divider = (profile == M55M1_AUDIO_PROFILE_16KHZ) ? 58UL : 29UL;
#endif
    m55m1_audio_notify_progress(progress_callback, progress_context, M55M1_AUDIO_BOARD_STAGE_ENTER);
    SYS_UnlockReg();

#if M55M1_PRODUCT_AUDIO
    m55m1_power_configure();
#endif

    /*
     * CLK_EnableAPLL first disables its selected PLL. The BSP or a prior boot stage may
     * still select APLL0 as SCLK, so switching the core to HIRC first prevents
     * an APLL0 reprogram from removing the CPU clock and causing CPULK reset.
     * APLL0 is reconfigured for the 220 MHz core only after HIRC owns SCLK.
     */
    CLK_SetSCLK(CLK_SCLKSEL_SCLKSEL_HIRC);
    m55m1_audio_notify_progress(progress_callback, progress_context,
                                M55M1_AUDIO_BOARD_STAGE_SCLK_HIRC_READY);

    m55m1_audio_notify_progress(progress_callback, progress_context,
                                M55M1_AUDIO_BOARD_STAGE_HXT_WAIT);
    CLK_EnableXtalRC(CLK_SRCCTL_HXTEN_Msk);
    /*
     * The external 24 MHz crystal is required for the exact 4.096 MHz MCLK.
     * Check the StdDriver's bounded readiness result before CLK_EnableAPLL,
     * whose HXT-source path otherwise repeats the same wait internally.
     */
    if (CLK_WaitClockReady(CLK_STATUS_HXTSTB_Msk) == 0U) {
        SYS_LockReg();
        return M55M1_AUDIO_BOARD_INIT_HXT_TIMEOUT;
    }
    m55m1_audio_notify_progress(progress_callback, progress_context,
                                M55M1_AUDIO_BOARD_STAGE_HXT_READY);
    /*
     * The DMIC-only custom image uses 196.608 MHz (98.304 MHz / 48 for PDM).
     * The DAC build uses the following separate audio clock plan:
     * APLL1 remains inside the M55M1 36..500 MHz operating range. The selected
     * fractional value is divided by two and then by 58 or 29,
     * yielding exact 4.096 MHz or 8.192 MHz I2S source clocks. An invalid
     * 32.768 MHz APLL target would silently select the BSP fallback clock and
     * make the driver's clock-mismatch guard fail.
     */
    m55m1_audio_notify_progress(progress_callback, progress_context,
                                M55M1_AUDIO_BOARD_STAGE_APLL1_WAIT);
    (void)CLK_EnableAPLL(CLK_APLLCTL_APLLSRC_HXT, M55M1_AUDIO_AUDIO_APLL_HZ, CLK_APLL1_SELECT);
    /*
     * CLK_EnableAPLL returns its calculated frequency even when its internal
     * bounded wait expires. Read the hardware-ready bit so an unlocked APLL1
     * cannot silently become an invalid I2S MCLK source.
     */
    if ((CLK->STATUS & CLK_STATUS_APLL1STB_Msk) == 0U) {
        SYS_LockReg();
        return M55M1_AUDIO_BOARD_INIT_APLL1_TIMEOUT;
    }
    m55m1_audio_notify_progress(progress_callback, progress_context,
                                M55M1_AUDIO_BOARD_STAGE_APLL1_READY);

    m55m1_audio_notify_progress(progress_callback, progress_context,
                                M55M1_AUDIO_BOARD_STAGE_APLL0_WAIT);
    (void)CLK_EnableAPLL(CLK_APLLCTL_APLLSRC_HXT, M55M1_AUDIO_CORE_HZ, CLK_APLL0_SELECT);
    if ((CLK->STATUS & CLK_STATUS_APLL0STB_Msk) == 0U) {
        SYS_LockReg();
        return M55M1_AUDIO_BOARD_INIT_APLL0_TIMEOUT;
    }
    m55m1_audio_notify_progress(progress_callback, progress_context,
                                M55M1_AUDIO_BOARD_STAGE_APLL0_READY);
    CLK_SET_SCLKDIV(1UL);
    CLK_SetSCLK(CLK_SCLKSEL_SCLKSEL_APLL0);
    m55m1_audio_notify_progress(progress_callback, progress_context,
                                M55M1_AUDIO_BOARD_STAGE_SCLK_APLL0_READY);

    g_clock_snapshot = (m55m1_audio_clock_snapshot_t){
        .core_hz = CLK_GetSCLKFreq(),
        .apll0_hz = CLK_GetAPLL0ClockFreq(),
        .apll1_hz = CLK_GetAPLL1ClockFreq(),
    };
    g_clock_snapshot.i2s_src_hz = 0UL;
    g_clock_snapshot.mclk_hz = 0UL;
#if M55M1_ENABLE_DAC_I2S_OUTPUT
    g_clock_snapshot.i2s_src_hz = g_clock_snapshot.apll1_hz / 2UL;
    g_clock_snapshot.mclk_hz = g_clock_snapshot.i2s_src_hz / i2s_module_divider;
    if ((g_clock_snapshot.core_hz != M55M1_AUDIO_CORE_HZ) ||
        (g_clock_snapshot.apll0_hz != M55M1_AUDIO_CORE_HZ) ||
        (g_clock_snapshot.mclk_hz != required_mclk_hz)) {
        SYS_LockReg();
        return M55M1_AUDIO_BOARD_INIT_CLOCK_MISMATCH;
    }
#else
    if ((g_clock_snapshot.core_hz != M55M1_AUDIO_CORE_HZ) ||
        (g_clock_snapshot.apll0_hz != M55M1_AUDIO_CORE_HZ)) {
        SYS_LockReg();
        return M55M1_AUDIO_BOARD_INIT_CLOCK_MISMATCH;
    }
#endif
#if M55M1_ENABLE_DAC_I2S_OUTPUT
    if (i2s_module_divider == 58UL) {
        CLK_SetModuleClock(I2S1_MODULE, CLK_I2SSEL_I2S1SEL_APLL1_DIV2, CLK_I2SDIV_I2S1DIV(58));
    } else {
        CLK_SetModuleClock(I2S1_MODULE, CLK_I2SSEL_I2S1SEL_APLL1_DIV2, CLK_I2SDIV_I2S1DIV(29));
    }
    CLK_EnableModuleClock(I2S1_MODULE);
    CLK_EnableModuleClock(I2C0_MODULE);
    CLK_EnableModuleClock(PDMA0_MODULE);
#endif
    CLK_EnableModuleClock(GPIOB_MODULE);
#if M55M1_ENABLE_DAC_I2S_OUTPUT
    CLK_EnableModuleClock(GPIOA_MODULE);
#endif
    m55m1_audio_notify_progress(progress_callback, progress_context,
                                M55M1_AUDIO_BOARD_STAGE_MODULE_CLOCKS_READY);

#if M55M1_ENABLE_DAC_I2S_OUTPUT
    SET_I2S1_DO_PA8();
    SET_I2S1_MCLK_PA10();
    SET_I2S1_BCLK_PA11();
    SET_I2S1_LRCK_PB0();
    m55m1_audio_notify_progress(progress_callback, progress_context,
                                M55M1_AUDIO_BOARD_STAGE_AUDIO_PINS_READY);
    if (!m55m1_audio_recover_i2c_bus()) {
        SYS_LockReg();
        return M55M1_AUDIO_BOARD_INIT_I2C_STUCK;
    }
    m55m1_audio_notify_progress(progress_callback, progress_context,
                                M55M1_AUDIO_BOARD_STAGE_I2C_RECOVERY_READY);
    SET_I2C0_SCL_PA5();
    SET_I2C0_SDA_PA4();
    m55m1_audio_notify_progress(progress_callback, progress_context,
                                M55M1_AUDIO_BOARD_STAGE_I2C_PINS_READY);

    /* Clear any boot-ROM/debugger I2C state before I2C_Open enables master mode. */
    SYS_ResetModule(SYS_I2C0RST);
    m55m1_audio_notify_progress(progress_callback, progress_context,
                                M55M1_AUDIO_BOARD_STAGE_I2C_RESET_READY);
#endif
    SYS_LockReg();
    m55m1_timebase_init();
    m55m1_audio_notify_progress(progress_callback, progress_context,
                                M55M1_AUDIO_BOARD_STAGE_CYCLE_COUNTER_READY);

#if M55M1_ENABLE_DAC_I2S_OUTPUT
    /* The codec supports 400 kHz Fast-mode; do not assume probe nRESET wiring. */
    (void)I2C_Open(I2C0, M55M1_AUDIO_I2C_HZ);
    NVIC_SetPriority(PDMA0_IRQn, 1U);
    NVIC_EnableIRQ(PDMA0_IRQn);

    /* Custom PCB: I2S1 TX uses PDMA0 channel 0. */
    *binding = (m55m1_audio_playback_binding_t){
        .i2c = I2C0,
        .i2s = I2S1,
        .pdma = PDMA0,
        .pdma_channel = M55M1_AUDIO_PDMA_CH,
        .max_digital_gain_db = M55M1_AUDIO_SAFE_MAX_GAIN_DB,
    };
    g_playback_irq_callback = playback_irq_callback;
    g_playback_irq_context = playback_irq_context;
#endif
    m55m1_audio_notify_progress(progress_callback, progress_context,
                                M55M1_AUDIO_BOARD_STAGE_PERIPHERALS_READY);

    g_clock_snapshot_valid = true;
    return M55M1_AUDIO_BOARD_INIT_OK;
}

bool m55m1_audio_get_clock_snapshot(m55m1_audio_clock_snapshot_t *snapshot) {
    if ((snapshot == NULL) || !g_clock_snapshot_valid) {
        return false;
    }
    *snapshot = g_clock_snapshot;
    return true;
}

bool m55m1_audio_dmic_prepare(m55m1_audio_profile_t profile) {
    uint32_t index;
    uint32_t actual_sample_rate;
    uint32_t requested_sample_rate;
    uint16_t samples_per_block;

    requested_sample_rate = m55m1_audio_profile_sample_rate_hz(profile);
    samples_per_block = m55m1_audio_profile_samples_per_block(profile);
    if ((requested_sample_rate == 0UL) || (samples_per_block == 0U) ||
        samples_per_block > M55M1_AUDIO_MAX_SAMPLES_PER_BLOCK) {
        return false;
    }

    SYS_UnlockReg();
    /* The standalone UART2 DMIC diagnostic does not call the I2S board init. */
    CLK_EnableXtalRC(CLK_SRCCTL_HXTEN_Msk);
    if (CLK_WaitClockReady(CLK_STATUS_HXTSTB_Msk) == 0U) {
        SYS_LockReg();
        return false;
    }
    CLK_EnableModuleClock(DMIC0_MODULE);
    CLK_EnableModuleClock(LPPDMA0_MODULE);
    if (!g_clock_snapshot_valid) {
        (void)CLK_EnableAPLL(CLK_APLLCTL_APLLSRC_HXT, DMIC_APLL1_FREQ_196608KHZ, CLK_APLL1_SELECT);
    }
    if ((CLK->STATUS & CLK_STATUS_APLL1STB_Msk) == 0U) {
        SYS_LockReg();
        return false;
    }
    CLK_SetModuleClock(DMIC0_MODULE, CLK_DMICSEL_DMIC0SEL_APLL1_DIV2, CLK_DMICDIV_DMIC0DIV(1));
    CLK_EnableModuleClock(GPIOA_MODULE);
    SET_DMIC1_CLK_PA1();
    SET_DMIC1_DAT_PA2();
#if M55M1_CUSTOM_DMIC_TEST && !M55M1_DMIC_PRODUCT_STARTUP
    SYS_ResetModule(SYS_DMIC0RST);
#endif
    SYS_LockReg();

    DMIC_Open(DMIC0);
    DMIC_SetFIFOWidth(DMIC0, DMIC_FIFOWIDTH_16);
    DMIC_SetDSPGainVolume(DMIC0, DMIC_CTL_CHEN2_Msk, M55M1_DMIC_GAIN_DB);
    /*
     * MP34DT05 requires a 1.2..3.25 MHz PDM clock in normal operation. Both
     * profiles retain 2.048 MHz: 16 kHz needs OSR=128, while 32 kHz needs
     * OSR=64. Using OSR=64 at 16 kHz would generate only 1.024 MHz and can
     * distort PCM before it ever reaches the codec.
     */
    DMIC_SET_DOWNSAMPLE(DMIC0, (profile == M55M1_AUDIO_PROFILE_16KHZ) ? DMIC_DOWNSAMPLE_128
                                                                      : DMIC_DOWNSAMPLE_64);
    /*
     * MP34DT05 L/R is low, so DOUT becomes valid after CLK falls and remains
     * driven through the low phase. Channel 2 must latch on CLK rising, at
     * the end of that valid window; sampling at the falling edge captures the
     * prior high-Z/transition state and corrupts the PDM stream.
     */
    DMIC_SET_LATCHEDGE_CH23(DMIC0, DMIC_LATCHDATA_CH23RF);
    actual_sample_rate = DMIC_SetSampleRate(DMIC0, requested_sample_rate);
    if (actual_sample_rate != requested_sample_rate) {
        DMIC_Close(DMIC0);
        return false;
    }

#if M55M1_CUSTOM_DMIC_TEST && !M55M1_DMIC_PRODUCT_STARTUP
    /* Match the BSP startup sequence without changing the product path. */
    DMIC_ClearFIFO(DMIC0);
    DMIC_ResetDSP(DMIC0);
    if ((DMIC_IS_FIFOEMPTY(DMIC0) == 0U) || ((DMIC0->CTL & DMIC_CTL_SWRST_Msk) != 0U)) {
        DMIC_Close(DMIC0);
        return false;
    }
    DMIC_EnableChMsk(DMIC0, DMIC_CTL_CHEN2_Msk);
    /* MP34DT05 needs up to 10 ms of PDM clock before valid output. */
    m55m1_timebase_delay_us(10000UL);
    DMIC_ClearFIFO(DMIC0);
    if (DMIC_IS_FIFOEMPTY(DMIC0) == 0U) {
        DMIC_Close(DMIC0);
        return false;
    }
#endif

    g_dmic_state = (m55m1_audio_dmic_state_t){
        .samples_per_block = samples_per_block,
    };
    DMIC_DisableChMsk(DMIC0, DMIC_CTL_CHEN2_Msk);
    DMIC_ClearFIFO(DMIC0);
    for (index = 0U; index < 2U; ++index) {
        m55m1_audio_configure_dmic_descriptor((uint8_t)index);
    }
    LPPDMA_Open(LPPDMA, 1UL << M55M1_AUDIO_LPPDMA_CH);
    LPPDMA_SetTransferMode(LPPDMA, M55M1_AUDIO_LPPDMA_CH, LPPDMA_DMIC0_RX, LPPDMA_OP_SCATTER,
                           (uint32_t)(uintptr_t)&g_dmic_descriptors[0]);
    LPPDMA_EnableInt(LPPDMA, M55M1_AUDIO_LPPDMA_CH, LPPDMA_INT_TRANS_DONE);
    NVIC_SetPriority(LPPDMA_IRQn, 1U);
    NVIC_EnableIRQ(LPPDMA_IRQn);

    return true;
}

void m55m1_audio_dmic_enable(void) {
    LPPDMA_CLR_TD_FLAG(LPPDMA, 1UL << M55M1_AUDIO_LPPDMA_CH);
    LPPDMA_CLR_ABORT_FLAG(LPPDMA, 1UL << M55M1_AUDIO_LPPDMA_CH);
    __DMB();
    DMIC_ENABLE_LPPDMA(DMIC0);
    DMIC_EnableChMsk(DMIC0, DMIC_CTL_CHEN2_Msk);
}

bool m55m1_audio_dmic_start(m55m1_audio_profile_t profile) {
    if (!m55m1_audio_dmic_prepare(profile)) {
        return false;
    }
    m55m1_audio_dmic_enable();
    return true;
}

void m55m1_audio_dmic_stop(void) {
    const uint32_t channel_mask = 1UL << M55M1_AUDIO_LPPDMA_CH;
    uint32_t primask;

    /*
     * Stop new peripheral requests before disabling the interrupt and channel.
     * This is foreground-only; the short critical section below only resets
     * the descriptor ownership state shared with LPPDMA_IRQHandler().
     */
    DMIC_DISABLE_LPPDMA(DMIC0);
    DMIC_DisableChMsk(DMIC0, DMIC_CTL_CHEN2_Msk);
    LPPDMA_DisableInt(LPPDMA, M55M1_AUDIO_LPPDMA_CH, LPPDMA_INT_TRANS_DONE);
    NVIC_DisableIRQ(LPPDMA_IRQn);
    LPPDMA_Close(LPPDMA);
    LPPDMA_CLR_TD_FLAG(LPPDMA, channel_mask);
    LPPDMA_CLR_ABORT_FLAG(LPPDMA, channel_mask);
    NVIC_ClearPendingIRQ(LPPDMA_IRQn);
    DMIC_Close(DMIC0);

    primask = __get_PRIMASK();
    __disable_irq();
    g_dmic_state.block_ready = false;
    g_dmic_state.active_slot = 0U;
    g_dmic_state.completed_slot = 0U;
    g_dmic_state.samples_per_block = 0U;
    g_dmic_state.last_completed_cycle = 0U;
    __set_PRIMASK(primask);
}

bool m55m1_audio_dmic_take_block(int16_t *destination, uint16_t sample_count) {
    return m55m1_audio_dmic_take_block_timed(destination, sample_count, NULL);
}

bool m55m1_audio_dmic_block_pending(void) {
    return g_dmic_state.block_ready;
}

bool m55m1_audio_dmic_take_block_timed(int16_t *destination, uint16_t sample_count,
                                       m55m1_audio_dmic_block_timing_t *timing) {
    uint32_t primask;
    uint32_t index;
    uint8_t completed_slot;
    m55m1_audio_dmic_block_timing_t local_timing;

    if ((destination == NULL) || (sample_count != g_dmic_state.samples_per_block)) {
        return false;
    }

    /* Gate requests while copying: an IRQ mask alone does not stop DMA. */
    primask = __get_PRIMASK();
    __disable_irq();
    if (!g_dmic_state.block_ready) {
        __set_PRIMASK(primask);
        return false;
    }
    DMIC_DISABLE_LPPDMA(DMIC0);
    __DSB();
    uint32_t wait = 64U;
    while (LPPDMA_IS_CH_BUSY(LPPDMA, M55M1_AUDIO_LPPDMA_CH) && (--wait != 0U)) {
    }
    if ((wait == 0U) || (g_dmic_state.overrun_blocks != 0U) ||
        ((LPPDMA_GET_TD_STS(LPPDMA) & (1UL << M55M1_AUDIO_LPPDMA_CH)) != 0U)) {
        ++g_dmic_state.overrun_blocks;
        __set_PRIMASK(primask);
        return false;
    }
    completed_slot = g_dmic_state.completed_slot;
    local_timing.sequence = g_dmic_state.completed_blocks;
    local_timing.completed_cycle = g_dmic_state.last_completed_cycle;
    g_dmic_state.block_ready = false;
    __DMB();
    for (index = 0U; index < sample_count; ++index) {
        destination[index] = g_dmic_buffers[completed_slot][index];
    }
    __DMB();
    DMIC_ENABLE_LPPDMA(DMIC0);
    __set_PRIMASK(primask);
    if (timing != NULL) {
        *timing = local_timing;
    }
    return true;
}

void m55m1_audio_dmic_get_status(m55m1_audio_dmic_status_t *status) {
    uint32_t primask;

    if (status == NULL) {
        return;
    }

    /* These counters are updated in LPPDMA IRQ context, so copy them atomically. */
    primask = __get_PRIMASK();
    __disable_irq();
    *status = (m55m1_audio_dmic_status_t){
        .completed_blocks = g_dmic_state.completed_blocks,
        .overrun_blocks = g_dmic_state.overrun_blocks,
        .dma_errors = g_dmic_state.dma_errors,
    };
    __set_PRIMASK(primask);
}

void PDMA0_IRQHandler(void) {
#if M55M1_ENABLE_DAC_I2S_OUTPUT
    if (g_playback_irq_callback != NULL) {
        g_playback_irq_callback(m55m1_timebase_cycles_now(), g_playback_irq_context);
    }
#endif
}

void LPPDMA_IRQHandler(void) {
    const uint32_t channel_mask = 1UL << M55M1_AUDIO_LPPDMA_CH;

    if ((LPPDMA_GET_ABORT_STS(LPPDMA) & channel_mask) != 0U) {
        LPPDMA_CLR_ABORT_FLAG(LPPDMA, channel_mask);
        ++g_dmic_state.dma_errors;
    }

    if ((LPPDMA_GET_TD_STS(LPPDMA) & channel_mask) != 0U) {
        LPPDMA_CLR_TD_FLAG(LPPDMA, channel_mask);
        ++g_dmic_state.completed_blocks;
        g_dmic_state.last_completed_cycle = m55m1_timebase_cycles_now();
        if (g_dmic_state.block_ready) {
            ++g_dmic_state.overrun_blocks;
        }

        /* The completed descriptor is stable while LPPDMA fills the alternate slot. */
        g_dmic_state.completed_slot = g_dmic_state.active_slot;
        g_dmic_state.active_slot ^= 1U;
        g_dmic_state.block_ready = true;
    }
}
