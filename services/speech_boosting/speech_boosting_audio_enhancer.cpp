/**
 * @file speech_boosting_audio_enhancer.cpp
 * @brief PCM16 adapter for the Speech Boosting Chunk64 stream model.
 * @author Bui Minh Huy
 * @date 2026-08-27
 */

#include <cmath>
#include <cstdint>
#include <cstring>

#include "drivers/audio_transport/audio_transport.h"
#include "models/speech_boosting/speech_boosting_dsp_contract.h"
#include "services/speech_boosting/speech_boosting_audio_enhancer.h"

namespace {

speech_boosting_audio_diagnostics_t g_diagnostics;

uint32_t elapsed_microseconds(uint32_t start_cycle) {
    return audio_transport_cycles_to_us(
        audio_transport_cycles_elapsed(start_cycle, audio_transport_cycles_now()));
}

int16_t dequantize_pcm16(int8_t value, uint32_t *saturation_count) {
    const float normalized =
        (static_cast<float>(value) -
         static_cast<float>(SPEECH_BOOSTING_AUDIO_OUTPUT_ZERO_POINT)) *
        SPEECH_BOOSTING_AUDIO_OUTPUT_SCALE;
    const int32_t pcm = static_cast<int32_t>(std::round(normalized * 32768.0f));

    if (pcm < -32768) {
        ++(*saturation_count);
        return -32768;
    }
    if (pcm > 32767) {
        ++(*saturation_count);
        return 32767;
    }
    return static_cast<int16_t>(pcm);
}

}  // namespace

extern "C" bool speech_boosting_audio_enhancer_init(void) {
    return speech_boosting_inference_init();
}

extern "C" void speech_boosting_audio_enhancer_reset(
    speech_boosting_audio_stream_t *stream) {
    if (stream == nullptr) {
        return;
    }
    std::memset(stream, 0, sizeof(*stream));
    speech_boosting_inference_reset(&stream->state);
}

extern "C" bool speech_boosting_audio_enhancer_process(
    speech_boosting_audio_stream_t *stream,
    const int16_t capture[SPEECH_BOOSTING_CAPTURE_SAMPLES],
    int16_t output[SPEECH_BOOSTING_CHUNK_SAMPLES]) {
    int8_t packed[SPEECH_BOOSTING_CHUNK_SAMPLES]
                 [SPEECH_BOOSTING_INPUT_CHANNELS];
    int8_t model_output[SPEECH_BOOSTING_CHUNK_SAMPLES];
    uint32_t start_cycle;
    uint32_t saturation_count = 0U;
    speech_boosting_inference_diagnostics_t inference_diagnostics;

    if ((stream == nullptr) || (capture == nullptr) || (output == nullptr)) {
        return false;
    }

    start_cycle = audio_transport_cycles_now();
    speech_boosting_pack_capture_int8(capture, packed);
    g_diagnostics.lookahead_assembly_and_pcm_quantization_microseconds =
        elapsed_microseconds(start_cycle);

    if (!speech_boosting_inference_invoke(
            packed, &stream->state, model_output)) {
        g_diagnostics.neural_network_invocation_microseconds = 0U;
        g_diagnostics.output_dequantization_microseconds = 0U;
        ++g_diagnostics.invoke_error_count;
        return false;
    }
    speech_boosting_inference_get_diagnostics(&inference_diagnostics);
    g_diagnostics.neural_network_invocation_microseconds =
        inference_diagnostics.neural_network_invocation_microseconds;

    start_cycle = audio_transport_cycles_now();
    for (uint32_t index = 0U; index < SPEECH_BOOSTING_CHUNK_SAMPLES; ++index) {
        output[index] = dequantize_pcm16(model_output[index], &saturation_count);
    }
    g_diagnostics.output_dequantization_microseconds =
        elapsed_microseconds(start_cycle);
    g_diagnostics.output_saturation_count += saturation_count;
    return true;
}

extern "C" void speech_boosting_audio_enhancer_get_diagnostics(
    speech_boosting_audio_diagnostics_t *diagnostics) {
    if (diagnostics != nullptr) {
        *diagnostics = g_diagnostics;
    }
}
