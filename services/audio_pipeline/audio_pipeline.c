/**
 * @file audio_pipeline.c
 * @brief Foreground DMIC, ERB-GRU, WOLA tagging, DAC, and telemetry service.
 * @author Bui Minh Huy
 * @date 2026-07-30
 *
 * The service is foreground-only and non-reentrant. DMA interrupt handlers
 * retain ownership of descriptor movement and only publish fixed-size events.
 */

#include <stddef.h>
#include <stdint.h>
#if M55M1_ERB_GRU_DIAGNOSTICS
#include <stdio.h>
#endif

#if M55M1_ERB_GRU_DIAGNOSTICS
#include "board/m55m1_debug_uart.h"
#endif
#include "drivers/audio_transport/audio_transport.h"
#include "services/audio_pipeline/audio_pipeline.h"

#ifndef M55M1_ENABLE_ERB_GRU
#define M55M1_ENABLE_ERB_GRU 0
#endif
#ifndef MODEL_GOLDEN_TEST_DATA
#define MODEL_GOLDEN_TEST_DATA 0
#endif
#ifndef M55M1_ERB_GRU_DIAGNOSTICS
#define M55M1_ERB_GRU_DIAGNOSTICS 0
#endif

#if M55M1_ENABLE_ERB_GRU
#if M55M1_ERB_GRU_DIAGNOSTICS
#include "services/audio_telemetry.h"
#include "services/erb_gru/erb_gru_stream_timing.h"
#endif
#include "services/erb_gru/erb_gru_audio_enhancer.h"
#include "services/erb_gru/erb_gru_inference.h"
#if MODEL_GOLDEN_TEST_DATA
#include "services/erb_gru/erb_gru_golden_runner.h"
#endif
#endif

static int16_t g_dmic_block[AUDIO_TRANSPORT_SAMPLES_PER_BLOCK];
#if M55M1_ENABLE_ERB_GRU
static int16_t g_enhanced_block[ERB_GRU_AUDIO_HOP_SAMPLES];
#if M55M1_ERB_GRU_DIAGNOSTICS
static audio_telemetry_snapshot_t g_telemetry;
static erb_gru_stream_timing_t g_stream_timing;
#endif
#endif
static bool g_initialized;
static bool g_running;
static bool g_polling;

static void audio_pipeline_report_failure(const char *stage, int status)
{
#if M55M1_ERB_GRU_DIAGNOSTICS
    audio_telemetry_fatal(stage, status);
#else
    (void)stage;
    (void)status;
#endif
}

static void audio_pipeline_halt(const char *stage, int status)
{
    audio_pipeline_report_failure(stage, status);
    audio_pipeline_stop_safe();
    for (;;) {
    }
}

#if M55M1_ERB_GRU_DIAGNOSTICS
static const char *audio_pipeline_board_stage_name(
    audio_transport_board_stage_t stage)
{
    switch (stage) {
    case AUDIO_TRANSPORT_BOARD_STAGE_ENTER:
        return "ENTER";
    case AUDIO_TRANSPORT_BOARD_STAGE_SCLK_HIRC_READY:
        return "SCLK_HIRC_READY";
    case AUDIO_TRANSPORT_BOARD_STAGE_HXT_WAIT:
        return "HXT_WAIT";
    case AUDIO_TRANSPORT_BOARD_STAGE_HXT_READY:
        return "HXT_READY";
    case AUDIO_TRANSPORT_BOARD_STAGE_APLL1_WAIT:
        return "APLL1_WAIT";
    case AUDIO_TRANSPORT_BOARD_STAGE_APLL1_READY:
        return "APLL1_READY";
    case AUDIO_TRANSPORT_BOARD_STAGE_APLL0_WAIT:
        return "APLL0_WAIT";
    case AUDIO_TRANSPORT_BOARD_STAGE_APLL0_READY:
        return "APLL0_READY";
    case AUDIO_TRANSPORT_BOARD_STAGE_SCLK_APLL0_READY:
        return "SCLK_APLL0_READY";
    case AUDIO_TRANSPORT_BOARD_STAGE_MODULE_CLOCKS_READY:
        return "MODULE_CLOCKS_READY";
    case AUDIO_TRANSPORT_BOARD_STAGE_AUDIO_PINS_READY:
        return "AUDIO_PINS_READY";
    case AUDIO_TRANSPORT_BOARD_STAGE_I2C_RECOVERY_READY:
        return "I2C_RECOVERY_READY";
    case AUDIO_TRANSPORT_BOARD_STAGE_I2C_PINS_READY:
        return "I2C_PINS_READY";
    case AUDIO_TRANSPORT_BOARD_STAGE_I2C_RESET_READY:
        return "I2C_RESET_READY";
    case AUDIO_TRANSPORT_BOARD_STAGE_CYCLE_COUNTER_READY:
        return "CYCLE_COUNTER_READY";
    case AUDIO_TRANSPORT_BOARD_STAGE_PERIPHERALS_READY:
        return "PERIPHERALS_READY";
    default:
        return "UNKNOWN";
    }
}

static void audio_pipeline_board_progress(
    audio_transport_board_stage_t stage,
    void *context)
{
    (void)context;
    printf("[BOOT] board stage=%d (%s)\n",
           (int)stage, audio_pipeline_board_stage_name(stage));
}
#endif

#if M55M1_ENABLE_ERB_GRU && M55M1_ERB_GRU_DIAGNOSTICS
static void audio_pipeline_update_telemetry(
    uint32_t elapsed_cycles,
    erb_gru_audio_result_t result)
{
    audio_transport_health_t health;
    erb_gru_inference_performance_t inference;

    g_telemetry.enhance_last_us =
        audio_transport_cycles_to_us(elapsed_cycles);
    audio_telemetry_record_processing_us(g_telemetry.enhance_last_us);
    if (g_telemetry.enhance_last_us > g_telemetry.enhance_max_us) {
        g_telemetry.enhance_max_us = g_telemetry.enhance_last_us;
    }
    if (result == ERB_GRU_AUDIO_RESULT_UNITY_FALLBACK) {
        ++g_telemetry.unity_fallback;
    }
    erb_gru_inference_get_performance(&inference);
    g_telemetry.invoke_last_us =
        audio_transport_cycles_to_us(inference.last_invoke_cycles);
    if (inference.invoke_count != g_telemetry.invoke_count) {
        audio_telemetry_record_invoke_us(g_telemetry.invoke_last_us);
    }
    g_telemetry.invoke_max_us =
        audio_transport_cycles_to_us(inference.max_invoke_cycles);
    g_telemetry.invoke_count = inference.invoke_count;
    g_telemetry.deadline_miss = inference.deadline_miss_count;
    g_telemetry.npu_error = inference.invoke_error_count;
    g_telemetry.fault_injected = inference.fault_injected_count;
    g_telemetry.arena_used_bytes = (uint32_t)inference.arena_used_bytes;
    g_telemetry.arena_capacity_bytes = (uint32_t)inference.arena_capacity_bytes;
    g_telemetry.dsp_static_bytes = erb_gru_audio_enhancer_static_bytes();
    g_telemetry.stack_configured_bytes = M55M1_STACK_SIZE_BYTES;
    g_telemetry.macs_per_inference = inference.macs_per_inference;
    g_telemetry.npu_operator_count = inference.npu_operator_count;
    g_telemetry.cpu_operator_count = inference.cpu_operator_count;
    g_telemetry.model_flash_bytes = inference.model_flash_bytes;
    audio_transport_get_health(&health);
    g_telemetry.dmic_overrun = health.dmic_overrun_blocks;
    g_telemetry.dmic_dma_error = health.dmic_dma_errors;
    g_telemetry.dac_underrun = health.dac_underrun_blocks;
    g_telemetry.dac_dma_error = health.dac_dma_errors;
}
#endif

bool audio_pipeline_init(void)
{
    audio_transport_init_report_t report;

    if (g_initialized) {
        return true;
    }
#if M55M1_ERB_GRU_DIAGNOSTICS
    m55m1_debug_uart_init();
    printf("\n[BOOT] ERB-GRU firmware starting\n");
    printf("[BUILD] type=%s diagnostics=1 gain_db=%d "
           "fault=%s git=%s dirty=%d\n",
           M55M1_ERB_GRU_BUILD_TYPE,
           M55M1_ERB_GRU_INPUT_GAIN_DB,
           M55M1_ERB_GRU_FAULT_MODE_NAME,
           M55M1_ERB_GRU_GIT_REV,
           M55M1_ERB_GRU_GIT_DIRTY);
    printf("[BOOT] audio profile=%lu Hz, hop=%u samples\n",
           (unsigned long)AUDIO_TRANSPORT_SAMPLE_RATE_HZ,
           AUDIO_TRANSPORT_SAMPLES_PER_BLOCK);
    printf("[BOOT] board clock and pin setup\n");
#endif
    if (!audio_transport_init(
            &report,
#if M55M1_ERB_GRU_DIAGNOSTICS
            audio_pipeline_board_progress,
#else
            NULL,
#endif
            NULL)) {
        audio_pipeline_report_failure(
            (report.board_status != 0) ?
                "board initialization" : "codec initialization",
            (report.board_status != 0) ?
                report.board_status : report.codec_status);
        return false;
    }
    if (!report.clocks_valid) {
        audio_pipeline_report_failure("board clock snapshot", -1);
        return false;
    }
#if M55M1_ERB_GRU_DIAGNOSTICS
    printf("[BOOT] clocks core_hz=%lu apll0_hz=%lu apll1_hz=%lu "
           "i2s_src_hz=%lu mclk_hz=%lu\n",
           (unsigned long)report.core_hz,
           (unsigned long)report.apll0_hz,
           (unsigned long)report.apll1_hz,
           (unsigned long)report.i2s_src_hz,
           (unsigned long)report.mclk_hz);
    printf("[BOOT] codec initialized\n");
    printf("[BOOT] output gain=%d dB\n", AUDIO_TRANSPORT_SAFE_MAX_GAIN_DB);
#endif

#if M55M1_ENABLE_ERB_GRU
    if (!erb_gru_audio_enhancer_init()) {
        audio_pipeline_report_failure(
            "Ethos-U55 and TFLM initialization", -1);
        return false;
    }
#if MODEL_GOLDEN_TEST_DATA
    {
        erb_gru_golden_result_t golden;
        const bool golden_ok = erb_gru_golden_run(&golden);

        printf("[GOLDEN] %s frames=%u max=%u signal=%llu noise=%llu\r\n",
               golden_ok ? "PASS" : "FAIL",
               (unsigned int)golden.processed_frames,
               (unsigned int)golden.max_absolute_error,
               (unsigned long long)golden.signal_energy,
               (unsigned long long)golden.noise_energy);
        for (;;) {
        }
    }
#endif
#if M55M1_ERB_GRU_DIAGNOSTICS
    audio_telemetry_init();
    erb_gru_stream_timing_reset(&g_stream_timing);
    printf("[BOOT] Ethos-U55 and TFLM initialized\n");
    printf("[ERB-GRU] 16 kHz Ethos-U55 inference enabled\n");
#endif
#endif
    g_initialized = true;
    return true;
}

bool audio_pipeline_start(void)
{
    if (!g_initialized || g_running) {
        return false;
    }
#if M55M1_ERB_GRU_DIAGNOSTICS
    printf("[BOOT] starting I2S DAC and DMIC; polling UART disabled\n");
#endif
    if (audio_transport_start() != AUDIO_TRANSPORT_STATUS_OK) {
        audio_pipeline_report_failure("I2S DAC and DMIC start", -1);
        return false;
    }
    g_running = true;
    return true;
}

void audio_pipeline_poll(void)
{
    audio_transport_capture_timing_t capture_timing;

    if (!g_running || g_polling) {
        return;
    }
    g_polling = true;
    if (audio_transport_take_capture(g_dmic_block, &capture_timing)) {
#if M55M1_ENABLE_ERB_GRU
#if M55M1_ERB_GRU_DIAGNOSTICS
        const uint32_t enhance_start = audio_transport_cycles_now();
#endif
        const erb_gru_audio_result_t result =
            erb_gru_audio_enhancer_process(g_dmic_block, g_enhanced_block);
        audio_transport_status_t status = AUDIO_TRANSPORT_STATUS_OK;

        if (result == ERB_GRU_AUDIO_RESULT_INVALID) {
            g_polling = false;
            return;
        }
        if (result != ERB_GRU_AUDIO_RESULT_INVALID) {
#if M55M1_ERB_GRU_FAULT_DAC_BUSY
            status = AUDIO_TRANSPORT_STATUS_BUSY;
#else
#if M55M1_ERB_GRU_DIAGNOSTICS
            {
                erb_gru_source_tag_t source_tag;
                const bool source_valid = erb_gru_stream_timing_advance(
                    &g_stream_timing,
                    capture_timing.sequence,
                    capture_timing.completed_cycle,
                    &source_tag);

                if (source_valid) {
                    status = audio_transport_submit_tagged(
                        g_enhanced_block,
                        &(audio_transport_block_tag_t){
                            .sequence = source_tag.sequence,
                            .capture_completed_cycle =
                                source_tag.capture_completed_cycle,
                        });
                } else {
                    status = audio_transport_submit(g_enhanced_block);
                }
            }
#else
            status = audio_transport_submit(g_enhanced_block);
#endif
#endif
            if (status == AUDIO_TRANSPORT_STATUS_BUSY) {
#if M55M1_ERB_GRU_DIAGNOSTICS
                ++g_telemetry.dac_busy;
#endif
            } else if (status != AUDIO_TRANSPORT_STATUS_OK) {
                audio_pipeline_halt("DAC submit", (int)status);
            }
#if M55M1_ERB_GRU_DIAGNOSTICS
            ++g_telemetry.frame_count;
            audio_pipeline_update_telemetry(
                audio_transport_cycles_elapsed(
                    enhance_start, audio_transport_cycles_now()),
                result);
            if ((g_telemetry.frame_count % 100U) == 0U) {
                (void)erb_gru_audio_enhancer_take_diagnostics(
                    &g_telemetry.audio);
            }
#endif
        }
#else
        (void)audio_transport_submit(g_dmic_block);
#endif
    }
#if M55M1_ENABLE_ERB_GRU && M55M1_ERB_GRU_DIAGNOSTICS
    {
        audio_transport_completion_t completion;

        if (audio_transport_take_completion(&completion)) {
            audio_telemetry_record_e2e_us(
                audio_transport_cycles_to_us(
                    audio_transport_cycles_elapsed(
                        completion.capture_completed_cycle,
                        completion.dac_completed_cycle)),
                completion.sequence);
        }
    }
    audio_telemetry_poll(&g_telemetry, audio_transport_capture_pending());
#endif
    g_polling = false;
}

void audio_pipeline_stop_safe(void)
{
    if (g_running) {
        audio_transport_stop_safe();
    }
    g_running = false;
    g_polling = false;
}
