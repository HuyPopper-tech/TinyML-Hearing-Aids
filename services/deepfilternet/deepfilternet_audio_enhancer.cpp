/**
 * @file deepfilternet_audio_enhancer.cpp
 * @brief Scalar and CMSIS-DSP/MVE 512/256 DeepFilterNet DSP service.
 * @author Bui Minh Huy
 * @date 2026-08-06
 */

#include <cmath>
#include <climits>
#include <cstdint>
#include <cstring>

#ifndef M55M1_DEEPFILTERNET_DIAGNOSTICS
#define M55M1_DEEPFILTERNET_DIAGNOSTICS 0
#endif
#ifndef M55M1_DEEPFILTERNET_INPUT_GAIN_DB
#define M55M1_DEEPFILTERNET_INPUT_GAIN_DB 0
#endif
#if (M55M1_DEEPFILTERNET_INPUT_GAIN_DB != 0) && \
    (M55M1_DEEPFILTERNET_INPUT_GAIN_DB != 12)
#error "M55M1_DEEPFILTERNET_INPUT_GAIN_DB must be 0 or 12"
#endif
#ifndef M55M1_DEEPFILTERNET_DSP_MVE
#define M55M1_DEEPFILTERNET_DSP_MVE 0
#endif
#ifndef M55M1_DEEPFILTERNET_DSP_PARITY
#define M55M1_DEEPFILTERNET_DSP_PARITY 0
#endif
#ifndef M55M1_DEEPFILTERNET_DSP_SCALAR
#if !M55M1_DEEPFILTERNET_DSP_MVE && !M55M1_DEEPFILTERNET_DSP_PARITY
#define M55M1_DEEPFILTERNET_DSP_SCALAR 1
#else
#define M55M1_DEEPFILTERNET_DSP_SCALAR 0
#endif
#endif

#if M55M1_DEEPFILTERNET_DIAGNOSTICS
#include "drivers/audio_transport/audio_transport.h"
#endif
#include "services/deepfilternet/deepfilternet_audio_enhancer.h"
#include "services/deepfilternet/deepfilternet_inference.h"
#include "models/deepfilternet/generated/deepfilternet_frontend_contract.h"
#include "models/deepfilternet/generated/dsp_constants.h"
#if M55M1_DEEPFILTERNET_DSP_MVE || M55M1_DEEPFILTERNET_DSP_PARITY
#include "arm_math.h"
#endif
#if M55M1_DEEPFILTERNET_DSP_SCALAR || M55M1_DEEPFILTERNET_DSP_PARITY
#include "kiss_fft.h"
#include "tools/kiss_fftr.h"
#endif

namespace {

constexpr uint32_t kFrameSamples = DEEPFILTERNET_AUDIO_FRAME_SAMPLES;
constexpr uint32_t kHopSamples = DEEPFILTERNET_AUDIO_HOP_SAMPLES;
constexpr uint32_t kBins = 257U;
constexpr uint32_t kBands = 32U;
constexpr uint32_t kDfBins = 144U;
constexpr uint32_t kDfTaps = 5U;
constexpr float kPcmScale = 32768.0f;
constexpr float kLogEpsilon = 1.0e-6f;
constexpr int32_t kInputGainMultiplier =
    (M55M1_DEEPFILTERNET_INPUT_GAIN_DB == 12) ? 4 : 1;

struct complex_f32_t {
    float r;
    float i;
};

alignas(16) float g_analysis_ring[kFrameSamples];
alignas(16) float g_overlap[kHopSamples];
#if M55M1_DEEPFILTERNET_DSP_SCALAR || M55M1_DEEPFILTERNET_DSP_PARITY
alignas(16) kiss_fft_scalar g_fft_input[kFrameSamples];
alignas(16) kiss_fft_scalar g_ifft_output[kFrameSamples];
alignas(16) uint8_t g_forward_memory[8192];
alignas(16) uint8_t g_inverse_memory[8192];
kiss_fftr_cfg g_forward_fft;
kiss_fftr_cfg g_inverse_fft;
#endif
alignas(16) complex_f32_t g_spectrum[kBins];
alignas(16) complex_f32_t g_filtered[kBins];
alignas(16) complex_f32_t g_df_history[kDfTaps][kDfBins];
alignas(16) float g_magnitude[kBins];
alignas(16) int8_t g_erb_q[kBands];
alignas(16) int8_t g_complex_q[kDfBins * 2U];
alignas(16) int8_t g_gain_q[kBins];
alignas(16) int8_t g_df_q[kDfTaps * kDfBins * 2U];
int8_t g_alpha_q;
#if M55M1_DEEPFILTERNET_DSP_MVE || M55M1_DEEPFILTERNET_DSP_PARITY
arm_rfft_fast_instance_f32 g_mve_fft;
alignas(16) float g_mve_time[kFrameSamples];
alignas(16) float g_mve_packed[kFrameSamples];
alignas(16) float g_mve_windowed[kFrameSamples];
alignas(16) float g_mve_erb_weights[kBands * kBins];
alignas(16) float g_mve_gain[kBins * 2U];
alignas(16) float g_mve_coefficients[kDfTaps * kDfBins * 2U];
alignas(16) float g_mve_accumulator[kDfBins * 2U];
alignas(16) float g_mve_temporary[kDfBins * 2U];
#endif
#if M55M1_DEEPFILTERNET_DSP_PARITY
alignas(16) complex_f32_t g_parity_filtered[kBins];
alignas(16) complex_f32_t g_parity_history[kDfTaps][kDfBins];
alignas(16) float g_parity_overlap[kHopSamples];
alignas(16) int16_t g_parity_output[kHopSamples];
#endif
bool g_ready;
int g_init_status;
uint32_t g_hops_seen;
#if M55M1_DEEPFILTERNET_DIAGNOSTICS
deepfilternet_audio_diagnostics_t g_diagnostics{};
#endif

#if M55M1_DEEPFILTERNET_DIAGNOSTICS
extern "C" uint32_t audio_transport_cycles_now(void) __attribute__((weak));
extern "C" uint32_t audio_transport_cycles_elapsed(
    uint32_t, uint32_t) __attribute__((weak));
extern "C" uint32_t audio_transport_cycles_to_us(uint32_t) __attribute__((weak));
extern "C" void deepfilternet_inference_get_diagnostics(
    deepfilternet_inference_diagnostics_t *) __attribute__((weak));
extern "C" __attribute__((weak)) void deepfilternet_inference_get_diagnostics(
    deepfilternet_inference_diagnostics_t *) {}
#endif

#if M55M1_DEEPFILTERNET_DIAGNOSTICS
uint32_t diagnostic_now() {
    return (audio_transport_cycles_now == nullptr) ? 0U :
        audio_transport_cycles_now();
}

uint32_t diagnostic_elapsed_us(uint32_t start) {
    if ((audio_transport_cycles_now == nullptr) ||
        (audio_transport_cycles_elapsed == nullptr) ||
        (audio_transport_cycles_to_us == nullptr)) {
        return 0U;
    }
    return audio_transport_cycles_to_us(audio_transport_cycles_elapsed(
        start, audio_transport_cycles_now()));
}
#endif

int8_t quantize(float value, float scale, int zero_point) {
    const long rounded = std::lrintf(value / scale);
    long result = rounded + zero_point;
    if (result < -128L) result = -128L;
    if (result > 127L) result = 127L;
    return static_cast<int8_t>(result);
}

float dequantize(int8_t value, float scale, int zero_point) {
    return static_cast<float>(static_cast<int32_t>(value) - zero_point) * scale;
}

float pcm_to_float(int16_t sample) {
    return static_cast<float>(sample) / kPcmScale;
}

int16_t apply_input_gain(int16_t sample) {
    const int32_t amplified = static_cast<int32_t>(sample) * kInputGainMultiplier;

    if (amplified < INT16_MIN) {
        return INT16_MIN;
    }
    if (amplified > INT16_MAX) {
        return INT16_MAX;
    }
    return static_cast<int16_t>(amplified);
}

int16_t float_to_pcm(float value) {
    if (value <= -1.0f) {
        return INT16_MIN;
    }
    if (value >= 0.999969482421875f) {
        return INT16_MAX;
    }
    return static_cast<int16_t>(std::lrintf(value * kPcmScale));
}

void clear_df_history() {
    std::memset(g_df_history, 0, sizeof(g_df_history));
}

void append_hop(const int16_t input[kHopSamples]) {
    if (g_hops_seen >= 2U) {
        std::memmove(g_analysis_ring, g_analysis_ring + kHopSamples,
                     kHopSamples * sizeof(g_analysis_ring[0]));
    }
    const uint32_t offset = (g_hops_seen == 0U) ? 0U : kHopSamples;
    for (uint32_t i = 0U; i < kHopSamples; ++i) {
        g_analysis_ring[offset + i] = pcm_to_float(apply_input_gain(input[i]));
    }
}

#if M55M1_DEEPFILTERNET_DSP_SCALAR || M55M1_DEEPFILTERNET_DSP_PARITY
void analyze_scalar() {
#if M55M1_DEEPFILTERNET_DIAGNOSTICS
    const uint32_t fft_start = diagnostic_now();
#endif
    for (uint32_t i = 0U; i < kFrameSamples; ++i) {
        g_fft_input[i] = g_analysis_ring[i] * dfn_sqrt_hann_512[i];
    }
    kiss_fftr(g_forward_fft, g_fft_input,
              reinterpret_cast<kiss_fft_cpx *>(g_spectrum));
#if M55M1_DEEPFILTERNET_DIAGNOSTICS
    g_diagnostics.fft_us = diagnostic_elapsed_us(fft_start);
    const uint32_t feature_start = diagnostic_now();
#endif
    for (uint32_t bin = 0U; bin < kBins; ++bin) {
        const float real = g_spectrum[bin].r;
        const float imag = g_spectrum[bin].i;
        g_magnitude[bin] = std::sqrt(real * real + imag * imag);
    }
    for (uint32_t band = 0U; band < kBands; ++band) {
        float energy = 0.0f;
        for (uint32_t bin = 0U; bin < kBins; ++bin) {
            energy += g_magnitude[bin] * dfn_erb_analysis_257x32[bin * kBands + band];
        }
        g_erb_q[band] = quantize(std::log((energy > kLogEpsilon) ? energy : kLogEpsilon),
                                  DFN_INPUTS_ERB_FEATURE_SCALE,
                                  DFN_INPUTS_ERB_FEATURE_ZERO_POINT);
    }
    for (uint32_t bin = 0U; bin < kDfBins; ++bin) {
        g_complex_q[bin * 2U] = quantize(g_spectrum[bin].r,
                                         DFN_INPUTS_COMPLEX_FEATURE_SCALE,
                                         DFN_INPUTS_COMPLEX_FEATURE_ZERO_POINT);
        g_complex_q[bin * 2U + 1U] = quantize(g_spectrum[bin].i,
                                              DFN_INPUTS_COMPLEX_FEATURE_SCALE,
                                              DFN_INPUTS_COMPLEX_FEATURE_ZERO_POINT);
    }
#if M55M1_DEEPFILTERNET_DIAGNOSTICS
    g_diagnostics.feature_us = diagnostic_elapsed_us(feature_start);
#endif
}

void analyze() {
    analyze_scalar();
}
#endif

#if M55M1_DEEPFILTERNET_DSP_MVE || M55M1_DEEPFILTERNET_DSP_PARITY
void analyze_mve() {
#if M55M1_DEEPFILTERNET_DIAGNOSTICS
    const uint32_t fft_start = diagnostic_now();
#endif
    arm_mult_f32(g_analysis_ring, dfn_sqrt_hann_512, g_mve_time,
                 kFrameSamples);
    arm_rfft_fast_f32(&g_mve_fft, g_mve_time, g_mve_packed, 0U);
    g_spectrum[0] = {g_mve_packed[0], 0.0f};
    g_spectrum[kBins - 1U] = {g_mve_packed[1], 0.0f};
    for (uint32_t bin = 1U; bin < (kBins - 1U); ++bin) {
        g_spectrum[bin] = {g_mve_packed[bin * 2U],
                           g_mve_packed[bin * 2U + 1U]};
    }
#if M55M1_DEEPFILTERNET_DIAGNOSTICS
    g_diagnostics.fft_us = diagnostic_elapsed_us(fft_start);
    const uint32_t feature_start = diagnostic_now();
#endif
    for (uint32_t bin = 0U; bin < kBins; ++bin) {
        const float real = g_spectrum[bin].r;
        const float imag = g_spectrum[bin].i;
        g_magnitude[bin] = std::sqrt(real * real + imag * imag);
    }
    for (uint32_t band = 0U; band < kBands; ++band) {
        float energy;
        arm_dot_prod_f32(g_magnitude,
                         &g_mve_erb_weights[band * kBins], kBins, &energy);
        g_erb_q[band] = quantize(
            std::log((energy > kLogEpsilon) ? energy : kLogEpsilon),
            DFN_INPUTS_ERB_FEATURE_SCALE,
            DFN_INPUTS_ERB_FEATURE_ZERO_POINT);
    }
    for (uint32_t bin = 0U; bin < kDfBins; ++bin) {
        g_complex_q[bin * 2U] = quantize(
            g_spectrum[bin].r, DFN_INPUTS_COMPLEX_FEATURE_SCALE,
            DFN_INPUTS_COMPLEX_FEATURE_ZERO_POINT);
        g_complex_q[bin * 2U + 1U] = quantize(
            g_spectrum[bin].i, DFN_INPUTS_COMPLEX_FEATURE_SCALE,
            DFN_INPUTS_COMPLEX_FEATURE_ZERO_POINT);
    }
#if M55M1_DEEPFILTERNET_DIAGNOSTICS
    g_diagnostics.feature_us = diagnostic_elapsed_us(feature_start);
#endif
}
#endif

#if M55M1_DEEPFILTERNET_DSP_SCALAR || M55M1_DEEPFILTERNET_DSP_PARITY
void postprocess_scalar(const complex_f32_t spectrum[kBins],
                        complex_f32_t filtered[kBins],
                        complex_f32_t history[kDfTaps][kDfBins],
                        bool inference_ok) {
#if M55M1_DEEPFILTERNET_DIAGNOSTICS
    const uint32_t df_start = diagnostic_now();
#endif
    if (!inference_ok) {
        for (uint32_t bin = 0U; bin < kBins; ++bin) {
            filtered[bin] = spectrum[bin];
        }
#if M55M1_DEEPFILTERNET_DIAGNOSTICS
        g_diagnostics.df_us = diagnostic_elapsed_us(df_start);
#endif
        return;
    }
    for (uint32_t bin = 0U; bin < kBins; ++bin) {
        const float gain = dequantize(g_gain_q[bin], DFN_OUTPUTS_GAIN_BINS_SCALE,
                                      DFN_OUTPUTS_GAIN_BINS_ZERO_POINT);
        filtered[bin].r = spectrum[bin].r * gain;
        filtered[bin].i = spectrum[bin].i * gain;
    }
    for (uint32_t bin = 0U; bin < kDfBins; ++bin) {
        float df_real = 0.0f;
        float df_imag = 0.0f;
        for (uint32_t tap = 0U; tap < kDfTaps; ++tap) {
            const uint32_t offset = (tap * kDfBins + bin) * 2U;
            const float coeff_real = dequantize(g_df_q[offset],
                                                DFN_OUTPUTS_DF_COEFFICIENTS_SCALE,
                                                DFN_OUTPUTS_DF_COEFFICIENTS_ZERO_POINT);
            const float coeff_imag = dequantize(g_df_q[offset + 1U],
                                                DFN_OUTPUTS_DF_COEFFICIENTS_SCALE,
                                                DFN_OUTPUTS_DF_COEFFICIENTS_ZERO_POINT);
            const float xr = (tap == 0U) ? filtered[bin].r : history[tap - 1U][bin].r;
            const float xi = (tap == 0U) ? filtered[bin].i : history[tap - 1U][bin].i;
            df_real += coeff_real * xr - coeff_imag * xi;
            df_imag += coeff_real * xi + coeff_imag * xr;
        }
        const float alpha = dequantize(g_alpha_q, DFN_OUTPUTS_ALPHA_SCALE,
                                       DFN_OUTPUTS_ALPHA_ZERO_POINT);
        filtered[bin].r = (1.0f - alpha) * filtered[bin].r + alpha * df_real;
        filtered[bin].i = (1.0f - alpha) * filtered[bin].i + alpha * df_imag;
    }
#if M55M1_DEEPFILTERNET_DIAGNOSTICS
    g_diagnostics.df_us = diagnostic_elapsed_us(df_start);
#endif
}

void postprocess(bool inference_ok) {
    postprocess_scalar(g_spectrum, g_filtered, g_df_history, inference_ok);
}
#endif

#if M55M1_DEEPFILTERNET_DSP_MVE || M55M1_DEEPFILTERNET_DSP_PARITY
void postprocess_mve(bool inference_ok) {
#if M55M1_DEEPFILTERNET_DIAGNOSTICS
    const uint32_t df_start = diagnostic_now();
#endif
    if (!inference_ok) {
        std::memcpy(g_filtered, g_spectrum, sizeof(g_filtered));
#if M55M1_DEEPFILTERNET_DIAGNOSTICS
        g_diagnostics.df_us = diagnostic_elapsed_us(df_start);
#endif
        return;
    }
    for (uint32_t bin = 0U; bin < kBins; ++bin) {
        const float gain = dequantize(
            g_gain_q[bin], DFN_OUTPUTS_GAIN_BINS_SCALE,
            DFN_OUTPUTS_GAIN_BINS_ZERO_POINT);
        g_mve_gain[bin * 2U] = gain;
        g_mve_gain[bin * 2U + 1U] = gain;
    }
    arm_mult_f32(reinterpret_cast<const float *>(g_spectrum), g_mve_gain,
                 reinterpret_cast<float *>(g_filtered), kBins * 2U);
    for (uint32_t index = 0U; index < kDfTaps * kDfBins * 2U; ++index) {
        g_mve_coefficients[index] = dequantize(
            g_df_q[index], DFN_OUTPUTS_DF_COEFFICIENTS_SCALE,
            DFN_OUTPUTS_DF_COEFFICIENTS_ZERO_POINT);
    }
    for (uint32_t tap = 0U; tap < kDfTaps; ++tap) {
        const float *source = (tap == 0U) ?
            reinterpret_cast<const float *>(g_filtered) :
            reinterpret_cast<const float *>(g_df_history[tap - 1U]);
        arm_cmplx_mult_cmplx_f32(
            source, &g_mve_coefficients[tap * kDfBins * 2U],
            g_mve_temporary, kDfBins);
        if (tap == 0U) {
            std::memcpy(g_mve_accumulator, g_mve_temporary,
                        sizeof(g_mve_accumulator));
        } else {
            arm_add_f32(g_mve_accumulator, g_mve_temporary,
                        g_mve_accumulator, kDfBins * 2U);
        }
    }
    const float alpha = dequantize(
        g_alpha_q, DFN_OUTPUTS_ALPHA_SCALE, DFN_OUTPUTS_ALPHA_ZERO_POINT);
    arm_scale_f32(g_mve_accumulator, alpha, g_mve_accumulator,
                  kDfBins * 2U);
    arm_scale_f32(reinterpret_cast<const float *>(g_filtered), 1.0f - alpha,
                  g_mve_temporary, kDfBins * 2U);
    arm_add_f32(g_mve_temporary, g_mve_accumulator,
                reinterpret_cast<float *>(g_filtered), kDfBins * 2U);
#if M55M1_DEEPFILTERNET_DIAGNOSTICS
    g_diagnostics.df_us = diagnostic_elapsed_us(df_start);
#endif
}
#endif

#if M55M1_DEEPFILTERNET_DSP_SCALAR || M55M1_DEEPFILTERNET_DSP_PARITY
void synthesize_scalar(const complex_f32_t filtered[kBins],
                       float overlap[kHopSamples],
                       int16_t output[kHopSamples]) {
    kiss_fftri(g_inverse_fft,
               reinterpret_cast<const kiss_fft_cpx *>(filtered), g_ifft_output);
    for (uint32_t i = 0U; i < kHopSamples; ++i) {
        const float first = (g_ifft_output[i] / static_cast<float>(kFrameSamples)) *
                            dfn_sqrt_hann_512[i];
        const uint32_t second_index = i + kHopSamples;
        const float second = (g_ifft_output[second_index] /
                              static_cast<float>(kFrameSamples)) *
                             dfn_sqrt_hann_512[second_index];
        output[i] = float_to_pcm(first + overlap[i]);
        overlap[i] = second;
    }
}

void synthesize(int16_t output[kHopSamples]) {
    synthesize_scalar(g_filtered, g_overlap, output);
}
#endif

#if M55M1_DEEPFILTERNET_DSP_MVE || M55M1_DEEPFILTERNET_DSP_PARITY
void synthesize_mve(int16_t output[kHopSamples]) {
    g_mve_packed[0] = g_filtered[0].r;
    g_mve_packed[1] = g_filtered[kBins - 1U].r;
    for (uint32_t bin = 1U; bin < (kBins - 1U); ++bin) {
        g_mve_packed[bin * 2U] = g_filtered[bin].r;
        g_mve_packed[bin * 2U + 1U] = g_filtered[bin].i;
    }
    arm_rfft_fast_f32(&g_mve_fft, g_mve_packed, g_mve_time, 1U);
    arm_mult_f32(g_mve_time, dfn_sqrt_hann_512, g_mve_windowed,
                 kFrameSamples);
    arm_add_f32(g_mve_windowed, g_overlap, g_mve_time, kHopSamples);
    for (uint32_t index = 0U; index < kHopSamples; ++index) {
        output[index] = float_to_pcm(g_mve_time[index]);
    }
    std::memcpy(g_overlap, &g_mve_windowed[kHopSamples], sizeof(g_overlap));
}
#endif

void update_df_history(complex_f32_t history[kDfTaps][kDfBins]) {
    for (uint32_t tap = kDfTaps - 1U; tap > 0U; --tap) {
        std::memcpy(history[tap], history[tap - 1U], sizeof(history[tap]));
    }
    for (uint32_t bin = 0U; bin < kDfBins; ++bin) {
        const float gain = dequantize(
            g_gain_q[bin], DFN_OUTPUTS_GAIN_BINS_SCALE,
            DFN_OUTPUTS_GAIN_BINS_ZERO_POINT);
        history[0][bin].r = g_spectrum[bin].r * gain;
        history[0][bin].i = g_spectrum[bin].i * gain;
    }
}

} // namespace

extern "C" bool deepfilternet_audio_enhancer_init(void) {
    if (g_ready) {
        g_init_status = 1;
        return true;
    }
#if M55M1_DEEPFILTERNET_DSP_SCALAR || M55M1_DEEPFILTERNET_DSP_PARITY
    size_t forward_bytes = sizeof(g_forward_memory);
    size_t inverse_bytes = sizeof(g_inverse_memory);
    g_forward_fft = kiss_fftr_alloc(kFrameSamples, 0, g_forward_memory, &forward_bytes);
    g_inverse_fft = kiss_fftr_alloc(kFrameSamples, 1, g_inverse_memory, &inverse_bytes);
    if (g_forward_fft == nullptr || g_inverse_fft == nullptr) {
        g_init_status = -200;
        return false;
    }
#endif
#if M55M1_DEEPFILTERNET_DSP_MVE || M55M1_DEEPFILTERNET_DSP_PARITY
    if (arm_rfft_fast_init_512_f32(&g_mve_fft) != ARM_MATH_SUCCESS) {
        g_init_status = -201;
        return false;
    }
    for (uint32_t band = 0U; band < kBands; ++band) {
        for (uint32_t bin = 0U; bin < kBins; ++bin) {
            g_mve_erb_weights[band * kBins + bin] =
                dfn_erb_analysis_257x32[bin * kBands + band];
        }
    }
#endif
    if (!deepfilternet_inference_init()) {
        g_init_status = deepfilternet_inference_last_init_status();
        return false;
    }
    g_ready = true;
    g_init_status = 1;
    deepfilternet_audio_enhancer_reset();
    return true;
}

extern "C" int deepfilternet_audio_enhancer_last_init_status(void) {
    return g_init_status;
}

extern "C" deepfilternet_audio_result_t deepfilternet_audio_enhancer_process(
    const int16_t input[kHopSamples], int16_t output[kHopSamples]) {
    if (input == nullptr || output == nullptr || !g_ready) {
        if (output != nullptr) {
            std::memset(output, 0, sizeof(int16_t) * kHopSamples);
        }
        return DEEPFILTERNET_AUDIO_RESULT_INVALID;
    }
#if M55M1_DEEPFILTERNET_DIAGNOSTICS
    const uint32_t e2e_start = diagnostic_now();
    g_diagnostics.input_peak = 0U;
    g_diagnostics.model_input_peak = 0U;
    g_diagnostics.input_energy = 0U;
    g_diagnostics.model_input_energy = 0U;
    g_diagnostics.input_clip_count = 0U;
    g_diagnostics.model_input_clip_count = 0U;
    g_diagnostics.pcm_samples = kHopSamples;
    for (uint32_t index = 0U; index < kHopSamples; ++index) {
        const int32_t sample = input[index];
        const int32_t model_sample = apply_input_gain(input[index]);
        const uint32_t magnitude = (sample < 0) ? (uint32_t)(-sample) :
            (uint32_t)sample;
        const uint32_t model_magnitude = (model_sample < 0) ?
            (uint32_t)(-model_sample) : (uint32_t)model_sample;
        if (magnitude > g_diagnostics.input_peak) {
            g_diagnostics.input_peak = magnitude;
        }
        g_diagnostics.input_energy += (uint64_t)(sample * sample);
        if (model_magnitude > g_diagnostics.model_input_peak) {
            g_diagnostics.model_input_peak = model_magnitude;
        }
        g_diagnostics.model_input_energy +=
            (uint64_t)(model_sample * model_sample);
        if ((sample == INT16_MIN) || (sample == INT16_MAX)) {
            ++g_diagnostics.input_clip_count;
        }
        if ((model_sample == INT16_MIN) || (model_sample == INT16_MAX)) {
            ++g_diagnostics.model_input_clip_count;
        }
    }
#endif
    append_hop(input);
    if (g_hops_seen == 0U) {
        std::memset(output, 0, sizeof(int16_t) * kHopSamples);
        g_hops_seen = 1U;
#if M55M1_DEEPFILTERNET_DIAGNOSTICS
        g_diagnostics.e2e_us = diagnostic_elapsed_us(e2e_start);
        if (g_diagnostics.e2e_us > 16000U) {
            ++g_diagnostics.deadline_miss_count;
        }
#endif
        return DEEPFILTERNET_AUDIO_RESULT_PREROLL_SILENCE;
    }
#if M55M1_DEEPFILTERNET_DSP_MVE || M55M1_DEEPFILTERNET_DSP_PARITY
    analyze_mve();
#else
    analyze_scalar();
#endif
#if M55M1_DEEPFILTERNET_DIAGNOSTICS
    const uint32_t npu_start = diagnostic_now();
#endif
    const bool inference_ok = deepfilternet_inference_invoke(
        g_erb_q, g_complex_q, g_gain_q, g_df_q, &g_alpha_q);
#if M55M1_DEEPFILTERNET_DIAGNOSTICS
    g_diagnostics.npu_us = diagnostic_elapsed_us(npu_start);
#endif
#if M55M1_DEEPFILTERNET_DSP_PARITY
    postprocess_scalar(g_spectrum, g_parity_filtered, g_parity_history,
                       inference_ok);
    postprocess_mve(inference_ok);
#elif M55M1_DEEPFILTERNET_DSP_MVE
    postprocess_mve(inference_ok);
#else
    postprocess(inference_ok);
#endif
#if M55M1_DEEPFILTERNET_DIAGNOSTICS
    const uint32_t wola_start = diagnostic_now();
#endif
#if M55M1_DEEPFILTERNET_DSP_PARITY
    synthesize_scalar(g_parity_filtered, g_parity_overlap, g_parity_output);
    synthesize_mve(output);
#elif M55M1_DEEPFILTERNET_DSP_MVE
    synthesize_mve(output);
#else
    synthesize(output);
#endif
#if M55M1_DEEPFILTERNET_DIAGNOSTICS
    g_diagnostics.wola_us = diagnostic_elapsed_us(wola_start);
#if M55M1_DEEPFILTERNET_DSP_PARITY
    uint32_t parity_frame_max = 0U;
    for (uint32_t index = 0U; index < kHopSamples; ++index) {
        const int32_t difference = static_cast<int32_t>(output[index]) -
            static_cast<int32_t>(g_parity_output[index]);
        const uint32_t magnitude = static_cast<uint32_t>(
            (difference < 0) ? -difference : difference);
        if (magnitude > parity_frame_max) {
            parity_frame_max = magnitude;
        }
    }
    if (parity_frame_max > g_diagnostics.parity_max_pcm_difference) {
        g_diagnostics.parity_max_pcm_difference = parity_frame_max;
    }
    if (parity_frame_max > 1U) {
        ++g_diagnostics.parity_violation_count;
    }
#endif
    g_diagnostics.output_peak = 0U;
    g_diagnostics.output_energy = 0U;
    g_diagnostics.output_clip_count = 0U;
    for (uint32_t index = 0U; index < kHopSamples; ++index) {
        const int32_t sample = output[index];
        const uint32_t magnitude = (sample < 0) ? (uint32_t)(-sample) :
            (uint32_t)sample;
        if (magnitude > g_diagnostics.output_peak) {
            g_diagnostics.output_peak = magnitude;
        }
        g_diagnostics.output_energy += (uint64_t)(sample * sample);
        if ((sample == INT16_MIN) || (sample == INT16_MAX)) {
            ++g_diagnostics.output_clip_count;
        }
    }
    g_diagnostics.e2e_us = diagnostic_elapsed_us(e2e_start);
    if (g_diagnostics.e2e_us > 16000U) {
        ++g_diagnostics.deadline_miss_count;
    }
    {
        deepfilternet_inference_diagnostics_t inference_diagnostics{};
        using diagnostics_getter_t = void (*)(
            deepfilternet_inference_diagnostics_t *);
        const diagnostics_getter_t diagnostics_getter =
            &deepfilternet_inference_get_diagnostics;
        if (diagnostics_getter != nullptr) {
            diagnostics_getter(&inference_diagnostics);
        }
        g_diagnostics.invoke_count = inference_diagnostics.invoke_count;
        g_diagnostics.npu_error_count = inference_diagnostics.invoke_error_count;
        g_diagnostics.feature_min = inference_diagnostics.feature_min;
        g_diagnostics.feature_max = inference_diagnostics.feature_max;
        g_diagnostics.feature_sat_count = inference_diagnostics.feature_sat_count;
        g_diagnostics.complex_min = inference_diagnostics.complex_min;
        g_diagnostics.complex_max = inference_diagnostics.complex_max;
        g_diagnostics.complex_sat_count = inference_diagnostics.complex_sat_count;
        g_diagnostics.gain_min = inference_diagnostics.gain_min;
        g_diagnostics.gain_max = inference_diagnostics.gain_max;
        g_diagnostics.gain_sat_count = inference_diagnostics.gain_sat_count;
        g_diagnostics.df_min = inference_diagnostics.df_min;
        g_diagnostics.df_max = inference_diagnostics.df_max;
        g_diagnostics.df_sat_count = inference_diagnostics.df_sat_count;
        g_diagnostics.alpha_min = inference_diagnostics.alpha_min;
        g_diagnostics.alpha_max = inference_diagnostics.alpha_max;
        g_diagnostics.alpha_sat_count = inference_diagnostics.alpha_sat_count;
        std::memcpy(g_diagnostics.state_min, inference_diagnostics.state_min,
                    sizeof(g_diagnostics.state_min));
        std::memcpy(g_diagnostics.state_max, inference_diagnostics.state_max,
                    sizeof(g_diagnostics.state_max));
        std::memcpy(g_diagnostics.state_sat_count,
                    inference_diagnostics.state_sat_count,
                    sizeof(g_diagnostics.state_sat_count));
        g_diagnostics.arena_used_bytes = inference_diagnostics.arena_used_bytes;
        g_diagnostics.arena_capacity_bytes =
            inference_diagnostics.arena_capacity_bytes;
    }
#endif
    ++g_hops_seen;
    if (!inference_ok) {
#if M55M1_DEEPFILTERNET_DIAGNOSTICS
        ++g_diagnostics.fallback_count;
#endif
        deepfilternet_inference_reset();
        clear_df_history();
#if M55M1_DEEPFILTERNET_DSP_PARITY
        std::memset(g_parity_history, 0, sizeof(g_parity_history));
#endif
        return DEEPFILTERNET_AUDIO_RESULT_UNITY_FALLBACK;
    }
    update_df_history(g_df_history);
#if M55M1_DEEPFILTERNET_DSP_PARITY
    update_df_history(g_parity_history);
#endif
    return DEEPFILTERNET_AUDIO_RESULT_ENHANCED;
}

extern "C" void deepfilternet_audio_enhancer_reset(void) {
    std::memset(g_analysis_ring, 0, sizeof(g_analysis_ring));
    std::memset(g_overlap, 0, sizeof(g_overlap));
    clear_df_history();
#if M55M1_DEEPFILTERNET_DSP_PARITY
    std::memset(g_parity_overlap, 0, sizeof(g_parity_overlap));
    std::memset(g_parity_history, 0, sizeof(g_parity_history));
#endif
    if (g_ready) {
        deepfilternet_inference_reset();
    }
    g_hops_seen = 0U;
#if M55M1_DEEPFILTERNET_DIAGNOSTICS
    ++g_diagnostics.reset_count;
#endif
}

extern "C" void deepfilternet_audio_enhancer_discontinuity(void) {
    deepfilternet_audio_enhancer_reset();
}

extern "C" void deepfilternet_audio_enhancer_get_diagnostics(
    deepfilternet_audio_diagnostics_t *diagnostics) {
    if (diagnostics != nullptr) {
#if M55M1_DEEPFILTERNET_DIAGNOSTICS
        *diagnostics = g_diagnostics;
        uint32_t static_bytes =
            sizeof(g_analysis_ring) + sizeof(g_overlap) +
            sizeof(g_spectrum) + sizeof(g_filtered) +
            sizeof(g_df_history) + sizeof(g_magnitude) +
            sizeof(g_erb_q) + sizeof(g_complex_q) + sizeof(g_gain_q) +
            sizeof(g_df_q);
#if M55M1_DEEPFILTERNET_DSP_SCALAR || M55M1_DEEPFILTERNET_DSP_PARITY
        static_bytes += sizeof(g_fft_input) + sizeof(g_ifft_output) +
            sizeof(g_forward_memory) + sizeof(g_inverse_memory);
#endif
#if M55M1_DEEPFILTERNET_DSP_MVE || M55M1_DEEPFILTERNET_DSP_PARITY
        static_bytes += sizeof(g_mve_time) + sizeof(g_mve_packed) +
            sizeof(g_mve_windowed) + sizeof(g_mve_erb_weights) +
            sizeof(g_mve_gain) + sizeof(g_mve_coefficients) +
            sizeof(g_mve_accumulator) + sizeof(g_mve_temporary);
#endif
#if M55M1_DEEPFILTERNET_DSP_PARITY
        static_bytes += sizeof(g_parity_filtered) + sizeof(g_parity_history) +
            sizeof(g_parity_overlap) + sizeof(g_parity_output);
#endif
        diagnostics->dsp_static_bytes = static_bytes;
#else
        std::memset(diagnostics, 0, sizeof(*diagnostics));
#endif
    }
}

#if MODEL_GOLDEN_TEST_DATA
extern "C" bool deepfilternet_audio_enhancer_flush(int16_t output[kHopSamples]) {
    if (output == nullptr || !g_ready || g_hops_seen == 0U) {
        return false;
    }
    for (uint32_t i = 0U; i < kHopSamples; ++i) {
        output[i] = float_to_pcm(g_overlap[i]);
        g_overlap[i] = 0.0f;
    }
    return true;
}
#endif
