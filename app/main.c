/**
 * @file main.c
 * @brief Bare-metal boot composition for the production audio service.
 * @author Bui Minh Huy
 * @date 2026-07-30
 */

#include "services/audio_pipeline/audio_pipeline.h"

int main(void)
{
    if (!audio_pipeline_init() || !audio_pipeline_start()) {
        audio_pipeline_stop_safe();
        for (;;) {
        }
    }

    for (;;) {
        audio_pipeline_poll();
    }
}
