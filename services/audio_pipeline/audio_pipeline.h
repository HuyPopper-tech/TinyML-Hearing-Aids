/**
 * @file audio_pipeline.h
 * @brief Public lifecycle for the foreground production audio pipeline.
 * @author Bui Minh Huy
 * @date 2026-07-30
 */

#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

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
