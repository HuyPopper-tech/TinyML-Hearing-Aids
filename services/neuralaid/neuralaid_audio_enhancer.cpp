/**
 * @file neuralaid_audio_enhancer.cpp
 * @brief Contract-exact NeuralAid 96-sample streaming DSP.
 * @author Bui Minh Huy
 * @date 2026-08-24
 */

#include <cmath>
#include <cstring>

#include "arm_math.h"
#include "drivers/audio_transport/audio_transport.h"
#include "models/neuralaid/generated/neuralaid_contract.h"
#include "models/neuralaid/generated/neuralaid_window_coeffs.h"
#include "services/neuralaid/neuralaid_audio_enhancer.h"

namespace {

constexpr uint32_t kFftLength = NEURALAID_FFT_LENGTH;
constexpr uint32_t kFftBins = NEURALAID_FFT_BINS;
constexpr uint32_t kLookbackSamples = NEURALAID_LOOKBACK_SAMPLES;
constexpr uint32_t kChunkSamples = NEURALAID_CHUNK_SAMPLES;
constexpr uint32_t kLookaheadSamples = NEURALAID_LOOKAHEAD_SAMPLES;
constexpr uint32_t kSynthesisSamples = kChunkSamples + kLookaheadSamples;
constexpr float kPcmFullScale = 32768.0f;
constexpr float kModelInputTfScale = 0.01358956191688776f;
constexpr int32_t kModelInputTfZeroPoint = -15;
constexpr float kModelOutputTfScale = 0.012382527813315392f;
constexpr int32_t kModelOutputTfZeroPoint = -37;
constexpr int32_t kPreemphasisQ15 = NEURALAID_PREEMPHASIS_Q15;

static_assert(kFftLength == kLookbackSamples + kChunkSamples + kLookaheadSamples,
              "NeuralAid context must be [lookback|chunk|lookahead]");
static_assert(kSynthesisSamples == 160U, "NeuralAid synthesis contract changed");

arm_rfft_fast_instance_f32 g_fft;
int g_status;
neuralaid_audio_diagnostics_t g_diagnostics;

uint32_t elapsed_us(uint32_t start_cycle) {
    return audio_transport_cycles_to_us(audio_transport_cycles_elapsed(
        start_cycle, audio_transport_cycles_now()));
}

int16_t saturate_int16(int32_t value) {
    if (value < -32768) {
        ++g_diagnostics.saturation_count;
        return -32768;
    }
    if (value > 32767) {
        ++g_diagnostics.saturation_count;
        return 32767;
    }
    return static_cast<int16_t>(value);
}

int8_t quantize_input(float value) {
    const int32_t quantized = static_cast<int32_t>(std::round(
        value / kModelInputTfScale + static_cast<float>(kModelInputTfZeroPoint)));
    return (quantized < -128) ? -128 :
           (quantized > 127) ? 127 : static_cast<int8_t>(quantized);
}

int16_t float_to_pcm(float value) {
    return saturate_int16(static_cast<int32_t>(std::round(value * kPcmFullScale)));
}

int16_t ring_sample(const neuralaid_audio_stream_t *stream, int32_t sample_index) {
    if (sample_index < 0) {
        return 0;
    }
    return stream->ring[static_cast<uint32_t>(sample_index) & (kFftLength - 1U)];
}

void build_context(const neuralaid_audio_stream_t *stream, float time[kFftLength]) {
    const int32_t frame_start = static_cast<int32_t>(stream->logical_frame * kChunkSamples);

    for (uint32_t index = 0U; index < kFftLength; ++index) {
        const int32_t sample_index = frame_start - static_cast<int32_t>(kLookbackSamples) +
            static_cast<int32_t>(index);
        time[index] = static_cast<float>(ring_sample(stream, sample_index)) / kPcmFullScale;
    }
}

void cmsis_rfft_packed_to_tf(const float packed[kFftLength],
                             int8_t tf_frame[NEURALAID_TF_VALUES]) {
    tf_frame[0] = quantize_input(packed[0]);
    tf_frame[1] = quantize_input(0.0f);
    for (uint32_t bin = 1U; bin < (kFftBins - 1U); ++bin) {
        tf_frame[2U * bin] = quantize_input(packed[2U * bin]);
        tf_frame[2U * bin + 1U] = quantize_input(packed[2U * bin + 1U]);
    }
    tf_frame[2U * (kFftBins - 1U)] = quantize_input(packed[1]);
    tf_frame[2U * (kFftBins - 1U) + 1U] = quantize_input(0.0f);
}

void tf_to_cmsis_rfft_packed(const int8_t tf_frame[NEURALAID_TF_VALUES],
                             float packed[kFftLength]) {
    packed[0] = (static_cast<int32_t>(tf_frame[0]) - kModelOutputTfZeroPoint) *
        kModelOutputTfScale;
    packed[1] = (static_cast<int32_t>(tf_frame[2U * (kFftBins - 1U)]) -
        kModelOutputTfZeroPoint) * kModelOutputTfScale;
    for (uint32_t bin = 1U; bin < (kFftBins - 1U); ++bin) {
        packed[2U * bin] = (static_cast<int32_t>(tf_frame[2U * bin]) -
            kModelOutputTfZeroPoint) * kModelOutputTfScale;
        packed[2U * bin + 1U] = (static_cast<int32_t>(tf_frame[2U * bin + 1U]) -
            kModelOutputTfZeroPoint) * kModelOutputTfScale;
    }
}

void synthesize_ola(neuralaid_audio_stream_t *stream, const float ifft[kFftLength],
                    int16_t output[kChunkSamples]) {
    for (uint32_t index = 0U; index < kLookaheadSamples; ++index) {
        const float weight = g_neuralaid_synthesis_window[index] +
            stream->ola_weight_tail[index];
        const float short_sample = ifft[kLookbackSamples + index] *
            g_neuralaid_synthesis_window[index];
        output[index] = float_to_pcm((short_sample + stream->ola_tail[index]) / weight);
    }
    for (uint32_t index = kLookaheadSamples; index < kChunkSamples; ++index) {
        const float short_sample = ifft[kLookbackSamples + index] *
            g_neuralaid_synthesis_window[index];
        output[index] = float_to_pcm(short_sample / g_neuralaid_synthesis_window[index]);
    }
    for (uint32_t index = 0U; index < kLookaheadSamples; ++index) {
        const uint32_t short_index = kChunkSamples + index;
        stream->ola_tail[index] = ifft[kLookbackSamples + short_index] *
            g_neuralaid_synthesis_window[short_index];
        stream->ola_weight_tail[index] = g_neuralaid_synthesis_window[short_index];
    }
}

}  // namespace

extern "C" bool neuralaid_audio_enhancer_init(void) {
    if (arm_rfft_fast_init_f32(&g_fft, kFftLength) != ARM_MATH_SUCCESS) {
        g_status = -1;
        return false;
    }
    if (!neuralaid_inference_init()) {
        g_status = neuralaid_inference_last_init_status();
        return false;
    }
    g_status = 1;
    return true;
}

extern "C" int neuralaid_audio_enhancer_last_init_status(void) {
    return g_status;
}

extern "C" void neuralaid_audio_enhancer_reset(neuralaid_audio_stream_t *stream) {
    if (stream == nullptr) {
        return;
    }
    std::memset(stream, 0, sizeof(*stream));
    neuralaid_inference_reset(&stream->state);
}

extern "C" void neuralaid_audio_enhancer_get_diagnostics(
    neuralaid_audio_diagnostics_t *diagnostics) {
    if (diagnostics != nullptr) {
        *diagnostics = g_diagnostics;
    }
}

extern "C" neuralaid_audio_result_t neuralaid_audio_enhancer_process(
    neuralaid_audio_stream_t *stream,
    const int16_t input[NEURALAID_AUDIO_CHUNK_SAMPLES],
    int16_t output[NEURALAID_AUDIO_CHUNK_SAMPLES]) {
    float time[kFftLength];
    float packed[kFftLength];
    float enhanced[kFftLength];
    int8_t frame[NEURALAID_TF_VALUES];
    int8_t model_output[NEURALAID_TF_VALUES];
    neuralaid_stream_state_t candidate_state;
    uint32_t stage_start;
    const uint32_t total_start = audio_transport_cycles_now();

    if ((stream == nullptr) || (input == nullptr) || (output == nullptr)) {
        return NEURALAID_AUDIO_PCM_BYPASS;
    }

    stage_start = audio_transport_cycles_now();
    for (uint32_t index = 0U; index < kChunkSamples; ++index) {
        const int32_t preemphasized = static_cast<int32_t>(input[index]) -
            ((kPreemphasisQ15 * static_cast<int32_t>(stream->previous_sample)) >> 15);
        stream->previous_sample = input[index];
        stream->ring[stream->write_index++ & (kFftLength - 1U)] =
            saturate_int16(preemphasized);
        ++stream->samples_seen;
    }
    g_diagnostics.preemphasis_us = elapsed_us(stage_start);
    g_diagnostics.logical_frame = stream->logical_frame;
    g_diagnostics.required_samples = stream->logical_frame * kChunkSamples +
        kChunkSamples + kLookaheadSamples;
    if (stream->samples_seen < g_diagnostics.required_samples) {
        std::memset(output, 0, sizeof(int16_t) * kChunkSamples);
        ++g_diagnostics.preroll_count;
        g_diagnostics.total_us = elapsed_us(total_start);
        return NEURALAID_AUDIO_PREROLL_SILENCE;
    }

    stage_start = audio_transport_cycles_now();
    build_context(stream, time);
    g_diagnostics.context_us = elapsed_us(stage_start);
    stage_start = audio_transport_cycles_now();
    arm_rfft_fast_f32(&g_fft, time, packed, 0);
    g_diagnostics.rfft_us = elapsed_us(stage_start);
    stage_start = audio_transport_cycles_now();
    cmsis_rfft_packed_to_tf(packed, frame);
    g_diagnostics.quantize_us = elapsed_us(stage_start);

    candidate_state = stream->state;
    stage_start = audio_transport_cycles_now();
    if (!neuralaid_inference_invoke(frame, &candidate_state, model_output)) {
        g_diagnostics.invoke_us = elapsed_us(stage_start);
        std::memcpy(output, input, sizeof(int16_t) * kChunkSamples);
        neuralaid_audio_enhancer_reset(stream);
        ++g_diagnostics.bypass_count;
        ++g_diagnostics.invoke_error_count;
        g_diagnostics.total_us = elapsed_us(total_start);
        return NEURALAID_AUDIO_PCM_BYPASS;
    }
    g_diagnostics.invoke_us = elapsed_us(stage_start);
    stage_start = audio_transport_cycles_now();
    tf_to_cmsis_rfft_packed(model_output, packed);
    arm_rfft_fast_f32(&g_fft, packed, enhanced, 1);
    synthesize_ola(stream, enhanced, output);
    g_diagnostics.ifft_ola_us = elapsed_us(stage_start);

    stream->state = candidate_state;
    ++stream->logical_frame;
    g_diagnostics.total_us = elapsed_us(total_start);
    return NEURALAID_AUDIO_ENHANCED;
}
