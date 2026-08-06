/**
 * @file audio_pipeline.c
 * @brief Foreground DMIC, DeepFilterNet, and DAC service.
 * @author Bui Minh Huy
 * @date 2026-08-06
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "drivers/audio_transport/audio_transport.h"
#include "services/audio_pipeline/audio_pipeline.h"

#if M55M1_DEEPFILTERNET_DIAGNOSTICS
#include "models/deepfilternet/generated/deepfilternet_deploy_contract.h"
#include "services/audio_telemetry.h"
#endif

#ifndef M55M1_ENABLE_DEEPFILTERNET
#define M55M1_ENABLE_DEEPFILTERNET 0
#endif
#ifndef M55M1_DEEPFILTERNET_DIAGNOSTICS
#define M55M1_DEEPFILTERNET_DIAGNOSTICS 0
#endif
#ifndef M55M1_DEEPFILTERNET_FAULT_DAC_BUSY
#define M55M1_DEEPFILTERNET_FAULT_DAC_BUSY 0
#endif
#if M55M1_DEEPFILTERNET_FAULT_INVOKE_FAIL || \
    M55M1_DEEPFILTERNET_FAULT_INVOKE_SLOW || M55M1_DEEPFILTERNET_FAULT_DAC_BUSY
#define AUDIO_PIPELINE_FAULT_INJECTED 1U
#else
#define AUDIO_PIPELINE_FAULT_INJECTED 0U
#endif

#if M55M1_ENABLE_DEEPFILTERNET
#include "services/deepfilternet/deepfilternet_audio_enhancer.h"
#endif

static int16_t g_dmic_block[AUDIO_TRANSPORT_SAMPLES_PER_BLOCK];
static int16_t g_enhanced_block[AUDIO_TRANSPORT_SAMPLES_PER_BLOCK];
static bool g_initialized;
static bool g_running;
static bool g_polling;
static bool g_have_capture_sequence;
static uint32_t g_last_capture_sequence;
static bool g_pending_tag_valid;
static audio_transport_block_tag_t g_pending_tag;

#if M55M1_DEEPFILTERNET_DIAGNOSTICS
static uint32_t g_unity_fallback_count;
static uint32_t g_npu_error_count;
static uint32_t g_reset_count;
static uint32_t g_dac_busy_count;
static uint32_t g_transport_error_count;
#endif

static void audio_pipeline_report_failure(const char *stage, int status) {
#if M55M1_DEEPFILTERNET_DIAGNOSTICS
    audio_telemetry_fatal(stage, status);
#else
    (void)stage;
    (void)status;
#endif
}

static void audio_pipeline_halt(const char *stage, int status) {
    audio_pipeline_stop_safe();
    audio_pipeline_report_failure(stage, status);
    for (;;) {
    }
}

#if M55M1_DEEPFILTERNET_DIAGNOSTICS
static void audio_pipeline_board_progress(
    audio_transport_board_stage_t stage, void *context) {
    (void)context;
    (void)stage;
}
#endif

bool audio_pipeline_init(void) {
    audio_transport_init_report_t report;

#if M55M1_DEEPFILTERNET_DIAGNOSTICS
    audio_telemetry_boot_info_t boot_info = {
        .build = "deepfilternet",
        .git_rev = M55M1_DEEPFILTERNET_GIT_REV,
        .git_dirty = M55M1_DEEPFILTERNET_GIT_DIRTY,
        .dsp_mode = M55M1_DEEPFILTERNET_DSP_MODE,
        .hop_samples = DEEPFILTERNET_AUDIO_HOP_SAMPLES,
        .frame_samples = DEEPFILTERNET_AUDIO_FRAME_SAMPLES,
        .input_gain_db = M55M1_DEEPFILTERNET_INPUT_GAIN_DB,
        .core_hz = 0U,
        .model_hash = DFN_MODEL_SHA256,
        .model_bytes = DFN_MODEL_BYTES,
    };
#endif

    if (g_initialized) {
        return true;
    }
#if M55M1_DEEPFILTERNET_DIAGNOSTICS
    audio_telemetry_init();
#endif
    if (!audio_transport_init(
            &report,
#if M55M1_DEEPFILTERNET_DIAGNOSTICS
            audio_pipeline_board_progress,
#else
            NULL,
#endif
            NULL)) {
        audio_pipeline_report_failure(
            (report.board_status != 0) ? "board initialization" :
                                         "codec initialization",
            (report.board_status != 0) ? report.board_status :
                                         report.codec_status);
        return false;
    }
    if (!report.clocks_valid) {
        audio_pipeline_report_failure("board clock snapshot", -1);
        return false;
    }
#if M55M1_DEEPFILTERNET_DIAGNOSTICS
    boot_info.core_hz = report.core_hz;
    audio_telemetry_boot(&boot_info);
#endif
#if M55M1_ENABLE_DEEPFILTERNET
    if (!deepfilternet_audio_enhancer_init()) {
        audio_pipeline_report_failure("DeepFilterNet initialization",
                                      deepfilternet_audio_enhancer_last_init_status());
        return false;
    }
    deepfilternet_audio_enhancer_reset();
#if M55M1_DEEPFILTERNET_DIAGNOSTICS
    g_unity_fallback_count = 0U;
    g_npu_error_count = 0U;
    g_reset_count = 0U;
    g_dac_busy_count = 0U;
    g_transport_error_count = 0U;
#endif
#endif
    g_have_capture_sequence = false;
    g_initialized = true;
    return true;
}

bool audio_pipeline_start(void) {
    if (!g_initialized || g_running) {
        return false;
    }
    if (audio_transport_start() != AUDIO_TRANSPORT_STATUS_OK) {
        audio_pipeline_report_failure("I2S DAC and DMIC start", -1);
        return false;
    }
    g_running = true;
    return true;
}

void audio_pipeline_poll(void) {
    audio_transport_capture_timing_t capture_timing;
    audio_transport_status_t status;
    audio_transport_block_tag_t current_tag;
#if M55M1_DEEPFILTERNET_DIAGNOSTICS
    uint32_t enhance_start;
    uint32_t enhance_elapsed_us;
    audio_transport_completion_t completion;
    audio_transport_health_t health;
    audio_telemetry_snapshot_t snapshot;
    deepfilternet_audio_diagnostics_t enhancer_diagnostics;
#endif

    if (!g_running || g_polling) {
        return;
    }
    g_polling = true;
    if (!audio_transport_take_capture(g_dmic_block, &capture_timing)) {
        g_polling = false;
        return;
    }

#if M55M1_ENABLE_DEEPFILTERNET
    if (g_have_capture_sequence &&
        capture_timing.sequence != (g_last_capture_sequence + 1U)) {
        deepfilternet_audio_enhancer_discontinuity();
#if M55M1_DEEPFILTERNET_DIAGNOSTICS
        ++g_reset_count;
#endif
        g_pending_tag_valid = false;
    }
    g_last_capture_sequence = capture_timing.sequence;
    g_have_capture_sequence = true;
    /* Exactly one enhancer call and one 256-sample submission per capture. */
#if M55M1_DEEPFILTERNET_DIAGNOSTICS
    enhance_start = audio_transport_cycles_now();
#endif
    {
        const deepfilternet_audio_result_t result =
            deepfilternet_audio_enhancer_process(g_dmic_block, g_enhanced_block);
#if M55M1_DEEPFILTERNET_DIAGNOSTICS
        if (result == DEEPFILTERNET_AUDIO_RESULT_UNITY_FALLBACK) {
            ++g_unity_fallback_count;
            ++g_npu_error_count;
        } else if (result == DEEPFILTERNET_AUDIO_RESULT_INVALID) {
            ++g_npu_error_count;
        }
#else
        (void)result;
#endif
    }
#if M55M1_DEEPFILTERNET_DIAGNOSTICS
    enhance_elapsed_us = audio_transport_cycles_to_us(
        audio_transport_cycles_elapsed(enhance_start, audio_transport_cycles_now()));
    audio_telemetry_record_processing_us(enhance_elapsed_us);
    audio_telemetry_record_e2e_us(enhance_elapsed_us, capture_timing.sequence);
    deepfilternet_audio_enhancer_get_diagnostics(&enhancer_diagnostics);
    audio_telemetry_record_fft_us(enhancer_diagnostics.fft_us);
    audio_telemetry_record_feature_us(enhancer_diagnostics.feature_us);
    audio_telemetry_record_invoke_us(enhancer_diagnostics.npu_us);
    audio_telemetry_record_df_us(enhancer_diagnostics.df_us);
    audio_telemetry_record_wola_us(enhancer_diagnostics.wola_us);
#endif
#else
    for (uint32_t i = 0U; i < AUDIO_TRANSPORT_SAMPLES_PER_BLOCK; ++i) {
        g_enhanced_block[i] = g_dmic_block[i];
    }
#endif

    current_tag = (audio_transport_block_tag_t){
        .sequence = capture_timing.sequence,
        .capture_completed_cycle = capture_timing.completed_cycle,
    };
#if M55M1_DEEPFILTERNET_FAULT_DAC_BUSY
    status = AUDIO_TRANSPORT_STATUS_BUSY;
#elif M55M1_ENABLE_DEEPFILTERNET
    if (g_pending_tag_valid) {
        status = audio_transport_submit_tagged(g_enhanced_block, &g_pending_tag);
    } else {
        status = audio_transport_submit(g_enhanced_block);
    }
#else
    status = audio_transport_submit(g_enhanced_block);
#endif
    if (status != AUDIO_TRANSPORT_STATUS_OK &&
        status != AUDIO_TRANSPORT_STATUS_BUSY) {
#if M55M1_DEEPFILTERNET_DIAGNOSTICS
        ++g_transport_error_count;
#endif
        audio_pipeline_halt("DAC submit", (int)status);
    }
#if M55M1_DEEPFILTERNET_DIAGNOSTICS
    if (status == AUDIO_TRANSPORT_STATUS_BUSY) {
        ++g_dac_busy_count;
    }
#endif
    g_pending_tag = current_tag;
    g_pending_tag_valid = true;
#if M55M1_DEEPFILTERNET_DIAGNOSTICS
    while (audio_transport_take_completion(&completion)) {
        audio_telemetry_record_transport_us(
            audio_transport_cycles_to_us(audio_transport_cycles_elapsed(
                completion.capture_completed_cycle, completion.dac_completed_cycle)),
            completion.sequence);
    }
    audio_transport_get_health(&health);
    snapshot = (audio_telemetry_snapshot_t){
        .frame_count = capture_timing.sequence,
        .unity_fallback = enhancer_diagnostics.fallback_count,
        .dmic_overrun = health.dmic_overrun_blocks,
        .dmic_dma_error = health.dmic_dma_errors,
        .dac_busy = g_dac_busy_count,
        .dac_underrun = health.dac_underrun_blocks,
        .dac_dma_error = health.dac_dma_errors,
        .npu_error = enhancer_diagnostics.npu_error_count,
        .parity_max_pcm_difference =
            enhancer_diagnostics.parity_max_pcm_difference,
        .parity_violation_count = enhancer_diagnostics.parity_violation_count,
        .deadline_miss = enhancer_diagnostics.deadline_miss_count,
        .fault_injected = AUDIO_PIPELINE_FAULT_INJECTED,
        .reset_count = enhancer_diagnostics.reset_count + g_reset_count,
        .transport_error = g_transport_error_count,
        .model_flash_bytes = DFN_MODEL_BYTES,
        .model_hash = DFN_MODEL_SHA256,
        .model_hash_valid = 1U,
        .hop_samples = DEEPFILTERNET_AUDIO_HOP_SAMPLES,
        .frame_samples = DEEPFILTERNET_AUDIO_FRAME_SAMPLES,
        .diagnostics_enabled = 1U,
        .stack_configured_bytes = audio_transport_stack_configured_bytes(),
        .stack_high_water_bytes = audio_transport_stack_high_water_bytes(),
        .stack_headroom_bytes = audio_transport_stack_headroom_bytes(),
        .invoke_count = enhancer_diagnostics.invoke_count,
        .arena_used_bytes = enhancer_diagnostics.arena_used_bytes,
        .arena_capacity_bytes = enhancer_diagnostics.arena_capacity_bytes,
        .macs_per_inference = DFN_MODEL_MACS_PER_FRAME,
        .npu_operator_count = DFN_MODEL_NPU_OPERATOR_COUNT,
        .cpu_operator_count = DFN_MODEL_CPU_OPERATOR_COUNT,
        .dsp_static_bytes = enhancer_diagnostics.dsp_static_bytes,
    };
    snapshot.audio = (audio_telemetry_audio_stats_t){
        .frames = 1U,
        .pcm_samples = enhancer_diagnostics.pcm_samples,
        .input_energy = enhancer_diagnostics.input_energy,
        .model_input_energy = enhancer_diagnostics.model_input_energy,
        .output_energy = enhancer_diagnostics.output_energy,
        .input_peak = enhancer_diagnostics.input_peak,
        .model_input_peak = enhancer_diagnostics.model_input_peak,
        .output_peak = enhancer_diagnostics.output_peak,
        .input_clip_count = enhancer_diagnostics.input_clip_count,
        .model_input_clip_count = enhancer_diagnostics.model_input_clip_count,
        .output_clip_count = enhancer_diagnostics.output_clip_count,
        .feature_min = enhancer_diagnostics.feature_min,
        .feature_max = enhancer_diagnostics.feature_max,
        .feature_sat_low = enhancer_diagnostics.feature_sat_count,
        .complex_min = enhancer_diagnostics.complex_min,
        .complex_max = enhancer_diagnostics.complex_max,
        .complex_sat_count = enhancer_diagnostics.complex_sat_count,
        .gain_min = enhancer_diagnostics.gain_min,
        .gain_max = enhancer_diagnostics.gain_max,
        .gain_sat_count = enhancer_diagnostics.gain_sat_count,
        .df_min = enhancer_diagnostics.df_min,
        .df_max = enhancer_diagnostics.df_max,
        .df_sat_count = enhancer_diagnostics.df_sat_count,
        .alpha_min = enhancer_diagnostics.alpha_min,
        .alpha_max = enhancer_diagnostics.alpha_max,
        .alpha_sat_count = enhancer_diagnostics.alpha_sat_count,
        .erb_cache_min = enhancer_diagnostics.state_min[0],
        .erb_cache_max = enhancer_diagnostics.state_max[0],
        .erb_cache_sat_count = enhancer_diagnostics.state_sat_count[0],
        .enc0_min = enhancer_diagnostics.state_min[1],
        .enc0_max = enhancer_diagnostics.state_max[1],
        .enc0_sat_count = enhancer_diagnostics.state_sat_count[1],
        .enc1_min = enhancer_diagnostics.state_min[2],
        .enc1_max = enhancer_diagnostics.state_max[2],
        .enc1_sat_count = enhancer_diagnostics.state_sat_count[2],
        .enc2_min = enhancer_diagnostics.state_min[3],
        .enc2_max = enhancer_diagnostics.state_max[3],
        .enc2_sat_count = enhancer_diagnostics.state_sat_count[3],
        .df0_min = enhancer_diagnostics.state_min[4],
        .df0_max = enhancer_diagnostics.state_max[4],
        .df0_sat_count = enhancer_diagnostics.state_sat_count[4],
        .df1_min = enhancer_diagnostics.state_min[5],
        .df1_max = enhancer_diagnostics.state_max[5],
        .df1_sat_count = enhancer_diagnostics.state_sat_count[5],
        .complex_cache_min = enhancer_diagnostics.state_min[6],
        .complex_cache_max = enhancer_diagnostics.state_max[6],
        .complex_cache_sat_count = enhancer_diagnostics.state_sat_count[6],
    };
    audio_telemetry_record_audio_stats(&snapshot.audio);
    audio_telemetry_poll(&snapshot, audio_transport_capture_pending());
#endif
    g_polling = false;
}

void audio_pipeline_stop_safe(void) {
    if (g_initialized || g_running) {
        audio_transport_stop_safe();
    }
#if M55M1_ENABLE_DEEPFILTERNET
    deepfilternet_audio_enhancer_discontinuity();
#if M55M1_DEEPFILTERNET_DIAGNOSTICS
    ++g_reset_count;
#endif
#endif
    g_initialized = false;
    g_running = false;
    g_polling = false;
    g_have_capture_sequence = false;
    g_pending_tag_valid = false;
}
