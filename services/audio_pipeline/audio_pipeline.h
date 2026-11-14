/**
 * @file audio_pipeline.h
 * @brief Public lifecycle for the foreground production audio pipeline.
 * @author Bui Minh Huy
 * @date 2026-07-30
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /** Contiguous DMIC hops observed before inference; cumulative until board reset. */
    uint64_t input_samples;
    /** Exact PCM24 endpoints (-8388608 or +8388607), not all distortion. */
    uint64_t input_clip_samples;
    /** Largest absolute PCM24 code observed, in the range 0..8388608. */
    uint32_t input_peak;
    uint32_t sequence_faults;
    uint32_t deadline_misses;
    uint32_t model_faults;
    uint32_t transport_faults;
    uint32_t restarts;
    uint32_t restart_failures;
    uint32_t max_capture_to_publish_us;
    uint32_t last_sequence;
    /** Logical PCM positions reset with the stream; physical alignment needs measurement. */
    uint64_t capture_sample_position;
    uint32_t scheduled_playback_slot;
    uint32_t capture_completed_cycle;
    uint32_t process_start_cycle;
    uint32_t process_end_cycle;
    uint32_t published_cycle;
} audio_pipeline_stats_t;

/** Foreground cumulative counters survive automatic stream restarts. */
void audio_pipeline_get_stats(audio_pipeline_stats_t *stats);

/**
 * @brief Compose the static audio transport, enhancer, and telemetry services.
 * @return True when the pipeline is ready to start.
 *
 * Foreground-only before audio starts. No dynamic allocation occurs after a
 * successful initialization.
 */
bool audio_pipeline_init(void);

/**
 * @brief Start capture and playback after successful initialization.
 * @return True when the active audio lifecycle begins.
 *
 * Foreground-only. A stopped lifecycle must be initialized again.
 */
bool audio_pipeline_start(void);

/**
 * @brief Process currently available foreground audio work.
 * @return None.
 *
 * Call from the bare-metal event loop, never from an ISR. The function is
 * non-reentrant and does not block waiting for audio or telemetry.
 */
void audio_pipeline_poll(void);

/** Foreground event-loop idle after poll; skipped when the stream is stopped.
 * Capture/playback clocks continue; each captured hop still invokes the model.
 */
void audio_pipeline_wait_for_work(void);

/**
 * @brief Quiesce capture and playback and terminate the current lifecycle.
 * @return None.
 *
 * After this call audio_pipeline_start() returns false. Re-initialize the
 * pipeline with audio_pipeline_init() before starting a new lifecycle.
 */
void audio_pipeline_stop_safe(void);

#ifdef __cplusplus
}
#endif
