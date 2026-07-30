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

/** @brief Compose static audio services and hardware. @return True when initialized; foreground-only and resettable only through stop. */
bool audio_pipeline_init(void);
/** @brief Start capture/playback after initialization. @return True when the terminal lifecycle begins; foreground-only. */
bool audio_pipeline_start(void);
/** @brief Process at most available foreground audio work. @return None. Call from the bare-metal event loop, never an ISR. */
void audio_pipeline_poll(void);
/**
 * @brief Quiesce capture and playback and terminate the current lifecycle.
 *
 * After this call audio_pipeline_start() returns false. Re-initialize the
 * pipeline with audio_pipeline_init() before starting a new lifecycle.
 */
void audio_pipeline_stop_safe(void);

#ifdef __cplusplus
}
#endif
