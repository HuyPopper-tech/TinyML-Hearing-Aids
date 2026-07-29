/**
 * @file erb_gru_audio_enhancer.cpp
 * @brief Static ERB-GRU frontend, synthesis, recurrent state, and recovery.
 * @author Bui Minh Huy
 * @date 2026-07-29
 *
 * Foreground-only and non-reentrant. The first 10 ms hop primes the 320-sample
 * analysis frame without invoking the NPU. Invoke failure uses unity gains
 * through the same WOLA path so the overlap state remains continuous.
 */

#include <cmath>
#include <cstdint>
#include <cstring>

#include "models/erb_gru/generated/erb_gru_assets.h"
#include "services/erb_gru/erb_gru_audio_enhancer.h"
#if M55M1_ERB_GRU_DIAGNOSTICS
#include "services/erb_gru/erb_gru_audio_diagnostics.h"
#endif
#include "services/erb_gru/erb_gru_inference.h"
#include "kiss_fft.h"
#include "tools/kiss_fftr.h"

#ifndef M55M1_ERB_GRU_INPUT_GAIN_DB
#define M55M1_ERB_GRU_INPUT_GAIN_DB 0
#endif

#if (M55M1_ERB_GRU_INPUT_GAIN_DB != 0) && (M55M1_ERB_GRU_INPUT_GAIN_DB != 12)
#error "M55M1_ERB_GRU_INPUT_GAIN_DB must be 0 or 12"
#endif

namespace {

constexpr uint32_t kHopSamples = 160U;
constexpr uint32_t kFftSamples = 320U;
constexpr uint32_t kFftBins = 161U;
constexpr uint32_t kBands = 32U;
constexpr float kPcmScale = 32768.0f;
constexpr float kLogEpsilon = 1.0e-6f;
#if M55M1_ERB_GRU_INPUT_GAIN_DB == 12
constexpr int32_t kInputGainMultiplier = 4;
#else
constexpr int32_t kInputGainMultiplier = 1;
#endif

alignas(16) float g_analysis_ring[kFftSamples];
alignas(16) float g_synthesis_overlap[kHopSamples];
alignas(16) kiss_fft_scalar g_fft_input[kFftSamples];
alignas(16) kiss_fft_scalar g_ifft_output[kFftSamples];
alignas(16) kiss_fft_cpx g_spectrum[kFftBins];
alignas(16) kiss_fft_cpx g_scaled_spectrum[kFftBins];
alignas(16) float g_magnitude[kFftBins];
alignas(16) float g_features[kBands];
alignas(16) float g_gains[kBands];
alignas(16) float g_bin_gains[kFftBins];
alignas(16) int8_t g_feature_q[kBands];
alignas(16) int8_t g_gain_q[kBands];
alignas(16) int8_t g_state1[ERB_GRU_INFERENCE_STATE_SIZE];
alignas(16) int8_t g_state2[ERB_GRU_INFERENCE_STATE_SIZE];
alignas(16) int8_t g_next_state1[ERB_GRU_INFERENCE_STATE_SIZE];
alignas(16) int8_t g_next_state2[ERB_GRU_INFERENCE_STATE_SIZE];
#if M55M1_ERB_GRU_DIAGNOSTICS
alignas(16) int16_t g_diagnostic_source_pcm[kHopSamples];
#endif
alignas(16) uint8_t g_forward_memory[4096];
alignas(16) uint8_t g_inverse_memory[4096];
kiss_fftr_cfg g_forward_fft;
kiss_fftr_cfg g_inverse_fft;
bool g_have_first_hop;
bool g_have_processed_frame;
bool g_ready;

int8_t quantize(float value, float scale, int zero_point)
{
    int32_t quantized = static_cast<int32_t>(std::lround(value / scale)) + zero_point;
    quantized = (quantized < -128) ? -128 : ((quantized > 127) ? 127 : quantized);
    return static_cast<int8_t>(quantized);
}

float dequantize(int8_t value, float scale, int zero_point)
{
    return static_cast<float>(static_cast<int32_t>(value) - zero_point) * scale;
}

int16_t float_to_pcm(float value)
{
    const float clipped = (value < -1.0f) ? -1.0f : ((value > 0.999969482421875f) ?
        0.999969482421875f : value);
    return static_cast<int16_t>(std::lround(clipped * kPcmScale));
}

float input_to_float(int16_t sample)
{
    int32_t scaled = static_cast<int32_t>(sample) * kInputGainMultiplier;

    scaled = (scaled > INT16_MAX) ? INT16_MAX : scaled;
    scaled = (scaled < INT16_MIN) ? INT16_MIN : scaled;
    return static_cast<float>(scaled) / kPcmScale;
}

void reset_recurrent_state()
{
    std::memset(g_state1, ERB_GRU_STATE1_ZERO_POINT, sizeof(g_state1));
    std::memset(g_state2, ERB_GRU_STATE2_ZERO_POINT, sizeof(g_state2));
}

void append_hop(const int16_t input[kHopSamples])
{
    if (g_have_processed_frame)
    {
        std::memmove(g_analysis_ring, g_analysis_ring + kHopSamples,
                     kHopSamples * sizeof(g_analysis_ring[0]));
    }
    for (uint32_t index = 0U; index < kHopSamples; ++index)
    {
        const uint32_t destination = g_have_first_hop ? (index + kHopSamples) : index;
        g_analysis_ring[destination] = input_to_float(input[index]);
    }
}

void analyze_frame()
{
    for (uint32_t index = 0U; index < kFftSamples; ++index)
    {
        g_fft_input[index] = g_analysis_ring[index] * sqrt_hann_320[index];
    }
    kiss_fftr(g_forward_fft, g_fft_input, g_spectrum);

    for (uint32_t bin = 0U; bin < kFftBins; ++bin)
    {
        const float real = g_spectrum[bin].r;
        const float imaginary = g_spectrum[bin].i;
        g_magnitude[bin] = std::sqrt((real * real) + (imaginary * imaginary));
    }
    for (uint32_t band = 0U; band < kBands; ++band)
    {
        float feature = 0.0f;

        for (uint32_t bin = 0U; bin < kFftBins; ++bin)
        {
            feature += g_magnitude[bin] * erb_analysis_161x32[(bin * kBands) + band];
        }
        feature = std::log((feature > kLogEpsilon) ? feature : kLogEpsilon);
        g_features[band] = feature;
        g_feature_q[band] = quantize(feature, ERB_GRU_FEATURE_SCALE, ERB_GRU_FEATURE_ZERO_POINT);
    }
}

void expand_gains(bool inference_ok)
{
    for (uint32_t bin = 0U; bin < kFftBins; ++bin)
    {
        float bin_gain = inference_ok ? 0.0f : 1.0f;

        if (inference_ok)
        {
            for (uint32_t band = 0U; band < kBands; ++band)
            {
                bin_gain += g_gains[band] *
                    erb_gain_expansion_32x161[(band * kFftBins) + bin];
            }
        }
        g_bin_gains[bin] = bin_gain;
        g_scaled_spectrum[bin].r = g_spectrum[bin].r * bin_gain;
        g_scaled_spectrum[bin].i = g_spectrum[bin].i * bin_gain;
    }
}

void synthesize_hop(int16_t output[kHopSamples])
{
    kiss_fftri(g_inverse_fft, g_scaled_spectrum, g_ifft_output);
    for (uint32_t index = 0U; index < kHopSamples; ++index)
    {
        const float first = (g_ifft_output[index] / kFftSamples) *
            wola_synthesis_320[index] * wola_normalization_320[index];
        const uint32_t overlap_index = index + kHopSamples;
        const float second = (g_ifft_output[overlap_index] / kFftSamples) *
            wola_synthesis_320[overlap_index] * wola_normalization_320[overlap_index];

        output[index] = float_to_pcm(first + g_synthesis_overlap[index]);
        g_synthesis_overlap[index] = second;
    }
}

} // namespace

extern "C" void erb_gru_audio_enhancer_reset(void)
{
    std::memset(g_analysis_ring, 0, sizeof(g_analysis_ring));
    std::memset(g_synthesis_overlap, 0, sizeof(g_synthesis_overlap));
    reset_recurrent_state();
#if M55M1_ERB_GRU_DIAGNOSTICS
    std::memset(g_diagnostic_source_pcm, 0, sizeof(g_diagnostic_source_pcm));
    erb_gru_audio_diagnostics_reset();
#endif
    g_have_first_hop = false;
    g_have_processed_frame = false;
}

extern "C" bool erb_gru_audio_enhancer_init(void)
{
    size_t forward_bytes = sizeof(g_forward_memory);
    size_t inverse_bytes = sizeof(g_inverse_memory);

    if (g_ready)
    {
        return true;
    }
    g_forward_fft = kiss_fftr_alloc(kFftSamples, 0, g_forward_memory, &forward_bytes);
    g_inverse_fft = kiss_fftr_alloc(kFftSamples, 1, g_inverse_memory, &inverse_bytes);
    if ((g_forward_fft == nullptr) || (g_inverse_fft == nullptr) || !erb_gru_inference_init())
    {
        return false;
    }
    erb_gru_audio_enhancer_reset();
    g_ready = true;
    return true;
}

extern "C" erb_gru_audio_result_t erb_gru_audio_enhancer_process(
    const int16_t input[kHopSamples], int16_t output[kHopSamples])
{
    bool inference_ok;

    if ((input == nullptr) || (output == nullptr) || !g_ready)
    {
        if (output != nullptr)
        {
            std::memset(output, 0, kHopSamples * sizeof(output[0]));
        }
        return ERB_GRU_AUDIO_RESULT_INVALID;
    }

    append_hop(input);
    if (!g_have_first_hop)
    {
        std::memset(output, 0, kHopSamples * sizeof(output[0]));
#if M55M1_ERB_GRU_DIAGNOSTICS
        std::memcpy(g_diagnostic_source_pcm, input, sizeof(g_diagnostic_source_pcm));
#endif
        g_have_first_hop = true;
        return ERB_GRU_AUDIO_RESULT_PREROLL_SILENCE;
    }

    analyze_frame();
#if M55M1_ERB_GRU_DIAGNOSTICS
    erb_gru_audio_diagnostics_record_feature(g_feature_q, kBands);
#endif
    inference_ok = erb_gru_inference_invoke(
        g_feature_q, g_state1, g_state2, g_gain_q, g_next_state1, g_next_state2);
    if (inference_ok)
    {
        std::memcpy(g_state1, g_next_state1, sizeof(g_state1));
        std::memcpy(g_state2, g_next_state2, sizeof(g_state2));
        for (uint32_t band = 0U; band < kBands; ++band)
        {
            g_gains[band] = dequantize(g_gain_q[band], ERB_GRU_GAIN_SCALE,
                                       ERB_GRU_GAIN_ZERO_POINT);
        }
#if M55M1_ERB_GRU_DIAGNOSTICS
        erb_gru_audio_diagnostics_record_gain_state(
            g_gain_q, kBands, g_next_state1, g_next_state2,
            ERB_GRU_INFERENCE_STATE_SIZE);
#endif
    }
    expand_gains(inference_ok);
    synthesize_hop(output);
#if M55M1_ERB_GRU_DIAGNOSTICS
    erb_gru_audio_diagnostics_record_pcm(
        g_diagnostic_source_pcm, output, kHopSamples);
    std::memcpy(g_diagnostic_source_pcm, input, sizeof(g_diagnostic_source_pcm));
#endif
    g_have_processed_frame = true;
    if (!inference_ok)
    {
        reset_recurrent_state();
        return ERB_GRU_AUDIO_RESULT_UNITY_FALLBACK;
    }
    return ERB_GRU_AUDIO_RESULT_ENHANCED;
}

#if M55M1_ERB_GRU_DIAGNOSTICS
extern "C" bool erb_gru_audio_enhancer_take_diagnostics(
    erb_gru_audio_diagnostics_t *diagnostics)
{
    return erb_gru_audio_diagnostics_take(diagnostics);
}

extern "C" uint32_t erb_gru_audio_enhancer_static_bytes(void)
{
    return (uint32_t)(
        sizeof(g_analysis_ring) + sizeof(g_synthesis_overlap) +
        sizeof(g_fft_input) + sizeof(g_ifft_output) + sizeof(g_spectrum) +
        sizeof(g_scaled_spectrum) + sizeof(g_magnitude) + sizeof(g_features) +
        sizeof(g_gains) + sizeof(g_bin_gains) + sizeof(g_feature_q) +
        sizeof(g_gain_q) + sizeof(g_state1) + sizeof(g_state2) +
        sizeof(g_next_state1) + sizeof(g_next_state2) +
        sizeof(g_diagnostic_source_pcm) + sizeof(g_forward_memory) +
        sizeof(g_inverse_memory));
}
#endif

#if MODEL_GOLDEN_TEST_DATA
extern "C" bool erb_gru_audio_enhancer_flush(int16_t output[kHopSamples])
{
    if ((output == nullptr) || !g_ready || !g_have_first_hop)
    {
        return false;
    }
    for (uint32_t index = 0U; index < kHopSamples; ++index)
    {
        output[index] = float_to_pcm(g_synthesis_overlap[index]);
        g_synthesis_overlap[index] = 0.0f;
    }
    return true;
}
#endif
