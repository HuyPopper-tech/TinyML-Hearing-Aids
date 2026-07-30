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
void audio_pipeline_stop_safe(void);

#ifdef __cplusplus
}
#endif
