/**
 * @file audio_pipeline.c
 * @brief Foreground DMIC to PCM16 passthrough to TLV320DAC3100 pipeline.
 * @author Bui Minh Huy
 * @date 2026-09-13
 */

#include <stdbool.h>
#include <stdint.h>

#include "drivers/audio_transport/audio_transport.h"
#include "services/audio_pipeline/audio_pipeline.h"
#include "services/enhancement_passthrough/enhancement_passthrough.h"

#ifndef M55M1_ENABLE_DAC_I2S_OUTPUT
#define M55M1_ENABLE_DAC_I2S_OUTPUT 0
#endif

typedef struct {
    int16_t capture[AUDIO_TRANSPORT_SAMPLES_PER_BLOCK];
    int16_t playback[AUDIO_TRANSPORT_SAMPLES_PER_BLOCK];
} audio_working_set_t;

static audio_working_set_t g_audio_working_set;
static bool g_initialized;
static bool g_running;

bool audio_pipeline_init(void) {
    audio_transport_init_report_t transport_report = {0};

    if (g_initialized) {
        return true;
    }
    if (!audio_transport_init(&transport_report, NULL, NULL) ||
        !transport_report.clocks_valid) {
        return false;
    }

    g_initialized = true;
    return true;
}

bool audio_pipeline_start(void) {
    if (!g_initialized || g_running ||
        (audio_transport_start() != AUDIO_TRANSPORT_STATUS_OK)) {
        return false;
    }

    g_running = true;
    return true;
}

void audio_pipeline_poll(void) {
    audio_transport_capture_timing_t timing;

    if (!g_running ||
        !audio_transport_take_capture(g_audio_working_set.capture, &timing)) {
        return;
    }

    if (!enhancement_passthrough_process(
            g_audio_working_set.capture,
            g_audio_working_set.playback,
            AUDIO_TRANSPORT_SAMPLES_PER_BLOCK)) {
        return;
    }

#if M55M1_ENABLE_DAC_I2S_OUTPUT
    (void)audio_transport_submit_tagged(
        g_audio_working_set.playback,
        &(audio_transport_block_tag_t){
            .sequence = timing.sequence,
            .capture_completed_cycle = timing.completed_cycle,
        });
#endif

}

void audio_pipeline_stop_safe(void) {
    if (g_running) {
        audio_transport_stop_safe();
    }

    g_initialized = false;
    g_running = false;
}
