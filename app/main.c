/**
 * @file main.c
 * @brief Bare-metal boot composition for the production audio service.
 * @author Bui Minh Huy
 * @date 2026-07-30
 */

#include "services/audio_pipeline/audio_pipeline.h"
#if M55M1_DEEPFIR_COMPILED_REPLAY
#include "services/deepfir/deepfir_inference_service.h"
#endif

int main(void) {
    if (!audio_pipeline_init()
#if !M55M1_DEEPFIR_COMPILED_REPLAY
        || !audio_pipeline_start()
#endif
    ) {
        audio_pipeline_stop_safe();
        for (;;) {
        }
    }

    for (;;) {
#if M55M1_DEEPFIR_COMPILED_REPLAY
        deepfir_inference_replay_poll();
#else
        audio_pipeline_poll();
        audio_pipeline_wait_for_work();
#endif
    }
}
