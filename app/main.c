/**
 * @file main.c
 * @brief Bare-metal 16 kHz ERB-GRU hearing-aid composition and event loop.
 * @author Bui Minh Huy
 * @date 2026-07-29
 *
 * All initialization and polling printf output finishes before audio starts.
 * DMA ISRs only transfer ownership; DSP, inference, and telemetry formatting
 * remain foreground-only.
 */

#include <stdint.h>
#include <stdio.h>

#include "board/m55m1_debug_uart.h"
#include "board/m55m1_tlv320dac3100_test.h"
#include "drivers/tlv320dac3100/tlv320dac3100.h"

#ifndef M55M1_ENABLE_ERB_GRU
#define M55M1_ENABLE_ERB_GRU 0
#endif
#ifndef MODEL_GOLDEN_TEST_DATA
#define MODEL_GOLDEN_TEST_DATA 0
#endif

#if M55M1_ENABLE_ERB_GRU
#include "services/audio_telemetry.h"
#include "services/erb_gru/erb_gru_audio_enhancer.h"
#include "services/erb_gru/erb_gru_inference.h"
#if MODEL_GOLDEN_TEST_DATA
#include "services/erb_gru/erb_gru_golden_runner.h"
#endif
#endif

#define APP_AUDIO_PROFILE TLV320DAC3100_AUDIO_PROFILE_16KHZ

static int16_t g_dmic_block[TLV320DAC3100_MAX_SAMPLES_PER_BLOCK];
#if M55M1_ENABLE_ERB_GRU
static int16_t g_enhanced_block[ERB_GRU_AUDIO_HOP_SAMPLES];
static audio_telemetry_snapshot_t g_telemetry;
#endif

static void audio_halt(const char *stage, int status)
{
    printf("[FAIL] %s (status=%d)\n", stage, status);
    for (;;) {
    }
}

static const char *audio_board_stage_name(m55m1_tlv320dac3100_board_stage_t stage)
{
    switch (stage) {
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
    case M55M1_TLV320DAC3100_BOARD_STAGE_SCLK_APLL0_READY:
        return "SCLK_APLL0_READY";
    case M55M1_TLV320DAC3100_BOARD_STAGE_MODULE_CLOCKS_READY:
        return "MODULE_CLOCKS_READY";
    case M55M1_TLV320DAC3100_BOARD_STAGE_AUDIO_PINS_READY:
        return "AUDIO_PINS_READY";
    case M55M1_TLV320DAC3100_BOARD_STAGE_I2C_RECOVERY_READY:
        return "I2C_RECOVERY_READY";
    case M55M1_TLV320DAC3100_BOARD_STAGE_I2C_PINS_READY:
        return "I2C_PINS_READY";
    case M55M1_TLV320DAC3100_BOARD_STAGE_I2C_RESET_READY:
        return "I2C_RESET_READY";
    case M55M1_TLV320DAC3100_BOARD_STAGE_CYCLE_COUNTER_READY:
        return "CYCLE_COUNTER_READY";
    case M55M1_TLV320DAC3100_BOARD_STAGE_PERIPHERALS_READY:
        return "PERIPHERALS_READY";
    default:
        return "UNKNOWN";
    }
}

static void audio_board_progress(m55m1_tlv320dac3100_board_stage_t stage, void *context)
{
    (void)context;
    printf("[BOOT] board stage=%d (%s)\n", (int)stage, audio_board_stage_name(stage));
}

#if M55M1_ENABLE_ERB_GRU
static void audio_update_telemetry(
    uint32_t elapsed_cycles, erb_gru_audio_result_t result)
{
    m55m1_tlv320dac3100_dmic_status_t dmic_status;
    tlv320dac3100_driver_status_t dac_status;
    erb_gru_inference_performance_t inference;

    g_telemetry.enhance_last_us =
        m55m1_tlv320dac3100_test_cycles_to_us(elapsed_cycles);
    audio_telemetry_record_processing_us(g_telemetry.enhance_last_us);
    if (g_telemetry.enhance_last_us > g_telemetry.enhance_max_us) {
        g_telemetry.enhance_max_us = g_telemetry.enhance_last_us;
    }
    if (result == ERB_GRU_AUDIO_RESULT_UNITY_FALLBACK) {
        ++g_telemetry.unity_fallback;
    }
    erb_gru_inference_get_performance(&inference);
    g_telemetry.invoke_last_us =
        m55m1_tlv320dac3100_test_cycles_to_us(inference.last_invoke_cycles);
    g_telemetry.invoke_max_us =
        m55m1_tlv320dac3100_test_cycles_to_us(inference.max_invoke_cycles);
    g_telemetry.deadline_miss = inference.deadline_miss_count;
    g_telemetry.npu_error = inference.invoke_error_count;
    g_telemetry.arena_used_bytes = (uint32_t)inference.arena_used_bytes;
    m55m1_tlv320dac3100_test_dmic_get_status(&dmic_status);
    tlv320dac3100_get_status(&dac_status);
    g_telemetry.dmic_overrun = dmic_status.overrun_blocks;
    g_telemetry.dmic_dma_error = dmic_status.dma_errors;
    g_telemetry.dac_underrun = dac_status.underrun_blocks;
    g_telemetry.dac_dma_error = dac_status.dma_errors;
}
#endif

int main(void)
{
    const tlv320dac3100_audio_profile_t profile = APP_AUDIO_PROFILE;
    const uint16_t sample_count = tlv320dac3100_profile_samples_per_block(profile);
    tlv320dac3100_config_t config;
    tlv320dac3100_status_t status;
    m55m1_tlv320dac3100_board_init_result_t board_status;

    m55m1_debug_uart_init();
    printf("\n[BOOT] ERB-GRU firmware starting\n");
    printf("[BOOT] audio profile=%u Hz, hop=%u samples\n",
           16000U, (unsigned int)sample_count);
    if (sample_count == 0U) {
        audio_halt("invalid audio profile", -1);
    }

    printf("[BOOT] board clock and pin setup\n");
    board_status = m55m1_tlv320dac3100_test_board_init_with_progress(
        profile, &config, NULL, NULL, audio_board_progress, NULL);
    if (board_status != M55M1_TLV320DAC3100_BOARD_INIT_OK) {
        audio_halt("board initialization", (int)board_status);
    }
    status = tlv320dac3100_init(&config);
    if (status != TLV320DAC3100_STATUS_OK) {
        audio_halt("codec initialization", (int)status);
    }
    printf("[BOOT] codec initialized\n");
    printf("[BOOT] output gain=%d dB\n", M55M1_TLV320DAC3100_SAFE_MAX_GAIN_DB);

#if M55M1_ENABLE_ERB_GRU
    if (!erb_gru_audio_enhancer_init()) {
        audio_halt("Ethos-U55 and TFLM initialization", -1);
    }
#if MODEL_GOLDEN_TEST_DATA
    {
        erb_gru_golden_result_t golden;
        const bool golden_ok = erb_gru_golden_run(&golden);

        printf("[GOLDEN] %s frames=%u max=%u signal=%llu noise=%llu\r\n",
               golden_ok ? "PASS" : "FAIL", (unsigned int)golden.processed_frames,
               (unsigned int)golden.max_absolute_error,
               (unsigned long long)golden.signal_energy,
               (unsigned long long)golden.noise_energy);
        for (;;) {
        }
    }
#endif
    audio_telemetry_init();
    printf("[BOOT] Ethos-U55 and TFLM initialized\n");
    printf("[ERB-GRU] 16 kHz Ethos-U55 inference enabled\n");
#endif

    printf("[BOOT] starting I2S DAC and DMIC; polling UART disabled\n");
    status = tlv320dac3100_start();
    if (status != TLV320DAC3100_STATUS_OK) {
        audio_halt("I2S DAC start", (int)status);
    }
    if (!m55m1_tlv320dac3100_test_dmic_start(profile)) {
#if M55M1_ENABLE_ERB_GRU
        audio_telemetry_fatal("DMIC capture start", -1);
#endif
        for (;;) {
        }
    }

    for (;;) {
        m55m1_tlv320dac3100_dmic_block_timing_t capture_timing;

        if (m55m1_tlv320dac3100_test_dmic_take_block_timed(
                g_dmic_block, sample_count, &capture_timing)) {
#if M55M1_ENABLE_ERB_GRU
            const uint32_t enhance_start =
                m55m1_tlv320dac3100_test_cycles_now();
            const erb_gru_audio_result_t result =
                erb_gru_audio_enhancer_process(g_dmic_block, g_enhanced_block);

            if (result == ERB_GRU_AUDIO_RESULT_INVALID) {
                continue;
            }
#if M55M1_ERB_GRU_FAULT_DAC_BUSY
            status = TLV320DAC3100_STATUS_BUSY;
#else
            status = tlv320dac3100_submit_mono_block_tagged(
                g_enhanced_block, sample_count,
                &(tlv320dac3100_audio_block_tag_t){
                    .sequence = capture_timing.sequence,
                    .capture_completed_cycle = capture_timing.completed_cycle,
                });
#endif
            if (status == TLV320DAC3100_STATUS_BUSY) {
                ++g_telemetry.dac_busy;
            } else if (status != TLV320DAC3100_STATUS_OK) {
                audio_telemetry_fatal("DAC submit", (int)status);
                for (;;) {
                }
            }
            ++g_telemetry.frame_count;
            audio_update_telemetry(
                m55m1_tlv320dac3100_test_cycles_now() - enhance_start, result);
#else
            status = tlv320dac3100_submit_mono_block(g_dmic_block, sample_count);
#endif
        }
#if M55M1_ENABLE_ERB_GRU
        audio_telemetry_poll(
            &g_telemetry, m55m1_tlv320dac3100_test_dmic_block_pending());
#endif
    }
}
