/**************************************************************************//**
 * @file    main.c
 * @brief   Production-style functional test for the on-board MEMS microphone.
 *
 * Board wiring (NuMaker-GestureAI-M55M1 schematic, sheet 6):
 *   U4 MP34DT05-M CLK  -> PB2 / DMIC1_CLK
 *   U4 MP34DT05-M DOUT -> PB3 / DMIC1_DAT
 *   U4 L/R is pulled low by R22, so it transmits in the first half-clock
 *   slot.  DMIC channel 2 is consequently sampled on the falling edge.
 *
 * The test uses polling deliberately.  It eliminates DMA/cache variables
 * during bring-up and still drains the 32-word FIFO much faster than 16 kHz.
 *****************************************************************************/

#include <stdint.h>
#include <stdio.h>
#include "NuMicro.h"

#define DMIC_SAMPLE_RATE_HZ       16000UL
#define DMIC_PDM_OSR                 100UL
#define DMIC_PDM_CLOCK_NOMINAL_HZ 1600000UL /* 16 kHz x OSR 100 */
#define DMIC_TEST_SAMPLES           512UL /* 32 ms per measurement */
#define DMIC_IDLE_WINDOWS            16UL /* 0.512 s ambient baseline */
#define DMIC_SOUND_WINDOWS           64UL /* 2.048 s: speak/clap here */
#define DMIC_GAIN_DB                  24   /* makes a simple UART test clear */
#define DMIC_CAPTURE_TIMEOUT_LOOPS (SystemCoreClock / 20UL) /* 50 ms */

typedef struct
{
    int32_t  dc;
    uint32_t rms;
    uint32_t peak;
    uint32_t peak_to_peak;
    uint32_t clipped;
} dmic_stats_t;

static int32_t DmicSignExtend24(uint32_t value)
{
    value &= 0x00FFFFFFUL;
    return (value & 0x00800000UL) ? (int32_t)(value | 0xFF000000UL) : (int32_t)value;
}

static uint32_t IntegerSqrt64(uint64_t value)
{
    uint64_t bit = 1ULL << 62;
    uint64_t root = 0ULL;

    while (bit > value)
    {
        bit >>= 2;
    }

    while (bit != 0ULL)
    {
        if (value >= root + bit)
        {
            value -= root + bit;
            root = (root >> 1) + bit;
        }
        else
        {
            root >>= 1;
        }
        bit >>= 2;
    }

    return (uint32_t)root;
}

/* Capture one 32 ms block without printing.  Printing while the DMIC runs
 * would overrun its 32-word FIFO at 16 kHz. */
static uint32_t CaptureDmicBlock(dmic_stats_t *stats)
{
    uint32_t count = 0UL;
    uint32_t timeout = DMIC_CAPTURE_TIMEOUT_LOOPS;
    int32_t minimum = INT32_MAX;
    int32_t maximum = INT32_MIN;
    int64_t sum = 0;
    uint64_t sum_squares = 0ULL;

    stats->clipped = 0UL;

    while (count < DMIC_TEST_SAMPLES)
    {
        if (DMIC_IS_FIFOFULL(DMIC0))
        {
            return 0UL;
        }

        if (!DMIC_IS_FIFOEMPTY(DMIC0))
        {
            int32_t sample = DmicSignExtend24(DMIC_READ_DATA(DMIC0));
            int64_t sample64 = sample;

            if (sample < minimum) minimum = sample;
            if (sample > maximum) maximum = sample;
            if ((sample >= 0x007F0000L) || (sample <= -0x007F0000L)) stats->clipped++;

            sum += sample64;
            sum_squares += (uint64_t)(sample64 * sample64);
            count++;
            timeout = DMIC_CAPTURE_TIMEOUT_LOOPS;
        }
        else if (--timeout == 0UL)
        {
            return 0UL; /* clock/data/FIFO did not become active */
        }
    }

    stats->dc = (int32_t)(sum / (int64_t)DMIC_TEST_SAMPLES);
    stats->peak = (uint32_t)((maximum >= -minimum) ? maximum : -minimum);
    stats->peak_to_peak = (uint32_t)((int64_t)maximum - (int64_t)minimum);

    /* RMS of AC audio: sqrt(E[x^2] - E[x]^2). */
    {
        uint64_t mean_square = sum_squares / DMIC_TEST_SAMPLES;
        uint64_t dc_square = (uint64_t)((int64_t)stats->dc * (int64_t)stats->dc);
        stats->rms = IntegerSqrt64((mean_square > dc_square) ? (mean_square - dc_square) : 0ULL);
    }
    return 1UL;
}

static void DmicHardwareInit(void)
{
    SYS_UnlockReg();

    /* MP34DT05 requires a PDM clock from 1.2 to 3.25 MHz.  OSR=100 keeps
     * 16 kHz PCM while the 48 MHz source divides exactly to 1.6 MHz. */
    CLK_EnableXtalRC(CLK_SRCCTL_HIRC48MEN_Msk);
    CLK_SetModuleClock(DMIC0_MODULE, CLK_DMICSEL_DMIC0SEL_HIRC48M,
                       CLK_DMICDIV_DMIC0DIV(3));
    CLK_EnableModuleClock(DMIC0_MODULE);

    CLK_EnableModuleClock(GPIOB_MODULE);
    SET_DMIC1_CLK_PB2();
    SET_DMIC1_DAT_PB3();

    SYS_LockReg();

    DMIC_Open(DMIC0);
    DMIC_SetFIFOWidth(DMIC0, DMIC_FIFOWIDTH_24);
    DMIC_SetGainStep(DMIC0, DMIC_GAINSTEP_1_8);
    DMIC_SET_DOWNSAMPLE(DMIC0, DMIC_DOWNSAMPLE_100);
    DMIC_SET_LATCHEDGE_CH23(DMIC0, DMIC_LATCHDATA_CH23FR);
    (void)DMIC_SetSampleRate(DMIC0, DMIC_SAMPLE_RATE_HZ);
    DMIC_SetDSPGainVolume(DMIC0, DMIC_CTL_CHEN2_Msk, DMIC_GAIN_DB);
    DMIC_ClearFIFO(DMIC0);
}

static void RunMicrophoneTest(void)
{
    dmic_stats_t block;
    dmic_stats_t idle = {0};
    dmic_stats_t loudest = {0};
    uint32_t index;
    uint32_t capture_ok = 1UL;

    DmicHardwareInit();

    printf("\n=== MP34DT05-M / DMIC microphone test ===\n");
    printf("PB2=DMIC1_CLK, PB3=DMIC1_DAT, CH2 falling edge\n");
    {
        uint32_t actual_sample_rate = DMIC_GetSampleRate(DMIC0);

        printf("Fs requested=%lu Hz, Fs actual=%u Hz, PDM clock=%lu Hz (nominal %lu)\n",
               DMIC_SAMPLE_RATE_HZ, actual_sample_rate,
               actual_sample_rate * DMIC_PDM_OSR, DMIC_PDM_CLOCK_NOMINAL_HZ);
    }
    /* Do not print after DMIC starts: UART at 115200 baud takes longer than
     * the 32-sample FIFO can hold.  The complete operator instruction must
     * therefore be sent before enabling the microphone clock. */
    printf("Keep quiet for 0.5 s, then speak/tap/clap near U4 for 2 s.\n");
    printf("Capture starts now (no UART output until it has finished)...\n");
    DMIC_EnableChMsk(DMIC0, DMIC_CTL_CHEN2_Msk);
    /* MP34DT05 specifies up to 10 ms from the first PDM clock edge to valid
     * data.  Its FIFO can overflow during that wait; clear all startup data. */
    CLK_SysTickDelay(10000UL);
    DMIC_ClearFIFO(DMIC0);
    for (index = 0UL; index < DMIC_IDLE_WINDOWS; index++)
    {
        if (!CaptureDmicBlock(&block))
        {
            capture_ok = 0UL;
            break;
        }
        if (block.rms > idle.rms) idle = block;
    }

    if (capture_ok)
    {
        for (index = 0UL; index < DMIC_SOUND_WINDOWS; index++)
        {
            if (!CaptureDmicBlock(&block))
            {
                capture_ok = 0UL;
                break;
            }
            if (block.rms > loudest.rms) loudest = block;
        }
    }
    DMIC_Close(DMIC0); /* stop clock/data activity before UART reporting */

    if (!capture_ok)
    {
        printf("RESULT: FAIL - no PCM block in time or DMIC FIFO overflowed.\n");
        printf("Check U4 power, PB2/PB3 routing, and DMIC1 pin mux.\n");
        return;
    }

    printf("Ambient: rms=%u peak=%u p2p=%u dc=%d clip=%u\n",
           idle.rms, idle.peak, idle.peak_to_peak, idle.dc, idle.clipped);
    printf("Loudest: rms=%u peak=%u p2p=%u dc=%d clip=%u\n",
           loudest.rms, loudest.peak, loudest.peak_to_peak, loudest.dc, loudest.clipped);

    if ((loudest.peak_to_peak < 1000UL) || (loudest.rms < 100UL))
    {
        printf("RESULT: FAIL - PCM arrived but signal is nearly constant.\n");
    }
    else if (loudest.clipped != 0UL)
    {
        printf("RESULT: PASS (audio present), WARN: clipping; reduce DMIC_GAIN_DB.\n");
    }
    else if (loudest.rms > ((idle.rms * 3UL) / 2UL + 100UL))
    {
        printf("RESULT: PASS - microphone responds to sound.\n");
    }
    else
    {
        printf("RESULT: PASS - PCM/audio noise present; no strong sound response detected.\n");
        printf("Repeat and speak/clap closer to U4 to validate acoustic response.\n");
    }
}

static void SYS_Init(void)
{
    SYS_UnlockReg();
    CLK_SetBusClock(CLK_SCLKSEL_SCLKSEL_APLL0, CLK_APLLCTL_APLLSRC_HIRC, FREQ_220MHZ);
    SystemCoreClockUpdate();
    SetDebugUartCLK();
    SetDebugUartMFP();
    SYS_LockReg();
}

int main(void)
{
    SYS_Init();
    InitDebugUart();

    printf("NuMaker-GestureAI-M55M1 microphone functional test\n");
    printf("SystemCoreClock=%u Hz\n", SystemCoreClock);

    while (1)
    {
        RunMicrophoneTest();
        uint32_t delay_count;

        printf("Next test starts in 3 seconds.\n");
        /* CLK_SysTickDelay() has a maximum one-shot delay of about 76 ms at
         * 220 MHz, so form the 3-second interval from valid 50-ms chunks. */
        for (delay_count = 0UL; delay_count < 60UL; delay_count++)
        {
            CLK_SysTickDelay(50000UL);
        }
    }
}
