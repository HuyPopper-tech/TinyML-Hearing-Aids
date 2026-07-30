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

bool audio_pipeline_init(void);
bool audio_pipeline_start(void);
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
