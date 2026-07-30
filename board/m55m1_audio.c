/**
 * @file m55m1_audio.c
 * @brief Production M55M1 audio clock, pin, DMA, and peripheral binding.
 * @author Bui Minh Huy
 * @date 2026-07-30
 */

#include <stddef.h>

#include "board/m55m1_audio.h"
#include "board/m55m1_timebase.h"
#include "NuMicro.h"

#define M55M1_AUDIO_CORE_HZ       220000000UL
#define M55M1_AUDIO_AUDIO_APLL_HZ 475136006UL
/* TLV320DAC3100 supports I2C Fast-mode; control transfers do not gate PDMA audio. */
#define M55M1_AUDIO_I2C_HZ    400000UL
#define M55M1_AUDIO_PDMA_CH   0UL
#define M55M1_AUDIO_LPPDMA_CH 0UL
#define M55M1_AUDIO_I2C_RECOVERY_PULSES 9U
#define M55M1_AUDIO_I2C_RECOVERY_DELAY_US 5UL
#define M55M1_AUDIO_MAX_SAMPLES_PER_BLOCK 320U

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
static NVT_NONCACHEABLE int16_t
    g_dmic_buffers[2][M55M1_AUDIO_MAX_SAMPLES_PER_BLOCK];
static NVT_NONCACHEABLE LPDSCT_T g_dmic_descriptors[2] __ALIGNED(4);
static m55m1_audio_dmic_state_t g_dmic_state;
static m55m1_audio_clock_snapshot_t g_clock_snapshot;
static bool g_clock_snapshot_valid;
static m55m1_audio_playback_irq_callback_t g_playback_irq_callback;
static void *g_playback_irq_context;

static uint32_t m55m1_audio_profile_mclk_hz(m55m1_audio_profile_t profile)
{
    if (profile == M55M1_AUDIO_PROFILE_16KHZ) {
        return 4096000UL;
    }
    if (profile == M55M1_AUDIO_PROFILE_32KHZ) {
        return 8192000UL;
    }
    return 0U;
}

static uint32_t m55m1_audio_profile_sample_rate_hz(
    m55m1_audio_profile_t profile)
{
    if (profile == M55M1_AUDIO_PROFILE_16KHZ) {
        return 16000UL;
    }
    if (profile == M55M1_AUDIO_PROFILE_32KHZ) {
        return 32000UL;
    }
    return 0U;
}

static uint16_t m55m1_audio_profile_samples_per_block(
    m55m1_audio_profile_t profile)
{
    if (profile == M55M1_AUDIO_PROFILE_16KHZ) {
        return 160U;
    }
    if (profile == M55M1_AUDIO_PROFILE_32KHZ) {
        return 320U;
    }
    return 0U;
}

/**
 * @brief Release a slave that retained SDA low across an MCU-only reset.
 * @return True only when both I2C bus lines are released after recovery.
 *
 * The codec may remain powered while the M55M1 is manually reset during an
 * I2C transfer. Temporarily using PB0/PB1 as open-drain GPIO permits the
 * standard maximum of nine SCL pulses, followed by a STOP, before I2C1 owns
 * the pins. No push-pull high level is ever driven onto this shared bus.
 */
static bool m55m1_audio_recover_i2c_bus(void)
{
    uint32_t pulse;

    SET_GPIO_PB0();
    SET_GPIO_PB1();
    GPIO_SetMode(PB, BIT0 | BIT1, GPIO_MODE_OPEN_DRAIN);
    PB0 = 1U;
    PB1 = 1U;
    CLK_SysTickDelay(M55M1_AUDIO_I2C_RECOVERY_DELAY_US);

    for (pulse = 0U;
         (pulse < M55M1_AUDIO_I2C_RECOVERY_PULSES) && (PB0 == 0U);
         ++pulse)
    {
        PB1 = 0U;
        CLK_SysTickDelay(M55M1_AUDIO_I2C_RECOVERY_DELAY_US);
        PB1 = 1U;
        CLK_SysTickDelay(M55M1_AUDIO_I2C_RECOVERY_DELAY_US);
    }

    /* A STOP releases SDA while SCL is high, resetting compliant slave bus state. */
    PB0 = 0U;
    CLK_SysTickDelay(M55M1_AUDIO_I2C_RECOVERY_DELAY_US);
    PB1 = 1U;
    CLK_SysTickDelay(M55M1_AUDIO_I2C_RECOVERY_DELAY_US);
    PB0 = 1U;
    CLK_SysTickDelay(M55M1_AUDIO_I2C_RECOVERY_DELAY_US);

    return (PB0 != 0U) && (PB1 != 0U);
}

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
static void m55m1_audio_notify_progress(
    m55m1_audio_board_progress_callback_t callback, void *context,
    m55m1_audio_board_stage_t stage)
{
    if (callback != NULL)
    {
        callback(stage, context);
    }
}

static void m55m1_audio_configure_dmic_descriptor(uint8_t slot)
{
    const uint8_t next_slot = (uint8_t)(slot ^ 1U);

    g_dmic_descriptors[slot].CTL = LPPDMA_OP_SCATTER | LPPDMA_REQ_SINGLE |
                                   LPPDMA_WIDTH_16 | LPPDMA_SAR_FIX |
                                   LPPDMA_DAR_INC | LPPDMA_TBINTDIS_ENABLE |
                                   (((uint32_t)g_dmic_state.samples_per_block - 1UL)
                                    << LPPDMA_DSCT_CTL_TXCNT_Pos);
    g_dmic_descriptors[slot].SA = (uint32_t)(uintptr_t)&DMIC0->FIFO;
    g_dmic_descriptors[slot].DA = (uint32_t)(uintptr_t)g_dmic_buffers[slot];
    g_dmic_descriptors[slot].NEXT = (uint32_t)(uintptr_t)&g_dmic_descriptors[next_slot];
}

m55m1_audio_board_init_result_t m55m1_audio_board_init(
    m55m1_audio_profile_t profile,
    m55m1_audio_playback_binding_t *binding,
    m55m1_audio_playback_irq_callback_t playback_irq_callback,
    void *playback_irq_context,
    m55m1_audio_board_progress_callback_t progress_callback,
    void *progress_context)
{
    uint32_t i2s_module_divider;
    uint32_t required_mclk_hz;

    required_mclk_hz = m55m1_audio_profile_mclk_hz(profile);
    if ((binding == NULL) || (playback_irq_callback == NULL) ||
        (required_mclk_hz == 0UL))
    {
        return M55M1_AUDIO_BOARD_INIT_CLOCK_MISMATCH;
    }
    g_clock_snapshot_valid = false;
    i2s_module_divider = (profile == M55M1_AUDIO_PROFILE_16KHZ) ? 58UL : 29UL;
    m55m1_audio_notify_progress(
        progress_callback, progress_context, M55M1_AUDIO_BOARD_STAGE_ENTER);
    SYS_UnlockReg();

    /*
     * CLK_EnableAPLL first disables its selected PLL. The BSP or a prior boot stage may
     * still select APLL0 as SCLK, so switching the core to HIRC first prevents
     * an APLL0 reprogram from removing the CPU clock and causing CPULK reset.
     * APLL0 is reconfigured for the 220 MHz core only after HIRC owns SCLK.
     */
    CLK_SetSCLK(CLK_SCLKSEL_SCLKSEL_HIRC);
    m55m1_audio_notify_progress(
        progress_callback, progress_context,
        M55M1_AUDIO_BOARD_STAGE_SCLK_HIRC_READY);

    m55m1_audio_notify_progress(
        progress_callback, progress_context, M55M1_AUDIO_BOARD_STAGE_HXT_WAIT);
    CLK_EnableXtalRC(CLK_SRCCTL_HXTEN_Msk);
    /*
     * The external 24 MHz crystal is required for the exact 4.096 MHz MCLK.
     * Check the StdDriver's bounded readiness result before CLK_EnableAPLL,
     * whose HXT-source path otherwise repeats the same wait internally.
     */
    if (CLK_WaitClockReady(CLK_STATUS_HXTSTB_Msk) == 0U)
    {
        SYS_LockReg();
        return M55M1_AUDIO_BOARD_INIT_HXT_TIMEOUT;
    }
    m55m1_audio_notify_progress(
        progress_callback, progress_context, M55M1_AUDIO_BOARD_STAGE_HXT_READY);
    /*
     * APLL1 remains inside the M55M1 36..500 MHz operating range. The selected
     * fractional value is divided by two and then by 58 or 29,
     * yielding exact 4.096 MHz or 8.192 MHz I2S source clocks. An invalid
     * 32.768 MHz APLL target would silently select the BSP fallback clock and
     * make the driver's clock-mismatch guard fail.
     */
    m55m1_audio_notify_progress(
        progress_callback, progress_context, M55M1_AUDIO_BOARD_STAGE_APLL1_WAIT);
    (void)CLK_EnableAPLL(CLK_APLLCTL_APLLSRC_HXT,
                         M55M1_AUDIO_AUDIO_APLL_HZ,
                         CLK_APLL1_SELECT);
    /*
     * CLK_EnableAPLL returns its calculated frequency even when its internal
     * bounded wait expires. Read the hardware-ready bit so an unlocked APLL1
     * cannot silently become an invalid I2S MCLK source.
     */
    if ((CLK->STATUS & CLK_STATUS_APLL1STB_Msk) == 0U)
    {
        SYS_LockReg();
        return M55M1_AUDIO_BOARD_INIT_APLL1_TIMEOUT;
    }
    m55m1_audio_notify_progress(
        progress_callback, progress_context, M55M1_AUDIO_BOARD_STAGE_APLL1_READY);

    m55m1_audio_notify_progress(
        progress_callback, progress_context, M55M1_AUDIO_BOARD_STAGE_APLL0_WAIT);
    (void)CLK_EnableAPLL(CLK_APLLCTL_APLLSRC_HXT,
                         M55M1_AUDIO_CORE_HZ,
                         CLK_APLL0_SELECT);
    if ((CLK->STATUS & CLK_STATUS_APLL0STB_Msk) == 0U)
    {
        SYS_LockReg();
        return M55M1_AUDIO_BOARD_INIT_APLL0_TIMEOUT;
    }
    m55m1_audio_notify_progress(
        progress_callback, progress_context, M55M1_AUDIO_BOARD_STAGE_APLL0_READY);
    CLK_SET_SCLKDIV(1UL);
    CLK_SetSCLK(CLK_SCLKSEL_SCLKSEL_APLL0);
    m55m1_audio_notify_progress(
        progress_callback, progress_context,
        M55M1_AUDIO_BOARD_STAGE_SCLK_APLL0_READY);

    g_clock_snapshot = (m55m1_audio_clock_snapshot_t){
        .core_hz = CLK_GetSCLKFreq(),
        .apll0_hz = CLK_GetAPLL0ClockFreq(),
        .apll1_hz = CLK_GetAPLL1ClockFreq(),
    };
    g_clock_snapshot.i2s_src_hz = g_clock_snapshot.apll1_hz / 2UL;
    g_clock_snapshot.mclk_hz =
        g_clock_snapshot.i2s_src_hz / i2s_module_divider;
    if ((g_clock_snapshot.core_hz != M55M1_AUDIO_CORE_HZ) ||
        (g_clock_snapshot.apll0_hz != M55M1_AUDIO_CORE_HZ) ||
        (g_clock_snapshot.mclk_hz != required_mclk_hz))
    {
        SYS_LockReg();
        return M55M1_AUDIO_BOARD_INIT_CLOCK_MISMATCH;
    }
    if (i2s_module_divider == 58UL)
    {
        CLK_SetModuleClock(I2S0_MODULE, CLK_I2SSEL_I2S0SEL_APLL1_DIV2,
                           CLK_I2SDIV_I2S0DIV(58));
    }
    else
    {
        CLK_SetModuleClock(I2S0_MODULE, CLK_I2SSEL_I2S0SEL_APLL1_DIV2,
                           CLK_I2SDIV_I2S0DIV(29));
    }
    CLK_EnableModuleClock(I2S0_MODULE);
    CLK_EnableModuleClock(I2C1_MODULE);
    CLK_EnableModuleClock(PDMA0_MODULE);
    CLK_EnableModuleClock(GPIOB_MODULE);
    CLK_EnableModuleClock(GPIOF_MODULE);
    m55m1_audio_notify_progress(
        progress_callback, progress_context,
        M55M1_AUDIO_BOARD_STAGE_MODULE_CLOCKS_READY);

    /* These assignments intentionally disable J2 SWD and UART5 while audio is active. */
    SET_I2S0_DO_PF0();
    SET_I2S0_LRCK_PF1();
    SET_I2S0_BCLK_PB5();
    SET_I2S0_MCLK_PB4();
    m55m1_audio_notify_progress(
        progress_callback, progress_context,
        M55M1_AUDIO_BOARD_STAGE_AUDIO_PINS_READY);
    if (!m55m1_audio_recover_i2c_bus())
    {
        SYS_LockReg();
        return M55M1_AUDIO_BOARD_INIT_I2C_STUCK;
    }
    m55m1_audio_notify_progress(
        progress_callback, progress_context,
        M55M1_AUDIO_BOARD_STAGE_I2C_RECOVERY_READY);
    SET_I2C1_SCL_PB1();
    SET_I2C1_SDA_PB0();
    m55m1_audio_notify_progress(
        progress_callback, progress_context,
        M55M1_AUDIO_BOARD_STAGE_I2C_PINS_READY);

    /* Clear any boot-ROM/debugger I2C state before I2C_Open enables master mode. */
    SYS_ResetModule(SYS_I2C1RST);
    SYS_LockReg();
    m55m1_audio_notify_progress(
        progress_callback, progress_context,
        M55M1_AUDIO_BOARD_STAGE_I2C_RESET_READY);
    m55m1_timebase_init();
    m55m1_audio_notify_progress(
        progress_callback, progress_context,
        M55M1_AUDIO_BOARD_STAGE_CYCLE_COUNTER_READY);

    /* The codec supports 400 kHz Fast-mode; nRESET now resets codec and MCU together. */
    (void)I2C_Open(I2C1, M55M1_AUDIO_I2C_HZ);
    NVIC_SetPriority(PDMA0_IRQn, 1U);
    NVIC_EnableIRQ(PDMA0_IRQn);

    /* The generic driver maps I2S0 to the PDMA_I2S0_TX request source. */
    *binding = (m55m1_audio_playback_binding_t){
        .i2c = I2C1,
        .i2s = I2S0,
        .pdma = PDMA0,
        .pdma_channel = M55M1_AUDIO_PDMA_CH,
        .max_digital_gain_db = M55M1_AUDIO_SAFE_MAX_GAIN_DB,
    };
    g_playback_irq_callback = playback_irq_callback;
    g_playback_irq_context = playback_irq_context;
    m55m1_audio_notify_progress(
        progress_callback, progress_context,
        M55M1_AUDIO_BOARD_STAGE_PERIPHERALS_READY);

    g_clock_snapshot_valid = true;
    return M55M1_AUDIO_BOARD_INIT_OK;
}

bool m55m1_audio_get_clock_snapshot(
    m55m1_audio_clock_snapshot_t *snapshot)
{
    if ((snapshot == NULL) || !g_clock_snapshot_valid)
    {
        return false;
    }
    *snapshot = g_clock_snapshot;
    return true;
}

bool m55m1_audio_dmic_start(m55m1_audio_profile_t profile)
{
    uint32_t index;
    uint32_t actual_sample_rate;
    uint32_t requested_sample_rate;
    uint16_t samples_per_block;

    requested_sample_rate = m55m1_audio_profile_sample_rate_hz(profile);
    samples_per_block = m55m1_audio_profile_samples_per_block(profile);
    if ((requested_sample_rate == 0UL) || (samples_per_block == 0U))
    {
        return false;
    }

    SYS_UnlockReg();
    /* The standalone UART5 DMIC diagnostic does not call the I2S board init. */
    CLK_EnableXtalRC(CLK_SRCCTL_HXTEN_Msk);
    CLK_WaitClockReady(CLK_STATUS_HXTSTB_Msk);
    CLK_EnableModuleClock(DMIC0_MODULE);
    CLK_EnableModuleClock(LPPDMA0_MODULE);
    if (!g_clock_snapshot_valid)
    {
        (void)CLK_EnableAPLL(CLK_APLLCTL_APLLSRC_HXT,
                             DMIC_APLL1_FREQ_196608KHZ,
                             CLK_APLL1_SELECT);
    }
    CLK_SetModuleClock(DMIC0_MODULE, CLK_DMICSEL_DMIC0SEL_APLL1_DIV2,
                       CLK_DMICDIV_DMIC0DIV(1));
    SET_DMIC1_CLK_PB2();
    SET_DMIC1_DAT_PB3();
    SYS_LockReg();

    DMIC_Open(DMIC0);
    DMIC_SetFIFOWidth(DMIC0, DMIC_FIFOWIDTH_16);
    /*
     * MP34DT05 requires a 1.2..3.25 MHz PDM clock in normal operation. Both
     * profiles retain 2.048 MHz: 16 kHz needs OSR=128, while 32 kHz needs
     * OSR=64. Using OSR=64 at 16 kHz would generate only 1.024 MHz and can
     * distort PCM before it ever reaches the codec.
     */
    DMIC_SET_DOWNSAMPLE(DMIC0, (profile == M55M1_AUDIO_PROFILE_16KHZ)
                         ? DMIC_DOWNSAMPLE_128 : DMIC_DOWNSAMPLE_64);
    /*
     * MP34DT05 L/R is low, so DOUT becomes valid after CLK falls and remains
     * driven through the low phase. Channel 2 must latch on CLK rising, at
     * the end of that valid window; sampling at the falling edge captures the
     * prior high-Z/transition state and corrupts the PDM stream.
     */
    DMIC_SET_LATCHEDGE_CH23(DMIC0, DMIC_LATCHDATA_CH23RF);
    actual_sample_rate = DMIC_SetSampleRate(DMIC0, requested_sample_rate);
    if (actual_sample_rate != requested_sample_rate)
    {
        DMIC_Close(DMIC0);
        return false;
    }

    g_dmic_state = (m55m1_audio_dmic_state_t){
        .samples_per_block = samples_per_block,
    };
    for (index = 0U; index < 2U; ++index)
    {
        m55m1_audio_configure_dmic_descriptor((uint8_t)index);
    }
    LPPDMA_Open(LPPDMA, 1UL << M55M1_AUDIO_LPPDMA_CH);
    LPPDMA_SetTransferMode(LPPDMA, M55M1_AUDIO_LPPDMA_CH,
                           LPPDMA_DMIC0_RX, LPPDMA_OP_SCATTER,
                           (uint32_t)(uintptr_t)&g_dmic_descriptors[0]);
    LPPDMA_EnableInt(LPPDMA, M55M1_AUDIO_LPPDMA_CH, LPPDMA_INT_TRANS_DONE);
    NVIC_SetPriority(LPPDMA_IRQn, 1U);
    NVIC_EnableIRQ(LPPDMA_IRQn);

    DMIC_EnableChMsk(DMIC0, DMIC_CTL_CHEN2_Msk);
    DMIC_ENABLE_LPPDMA(DMIC0);
    return true;
}

void m55m1_audio_dmic_stop(void)
{
    const uint32_t channel_mask = 1UL << M55M1_AUDIO_LPPDMA_CH;
    uint32_t primask;

    /*
     * Stop new peripheral requests before disabling the interrupt and channel.
     * This is foreground-only; the short critical section below only resets
     * the descriptor ownership state shared with LPPDMA_IRQHandler().
     */
    DMIC_DISABLE_LPPDMA(DMIC0);
    DMIC_DisableChMsk(DMIC0, DMIC_CTL_CHEN2_Msk);
    LPPDMA_DisableInt(
        LPPDMA, M55M1_AUDIO_LPPDMA_CH, LPPDMA_INT_TRANS_DONE);
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

bool m55m1_audio_dmic_take_block(
    int16_t *destination, uint16_t sample_count)
{
    return m55m1_audio_dmic_take_block_timed(
        destination, sample_count, NULL);
}

bool m55m1_audio_dmic_block_pending(void)
{
    return g_dmic_state.block_ready;
}

bool m55m1_audio_dmic_take_block_timed(
    int16_t *destination, uint16_t sample_count,
    m55m1_audio_dmic_block_timing_t *timing)
{
    uint32_t primask;
    uint32_t index;
    uint8_t completed_slot;
    m55m1_audio_dmic_block_timing_t local_timing;

    if ((destination == NULL) || (sample_count != g_dmic_state.samples_per_block))
    {
        return false;
    }

    /* Only ownership flags need exclusion; LPPDMA keeps filling the alternate slot. */
    primask = __get_PRIMASK();
    __disable_irq();
    if (!g_dmic_state.block_ready)
    {
        __set_PRIMASK(primask);
        return false;
    }
    completed_slot = g_dmic_state.completed_slot;
    local_timing.sequence = g_dmic_state.completed_blocks;
    local_timing.completed_cycle = g_dmic_state.last_completed_cycle;
    g_dmic_state.block_ready = false;
    __set_PRIMASK(primask);

    /* This 10 ms profile block completes copying before LPPDMA returns to the alternate slot. */
    for (index = 0U; index < sample_count; ++index)
    {
        destination[index] = g_dmic_buffers[completed_slot][index];
    }
    if (timing != NULL)
    {
        *timing = local_timing;
    }
    return true;
}

void m55m1_audio_dmic_get_status(
    m55m1_audio_dmic_status_t *status)
{
    uint32_t primask;

    if (status == NULL)
    {
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

void PDMA0_IRQHandler(void)
{
    if (g_playback_irq_callback != NULL) {
        g_playback_irq_callback(
            m55m1_timebase_cycles_now(), g_playback_irq_context);
    }
}

void LPPDMA_IRQHandler(void)
{
    const uint32_t channel_mask = 1UL << M55M1_AUDIO_LPPDMA_CH;

    if ((LPPDMA_GET_ABORT_STS(LPPDMA) & channel_mask) != 0U)
    {
        LPPDMA_CLR_ABORT_FLAG(LPPDMA, channel_mask);
        ++g_dmic_state.dma_errors;
    }

    if ((LPPDMA_GET_TD_STS(LPPDMA) & channel_mask) != 0U)
    {
        LPPDMA_CLR_TD_FLAG(LPPDMA, channel_mask);
        ++g_dmic_state.completed_blocks;
        g_dmic_state.last_completed_cycle = m55m1_timebase_cycles_now();
        if (g_dmic_state.block_ready)
        {
            ++g_dmic_state.overrun_blocks;
        }

        /* The completed descriptor is stable while LPPDMA fills the alternate slot. */
        g_dmic_state.completed_slot = g_dmic_state.active_slot;
        g_dmic_state.active_slot ^= 1U;
        g_dmic_state.block_ready = true;
    }
}
