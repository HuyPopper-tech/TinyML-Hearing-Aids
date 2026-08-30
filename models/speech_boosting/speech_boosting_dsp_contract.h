#pragma once
#include <math.h>
#include <stdint.h>

#include "generated/speech_boosting_generated_contract.h"
#include "speech_boosting_model_contract.h"

/*
Input tensor: [1, 1, 64, 2]

packed[t][0] = PCM waveform at time t
packed[t][1] = PCM waveform at time t + LOOKAHEAD

The caller must provide a capture buffer of exactly 80 PCM16 samples:
64 current samples + 16 future samples.
*/

static inline int8_t speech_boosting_pcm16_to_int8(int16_t pcm) {
    const float waveform = (float)pcm / 32768.0f;

    int32_t quantized = (int32_t)lroundf(
        waveform / SPEECH_BOOSTING_AUDIO_INPUT_SCALE
        + SPEECH_BOOSTING_AUDIO_INPUT_ZERO_POINT
    );

    if (quantized < -128) quantized = -128;
    if (quantized > 127) quantized = 127;

    return (int8_t)quantized;
}

static inline void speech_boosting_pack_capture_int8(
    const int16_t capture[SPEECH_BOOSTING_CAPTURE_SAMPLES],
    int8_t packed[SPEECH_BOOSTING_CHUNK_SAMPLES][2]
) {
    for (uint32_t t = 0; t < SPEECH_BOOSTING_CHUNK_SAMPLES; ++t) {
        packed[t][0] = speech_boosting_pcm16_to_int8(capture[t]);
        packed[t][1] = speech_boosting_pcm16_to_int8(
            capture[t + SPEECH_BOOSTING_LOOKAHEAD_SAMPLES]
        );
    }
}
