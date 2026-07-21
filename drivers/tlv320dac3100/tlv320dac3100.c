/**
 * @file tlv320dac3100.c
 * @brief TLV320DAC3100 I2C control and fixed-profile I2S/PDMA playback.
 * @author Bui Minh Huy
 * @date 2026-07-20
 */

#include <stddef.h>

#include "drivers/tlv320dac3100/tlv320dac3100.h"

/* Page 0 register addresses used by the 16 kHz playback script. */
#define TLV320DAC3100_REG_PAGE_SELECT                 0x00U
#define TLV320DAC3100_REG_SOFTWARE_RESET              0x01U
#define TLV320DAC3100_REG_CODEC_CLOCK_INPUT           0x04U
#define TLV320DAC3100_REG_NDAC                        0x0BU
#define TLV320DAC3100_REG_MDAC                        0x0CU
#define TLV320DAC3100_REG_DOSR_MSB                    0x0DU
#define TLV320DAC3100_REG_DOSR_LSB                    0x0EU
#define TLV320DAC3100_REG_CODEC_INTERFACE_CTRL_1      0x1BU
#define TLV320DAC3100_REG_DAC_PROCESSING_BLOCK        0x3CU
#define TLV320DAC3100_REG_DAC_DATA_PATH               0x3FU
#define TLV320DAC3100_REG_DAC_VOLUME_CONTROL           0x40U
#define TLV320DAC3100_REG_DAC_LEFT_VOLUME              0x41U
#define TLV320DAC3100_REG_DAC_RIGHT_VOLUME             0x42U

/* Page 1 register addresses used by the headphone output script. */
#define TLV320DAC3100_REG_HEADPHONE_DRIVERS            0x1FU
#define TLV320DAC3100_REG_HEADPHONE_DEPOP               0x21U
#define TLV320DAC3100_REG_DAC_OUTPUT_ROUTE             0x23U
#define TLV320DAC3100_REG_HPL_ANALOG_VOLUME            0x24U
#define TLV320DAC3100_REG_HPR_ANALOG_VOLUME            0x25U
#define TLV320DAC3100_REG_HPL_DRIVER                   0x28U
#define TLV320DAC3100_REG_HPR_DRIVER                   0x29U

#define TLV320DAC3100_PAGE_0                           0x00U
#define TLV320DAC3100_PAGE_1                           0x01U
#define TLV320DAC3100_DAC_POWER_LEFT_RIGHT             0xD4U
#define TLV320DAC3100_DAC_MUTE_LEFT_RIGHT              0x0CU
#define TLV320DAC3100_DAC_UNMUTE                       0x00U
/* HPL/HPR enabled, 1.35 V common mode, reserved D2=1, short-current limiting. */
#define TLV320DAC3100_HEADPHONE_DRIVERS_POWERED        0xC4U
/*
 * M55M1 I2S TXFIFO entries are 32-bit stereo frames even with 16-bit audio
 * selected. One word therefore carries the left and right signed-16 samples
 * for one LRCK period; PDMA must transfer 160 words, not 320 half-words.
 */
#define TLV320DAC3100_DMA_MAX_FRAMES_PER_BLOCK         \
    TLV320DAC3100_MAX_SAMPLES_PER_BLOCK
/* Reset-less breakouts need time to release internal reset after their rails rise. */
#define TLV320DAC3100_CODEC_POWER_UP_SETTLE_US         20000UL
/* Probe for a maximum of 100 ms after the initial 20 ms power-up settle. */
#define TLV320DAC3100_I2C_READY_ATTEMPTS               20U
#define TLV320DAC3100_I2C_READY_RETRY_DELAY_US         5000UL
/* A transient NACK may occur while a software reset or analogue block settles. */
#define TLV320DAC3100_I2C_WRITE_ATTEMPTS               3U
#define TLV320DAC3100_I2C_WRITE_RETRY_DELAY_US         2000UL
#define TLV320DAC3100_I2C_PHASE_PAGE_SELECT            1U
#define TLV320DAC3100_I2C_PHASE_TARGET_REGISTER        2U
/* At 100 kHz, every master state arrives in under 100 us; 200,000 HIRC cycles gives margin. */
#define TLV320DAC3100_I2C_WAIT_LIMIT                   200000UL

typedef struct {
    uint32_t sample_rate_hz;
    uint32_t mclk_hz;
    uint16_t samples_per_block;
} tlv320dac3100_profile_definition_t;

typedef struct {
    tlv320dac3100_config_t config;
    const tlv320dac3100_profile_definition_t *profile;
    tlv320dac3100_driver_status_t status;
    bool slot_available[2];
    bool slot_contains_audio[2];
    bool has_queued_audio;
    uint8_t completed_slot;
} tlv320dac3100_context_t;

static const tlv320dac3100_profile_definition_t g_profiles[] = {
    [TLV320DAC3100_AUDIO_PROFILE_16KHZ] = {
        .sample_rate_hz = TLV320DAC3100_16KHZ_SAMPLE_RATE_HZ,
        .mclk_hz = TLV320DAC3100_16KHZ_MCLK_HZ,
        .samples_per_block = TLV320DAC3100_16KHZ_SAMPLES_PER_BLOCK,
    },
    [TLV320DAC3100_AUDIO_PROFILE_32KHZ] = {
        .sample_rate_hz = TLV320DAC3100_32KHZ_SAMPLE_RATE_HZ,
        .mclk_hz = TLV320DAC3100_32KHZ_MCLK_HZ,
        .samples_per_block = TLV320DAC3100_32KHZ_SAMPLES_PER_BLOCK,
    },
};

/*
 * PDMA reads these buffers directly while D-cache is enabled. Keeping both
 * descriptors and samples non-cacheable avoids a cache clean in every 10 ms
 * audio deadline and prevents stale PCM from reaching the codec.
 */
static NVT_NONCACHEABLE uint32_t g_stereo_buffers[2][TLV320DAC3100_DMA_MAX_FRAMES_PER_BLOCK];
static NVT_NONCACHEABLE uint32_t g_silence_buffer[TLV320DAC3100_DMA_MAX_FRAMES_PER_BLOCK];
static NVT_NONCACHEABLE DSCT_T g_pdma_descriptors[2] __ALIGNED(4);
static tlv320dac3100_context_t g_context;

/**
 * @brief Preserve the context required to diagnose one terminal I2C failure.
 * @param page Codec page requested by the failed operation.
 * @param reg Codec register requested by the failed operation.
 * @param phase Write phase: page select or target register.
 * @param attempts Number of attempts used before the failure was reported.
 *
 * This runs only after all retries fail. Counting an exhausted operation once,
 * rather than every NACK, keeps runtime logs meaningful when a reset-less
 * codec becomes ready during a retry window.
 */
static void tlv320dac3100_record_i2c_failure(uint8_t page, uint8_t reg,
                                              uint8_t phase, uint8_t attempts)
{
    ++g_context.status.i2c_failures;
    g_context.status.last_i2c_page = page;
    g_context.status.last_i2c_register = reg;
    g_context.status.last_i2c_phase = phase;
    g_context.status.last_i2c_attempts = attempts;
    g_context.status.last_i2c_status = (uint8_t)I2C_GET_STATUS(g_context.config.i2c);
    g_context.status.last_i2c_control = g_context.config.i2c->CTL0;
}

/**
 * @brief Return immutable timing data for a public audio profile.
 * @param profile Requested 16 kHz or 32 kHz profile.
 * @return Profile definition, or NULL for an unsupported enum value.
 */
static const tlv320dac3100_profile_definition_t *
tlv320dac3100_get_profile(tlv320dac3100_audio_profile_t profile)
{
    if ((uint32_t)profile >= (sizeof(g_profiles) / sizeof(g_profiles[0])))
    {
        return NULL;
    }
    return &g_profiles[profile];
}

static uint32_t tlv320dac3100_get_profile_sample_rate(
    tlv320dac3100_audio_profile_t profile)
{
    const tlv320dac3100_profile_definition_t *definition = tlv320dac3100_get_profile(profile);
    return (definition == NULL) ? 0UL : definition->sample_rate_hz;
}

static uint32_t tlv320dac3100_get_profile_mclk(tlv320dac3100_audio_profile_t profile)
{
    const tlv320dac3100_profile_definition_t *definition = tlv320dac3100_get_profile(profile);
    return (definition == NULL) ? 0UL : definition->mclk_hz;
}

static uint16_t tlv320dac3100_get_profile_samples_per_block(
    tlv320dac3100_audio_profile_t profile)
{
    const tlv320dac3100_profile_definition_t *definition = tlv320dac3100_get_profile(profile);
    return (definition == NULL) ? 0U : definition->samples_per_block;
}

uint32_t tlv320dac3100_profile_sample_rate_hz(tlv320dac3100_audio_profile_t profile)
{
    return tlv320dac3100_get_profile_sample_rate(profile);
}

uint32_t tlv320dac3100_profile_mclk_hz(tlv320dac3100_audio_profile_t profile)
{
    return tlv320dac3100_get_profile_mclk(profile);
}

uint16_t tlv320dac3100_profile_samples_per_block(tlv320dac3100_audio_profile_t profile)
{
    return tlv320dac3100_get_profile_samples_per_block(profile);
}

/**
 * @brief Wait for a single I2C master state without using the BSP's 1-second loop.
 * @param i2c Board-provided I2C controller that owns the pending state.
 * @return True when SI becomes set; false when the controller made no progress.
 *
 * The StdDriver helper polls SI for `SystemCoreClock` iterations. A missing
 * peripheral clock therefore hides the root cause behind a multi-second
 * diagnostic freeze. The driver-owned bound is long enough for 100 kHz I2C
 * but always lets foreground UART report the frozen controller state.
 */
static bool tlv320dac3100_wait_for_i2c_si(I2C_T *i2c)
{
    uint32_t count;

    for (count = 0U; count < TLV320DAC3100_I2C_WAIT_LIMIT; ++count)
    {
        if ((i2c->CTL0 & I2C_CTL0_SI_Msk) != 0U)
        {
            return true;
        }
    }
    return false;
}

/**
 * @brief Write one 8-bit codec register through a bounded polling state machine.
 * @param i2c Board-provided, enabled I2C controller.
 * @param address Codec 7-bit I2C address.
 * @param reg Codec register address to transmit after SLA+W.
 * @param value Register value to transmit.
 * @return True only after SLA+W, register, and value were each ACKed.
 *
 * No I2C IRQ is used: this runs only during foreground codec control. On a
 * missing SI, arbitration loss, or NACK it sends STOP immediately. The caller
 * then owns retry policy and records STATUS0/CTL0 for UART diagnostics.
 */
static bool tlv320dac3100_write_one_reg_bounded(I2C_T *i2c, uint8_t address,
                                                 uint8_t reg, uint8_t value)
{
    uint8_t expected_status;

    I2C_START(i2c);
    if (!tlv320dac3100_wait_for_i2c_si(i2c))
    {
        goto failed;
    }
    expected_status = I2C_GET_STATUS(i2c);
    if ((expected_status != 0x08U) && (expected_status != 0x10U))
    {
        goto failed;
    }

    I2C_SET_DATA(i2c, (uint8_t)(address << 1U));
    I2C_SET_CONTROL_REG(i2c, I2C_CTL_SI);
    if (!tlv320dac3100_wait_for_i2c_si(i2c) || (I2C_GET_STATUS(i2c) != 0x18U))
    {
        goto failed;
    }

    I2C_SET_DATA(i2c, reg);
    I2C_SET_CONTROL_REG(i2c, I2C_CTL_SI);
    if (!tlv320dac3100_wait_for_i2c_si(i2c) || (I2C_GET_STATUS(i2c) != 0x28U))
    {
        goto failed;
    }

    I2C_SET_DATA(i2c, value);
    I2C_SET_CONTROL_REG(i2c, I2C_CTL_SI);
    if (!tlv320dac3100_wait_for_i2c_si(i2c) || (I2C_GET_STATUS(i2c) != 0x28U))
    {
        goto failed;
    }

    I2C_SET_CONTROL_REG(i2c, I2C_CTL_STO_SI);
    return true;

failed:
    I2C_SET_CONTROL_REG(i2c, I2C_CTL_STO_SI);
    return false;
}

/**
 * @brief Wait for a reset-less codec to ACK a harmless page-0 select write.
 * @return `OK` after an ACK or `I2C_ERROR` after the bounded ready window.
 *
 * The Adafruit breakout may finish its power-on reset after the M55M1 starts.
 * Selecting page 0 is idempotent and does not enable audio, so it is a safe
 * readiness probe before the register script issues the software reset.
 */
static tlv320dac3100_status_t tlv320dac3100_wait_for_i2c_ready(void)
{
    uint8_t attempt;

    for (attempt = 1U; attempt <= TLV320DAC3100_I2C_READY_ATTEMPTS; ++attempt)
    {
        if (tlv320dac3100_write_one_reg_bounded(
                g_context.config.i2c, TLV320DAC3100_I2C_ADDRESS,
                TLV320DAC3100_REG_PAGE_SELECT, TLV320DAC3100_PAGE_0))
        {
            return TLV320DAC3100_STATUS_OK;
        }
        CLK_SysTickDelay(TLV320DAC3100_I2C_READY_RETRY_DELAY_US);
    }

    tlv320dac3100_record_i2c_failure(TLV320DAC3100_PAGE_0,
                                      TLV320DAC3100_REG_PAGE_SELECT,
                                      TLV320DAC3100_I2C_PHASE_PAGE_SELECT, attempt - 1U);
    return TLV320DAC3100_STATUS_I2C_ERROR;
}

static tlv320dac3100_status_t tlv320dac3100_write_register(uint8_t page, uint8_t reg, uint8_t value)
{
    uint8_t attempt;

    /* Every retry reselects the page because a prior target write can be ambiguous. */
    for (attempt = 1U; attempt <= TLV320DAC3100_I2C_WRITE_ATTEMPTS; ++attempt)
    {
        if (!tlv320dac3100_write_one_reg_bounded(
                g_context.config.i2c, TLV320DAC3100_I2C_ADDRESS,
                TLV320DAC3100_REG_PAGE_SELECT, page))
        {
            if (attempt == TLV320DAC3100_I2C_WRITE_ATTEMPTS)
            {
                tlv320dac3100_record_i2c_failure(page, reg,
                                                  TLV320DAC3100_I2C_PHASE_PAGE_SELECT,
                                                  attempt);
                return TLV320DAC3100_STATUS_I2C_ERROR;
            }
            CLK_SysTickDelay(TLV320DAC3100_I2C_WRITE_RETRY_DELAY_US);
            continue;
        }

        if (!tlv320dac3100_write_one_reg_bounded(
                g_context.config.i2c, TLV320DAC3100_I2C_ADDRESS, reg, value))
        {
            if (attempt == TLV320DAC3100_I2C_WRITE_ATTEMPTS)
            {
                tlv320dac3100_record_i2c_failure(page, reg,
                                                  TLV320DAC3100_I2C_PHASE_TARGET_REGISTER,
                                                  attempt);
                return TLV320DAC3100_STATUS_I2C_ERROR;
            }
            CLK_SysTickDelay(TLV320DAC3100_I2C_WRITE_RETRY_DELAY_US);
            continue;
        }

        return TLV320DAC3100_STATUS_OK;
    }

    return TLV320DAC3100_STATUS_I2C_ERROR;
}

static uint32_t tlv320dac3100_get_pdma_request(const I2S_T *i2s)
{
    return (i2s == I2S0) ? PDMA_I2S0_TX : PDMA_I2S1_TX;
}

static void tlv320dac3100_configure_descriptor(uint8_t slot, const uint32_t *source)
{
    const uint8_t next_slot = (uint8_t)(slot ^ 1U);

    g_pdma_descriptors[slot].CTL = PDMA_OP_SCATTER | PDMA_REQ_SINGLE | PDMA_WIDTH_32 |
                                   PDMA_SAR_INC | PDMA_DAR_FIX |
                                   (((uint32_t)g_context.profile->samples_per_block - 1UL)
                                    << PDMA_DSCT_CTL_TXCNT_Pos);
    g_pdma_descriptors[slot].SA = (uint32_t)(uintptr_t)source;
    g_pdma_descriptors[slot].DA = (uint32_t)(uintptr_t)&g_context.config.i2s->TXFIFO;
    g_pdma_descriptors[slot].NEXT = (uint32_t)(uintptr_t)&g_pdma_descriptors[next_slot];
}

/** @brief Encode one valid signed whole-dB DAC attenuation value for page-0 volume registers. */
static uint8_t tlv320dac3100_gain_to_register(int8_t gain_db)
{
    return (gain_db == 0) ? 0U : (uint8_t)(256 + ((int16_t)gain_db * 2));
}

static tlv320dac3100_status_t tlv320dac3100_configure_codec(void)
{
    tlv320dac3100_status_t status;
    const uint8_t initial_gain = tlv320dac3100_gain_to_register(
        g_context.config.max_digital_gain_db);

    /* Datasheet page 20: reset is followed by a mandatory 1 ms memory lockout. */
    status = tlv320dac3100_write_register(TLV320DAC3100_PAGE_0,
                                           TLV320DAC3100_REG_SOFTWARE_RESET, 0x01U);
    if (status != TLV320DAC3100_STATUS_OK)
    {
        return status;
    }
    CLK_SysTickDelay(1000UL);

    /* Each accepted profile supplies MCLK=256*Fs directly; the internal PLL stays off. */
    status = tlv320dac3100_write_register(TLV320DAC3100_PAGE_0,
                                           TLV320DAC3100_REG_CODEC_CLOCK_INPUT, 0x00U);
    if (status != TLV320DAC3100_STATUS_OK) return status;
    status = tlv320dac3100_write_register(TLV320DAC3100_PAGE_0, TLV320DAC3100_REG_NDAC, 0x81U);
    if (status != TLV320DAC3100_STATUS_OK) return status;
    status = tlv320dac3100_write_register(TLV320DAC3100_PAGE_0, TLV320DAC3100_REG_MDAC, 0x81U);
    if (status != TLV320DAC3100_STATUS_OK) return status;
    status = tlv320dac3100_write_register(TLV320DAC3100_PAGE_0, TLV320DAC3100_REG_DOSR_MSB, 0x01U);
    if (status != TLV320DAC3100_STATUS_OK) return status;
    status = tlv320dac3100_write_register(TLV320DAC3100_PAGE_0, TLV320DAC3100_REG_DOSR_LSB, 0x00U);
    if (status != TLV320DAC3100_STATUS_OK) return status;

    /* I2S format, 16-bit words, and BCLK/WCLK are inputs because M55M1 is master. */
    status = tlv320dac3100_write_register(TLV320DAC3100_PAGE_0,
                                           TLV320DAC3100_REG_CODEC_INTERFACE_CTRL_1, 0x00U);
    if (status != TLV320DAC3100_STATUS_OK) return status;
    status = tlv320dac3100_write_register(TLV320DAC3100_PAGE_0,
                                           TLV320DAC3100_REG_DAC_PROCESSING_BLOCK, 0x07U);
    if (status != TLV320DAC3100_STATUS_OK) return status;

    /* Page 1 programs HPL/HPR de-pop-safe routing before the DAC is powered. */
    status = tlv320dac3100_write_register(TLV320DAC3100_PAGE_1,
                                           TLV320DAC3100_REG_HEADPHONE_DEPOP, 0x4EU);
    if (status != TLV320DAC3100_STATUS_OK) return status;
    status = tlv320dac3100_write_register(TLV320DAC3100_PAGE_1,
                                           TLV320DAC3100_REG_DAC_OUTPUT_ROUTE, 0x44U);
    if (status != TLV320DAC3100_STATUS_OK) return status;
    status = tlv320dac3100_write_register(TLV320DAC3100_PAGE_1,
                                           TLV320DAC3100_REG_HPL_ANALOG_VOLUME, 0x80U);
    if (status != TLV320DAC3100_STATUS_OK) return status;
    status = tlv320dac3100_write_register(TLV320DAC3100_PAGE_1,
                                           TLV320DAC3100_REG_HPR_ANALOG_VOLUME, 0x80U);
    if (status != TLV320DAC3100_STATUS_OK) return status;
    status = tlv320dac3100_write_register(TLV320DAC3100_PAGE_1,
                                           TLV320DAC3100_REG_HPL_DRIVER, 0x06U);
    if (status != TLV320DAC3100_STATUS_OK) return status;
    status = tlv320dac3100_write_register(TLV320DAC3100_PAGE_1,
                                           TLV320DAC3100_REG_HPR_DRIVER, 0x06U);
    if (status != TLV320DAC3100_STATUS_OK) return status;
    /*
     * Keep short protection in current-limit mode. A jack insertion can
     * briefly short the right-ring contact; shutdown mode would latch HPR off
     * until another codec reset, producing a left-only headphone symptom.
     */
    status = tlv320dac3100_write_register(TLV320DAC3100_PAGE_1,
                                           TLV320DAC3100_REG_HEADPHONE_DRIVERS,
                                           TLV320DAC3100_HEADPHONE_DRIVERS_POWERED);
    if (status != TLV320DAC3100_STATUS_OK) return status;

    CLK_SysTickDelay(800000UL);

    /* Start at the board safety ceiling; foreground may attenuate, never exceed it. */
    status = tlv320dac3100_write_register(TLV320DAC3100_PAGE_0,
                                           TLV320DAC3100_REG_DAC_LEFT_VOLUME, initial_gain);
    if (status != TLV320DAC3100_STATUS_OK) return status;
    status = tlv320dac3100_write_register(TLV320DAC3100_PAGE_0,
                                           TLV320DAC3100_REG_DAC_RIGHT_VOLUME, initial_gain);
    if (status != TLV320DAC3100_STATUS_OK) return status;
    status = tlv320dac3100_write_register(TLV320DAC3100_PAGE_0,
                                           TLV320DAC3100_REG_DAC_DATA_PATH,
                                           TLV320DAC3100_DAC_POWER_LEFT_RIGHT);
    if (status != TLV320DAC3100_STATUS_OK) return status;

    return tlv320dac3100_write_register(TLV320DAC3100_PAGE_0,
                                         TLV320DAC3100_REG_DAC_VOLUME_CONTROL,
                                         TLV320DAC3100_DAC_UNMUTE);
}

tlv320dac3100_status_t tlv320dac3100_init(const tlv320dac3100_config_t *config)
{
    uint32_t actual_rate;
    uint32_t actual_mclk;
    tlv320dac3100_status_t status;
    const tlv320dac3100_profile_definition_t *profile;

    if ((config == NULL) || (config->i2c == NULL) || (config->pdma == NULL) ||
        ((config->i2s != I2S0) && (config->i2s != I2S1)) || (config->pdma_channel >= PDMA_CH_MAX) ||
        (config->max_digital_gain_db < -63) || (config->max_digital_gain_db > 0))
    {
        return TLV320DAC3100_STATUS_INVALID_ARGUMENT;
    }
    profile = tlv320dac3100_get_profile(config->profile);
    if (profile == NULL)
    {
        return TLV320DAC3100_STATUS_INVALID_ARGUMENT;
    }
    if (g_context.status.running)
    {
        return TLV320DAC3100_STATUS_INVALID_STATE;
    }

    g_context.config = *config;
    g_context.profile = profile;
    g_context.status = (tlv320dac3100_driver_status_t){0};
    g_context.slot_available[0] = true;
    g_context.slot_available[1] = true;
    g_context.slot_contains_audio[0] = false;
    g_context.slot_contains_audio[1] = false;
    g_context.has_queued_audio = false;
    g_context.completed_slot = 0U;

    actual_rate = I2S_Open(config->i2s, I2S_MODE_MASTER, profile->sample_rate_hz,
                            I2S_DATABIT_16, I2S_DISABLE_MONO, I2S_FORMAT_I2S);
    actual_mclk = I2S_EnableMCLK(config->i2s, profile->mclk_hz);
    if ((actual_rate != profile->sample_rate_hz) || (actual_mclk != profile->mclk_hz))
    {
        I2S_Close(config->i2s);
        return TLV320DAC3100_STATUS_CLOCK_MISMATCH;
    }

    tlv320dac3100_configure_descriptor(0U, g_silence_buffer);
    tlv320dac3100_configure_descriptor(1U, g_silence_buffer);
    PDMA_Open(config->pdma, 1UL << config->pdma_channel);
    PDMA_SetTransferMode(config->pdma, config->pdma_channel, tlv320dac3100_get_pdma_request(config->i2s),
                         PDMA_OP_SCATTER, (uint32_t)(uintptr_t)&g_pdma_descriptors[0]);
    PDMA_EnableInt(config->pdma, config->pdma_channel, PDMA_INT_TRANS_DONE);

    /* No GPIO owns codec RESET, so establish MCLK then wait/poll for power-on ACK. */
    CLK_SysTickDelay(TLV320DAC3100_CODEC_POWER_UP_SETTLE_US);
    status = tlv320dac3100_wait_for_i2c_ready();
    if (status != TLV320DAC3100_STATUS_OK)
    {
        I2S_Close(config->i2s);
        return status;
    }

    status = tlv320dac3100_configure_codec();
    if (status != TLV320DAC3100_STATUS_OK)
    {
        I2S_Close(config->i2s);
        return status;
    }

    g_context.status.initialized = true;
    g_context.status.muted = false;
    return TLV320DAC3100_STATUS_OK;
}

tlv320dac3100_status_t tlv320dac3100_start(void)
{
    uint32_t primask;

    if (!g_context.status.initialized || g_context.status.running)
    {
        return TLV320DAC3100_STATUS_INVALID_STATE;
    }

    /* Once TX starts, both descriptors are owned by PDMA until their IRQ completes. */
    primask = __get_PRIMASK();
    __disable_irq();
    g_context.slot_available[0] = false;
    g_context.slot_available[1] = false;
    __set_PRIMASK(primask);

    I2S_ENABLE_TXDMA(g_context.config.i2s);
    I2S_ENABLE_TX(g_context.config.i2s);
    g_context.status.running = true;
    return TLV320DAC3100_STATUS_OK;
}

tlv320dac3100_status_t tlv320dac3100_mute(bool enabled)
{
    tlv320dac3100_status_t status;

    if (!g_context.status.initialized)
    {
        return TLV320DAC3100_STATUS_INVALID_STATE;
    }

    status = tlv320dac3100_write_register(TLV320DAC3100_PAGE_0,
                                           TLV320DAC3100_REG_DAC_VOLUME_CONTROL,
                                           enabled ? TLV320DAC3100_DAC_MUTE_LEFT_RIGHT
                                                   : TLV320DAC3100_DAC_UNMUTE);
    if (status == TLV320DAC3100_STATUS_OK)
    {
        g_context.status.muted = enabled;
    }
    return status;
}

tlv320dac3100_status_t tlv320dac3100_stop(void)
{
    tlv320dac3100_status_t status;

    if (!g_context.status.initialized)
    {
        return TLV320DAC3100_STATUS_INVALID_STATE;
    }

    status = tlv320dac3100_mute(true);
    I2S_DISABLE_TXDMA(g_context.config.i2s);
    PDMA_STOP(g_context.config.pdma, g_context.config.pdma_channel);
    /* STOP only pauses this BSP's channel; RESET clears that pause before a later init. */
    PDMA_RESET(g_context.config.pdma, g_context.config.pdma_channel);
    I2S_DISABLE_TX(g_context.config.i2s);
    g_context.status.running = false;

    if (status != TLV320DAC3100_STATUS_OK)
    {
        g_context.status.initialized = false;
        return status;
    }
    status = tlv320dac3100_write_register(TLV320DAC3100_PAGE_0,
                                           TLV320DAC3100_REG_DAC_DATA_PATH, 0x00U);
    if (status != TLV320DAC3100_STATUS_OK)
    {
        g_context.status.initialized = false;
        return status;
    }
    status = tlv320dac3100_write_register(TLV320DAC3100_PAGE_1,
                                           TLV320DAC3100_REG_HEADPHONE_DRIVERS, 0x04U);
    g_context.status.initialized = false;
    return status;
}

tlv320dac3100_status_t tlv320dac3100_submit_mono_block(
    const int16_t *samples, uint16_t sample_count)
{
    uint32_t primask;
    uint8_t slot = 2U;
    uint32_t index;

    if ((samples == NULL) || !g_context.status.initialized ||
        (sample_count != g_context.profile->samples_per_block))
    {
        return TLV320DAC3100_STATUS_INVALID_ARGUMENT;
    }

    primask = __get_PRIMASK();
    __disable_irq();
    for (index = 0U; index < 2U; ++index)
    {
        if (g_context.slot_available[index])
        {
            slot = (uint8_t)index;
            g_context.slot_available[index] = false;
            break;
        }
    }
    __set_PRIMASK(primask);

    if (slot >= 2U)
    {
        return TLV320DAC3100_STATUS_BUSY;
    }

    for (index = 0U; index < sample_count; ++index)
    {
        const uint32_t mono_sample = (uint32_t)(uint16_t)samples[index];

        /* Duplicate signed PCM into both halfwords of one 32-bit TXFIFO frame. */
        g_stereo_buffers[slot][index] = mono_sample | (mono_sample << 16U);
    }

    /* This descriptor was completed before becoming available, so changing SA is race-free. */
    g_pdma_descriptors[slot].SA = (uint32_t)(uintptr_t)g_stereo_buffers[slot];
    g_context.slot_contains_audio[slot] = true;
    g_context.has_queued_audio = true;
    ++g_context.status.queued_blocks;
    return TLV320DAC3100_STATUS_OK;
}

tlv320dac3100_status_t tlv320dac3100_set_channel_digital_gain_db(
    int8_t left_gain_db, int8_t right_gain_db)
{
    tlv320dac3100_status_t status;

    if (!g_context.status.initialized)
    {
        return TLV320DAC3100_STATUS_INVALID_STATE;
    }
    if ((left_gain_db < -63) || (right_gain_db < -63) ||
        (left_gain_db > g_context.config.max_digital_gain_db) ||
        (right_gain_db > g_context.config.max_digital_gain_db))
    {
        return TLV320DAC3100_STATUS_INVALID_ARGUMENT;
    }

    status = tlv320dac3100_write_register(TLV320DAC3100_PAGE_0,
                                           TLV320DAC3100_REG_DAC_LEFT_VOLUME,
                                           tlv320dac3100_gain_to_register(left_gain_db));
    if (status != TLV320DAC3100_STATUS_OK)
    {
        return status;
    }
    return tlv320dac3100_write_register(TLV320DAC3100_PAGE_0,
                                         TLV320DAC3100_REG_DAC_RIGHT_VOLUME,
                                         tlv320dac3100_gain_to_register(right_gain_db));
}

tlv320dac3100_status_t tlv320dac3100_set_digital_gain_db(int8_t gain_db)
{
    return tlv320dac3100_set_channel_digital_gain_db(gain_db, gain_db);
}

void tlv320dac3100_get_status(tlv320dac3100_driver_status_t *status)
{
    uint32_t primask;
    uint8_t index;

    if (status != NULL)
    {
        /*
         * The PDMA IRQ changes completion counters and ownership flags. A
         * short critical section gives diagnostics one coherent snapshot;
         * unlike the IRQ, this function never touches DMA descriptors.
         */
        primask = __get_PRIMASK();
        __disable_irq();
        *status = g_context.status;
        status->available_slot_mask = 0U;
        status->audio_slot_mask = 0U;
        for (index = 0U; index < 2U; ++index)
        {
            if (g_context.slot_available[index])
            {
                status->available_slot_mask |= 1U << index;
            }
            if (g_context.slot_contains_audio[index])
            {
                status->audio_slot_mask |= 1U << index;
            }
        }
        status->next_completed_slot = g_context.completed_slot;
        __set_PRIMASK(primask);
    }
}

tlv320dac3100_status_t tlv320dac3100_recover(void)
{
    tlv320dac3100_config_t config;

    if (!g_context.status.initialized)
    {
        return TLV320DAC3100_STATUS_INVALID_STATE;
    }

    config = g_context.config;
    (void)tlv320dac3100_stop();
    return tlv320dac3100_init(&config);
}

void tlv320dac3100_handle_pdma_irq(void)
{
    const uint32_t channel_mask = 1UL << g_context.config.pdma_channel;
    const uint8_t slot = g_context.completed_slot;

    if (!g_context.status.initialized)
    {
        return;
    }

    if ((PDMA_GET_ABORT_STS(g_context.config.pdma) & channel_mask) != 0U)
    {
        PDMA_CLR_ABORT_FLAG(g_context.config.pdma, channel_mask);
        ++g_context.status.dma_errors;
    }

    if ((PDMA_GET_TD_STS(g_context.config.pdma) & channel_mask) != 0U)
    {
        PDMA_CLR_TD_FLAG(g_context.config.pdma, channel_mask);
        ++g_context.status.completed_blocks;
        if (g_context.has_queued_audio && !g_context.slot_contains_audio[slot])
        {
            ++g_context.status.underrun_blocks;
        }

        /* Revert to silence before releasing the slot so a missed deadline cannot replay old audio. */
        g_pdma_descriptors[slot].SA = (uint32_t)(uintptr_t)g_silence_buffer;
        g_context.slot_contains_audio[slot] = false;
        g_context.slot_available[slot] = true;
        g_context.completed_slot ^= 1U;

        if (g_context.config.block_callback != NULL)
        {
            g_context.config.block_callback(g_context.config.callback_context);
        }
    }
}
