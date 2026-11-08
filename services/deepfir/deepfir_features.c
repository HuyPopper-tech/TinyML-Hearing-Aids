/**
 * @file deepfir_features.c
 * @brief R2 feature extraction and startup validation.
 * @author Bui Minh Huy
 * @date 2026-10-23
 */
#include <math.h>
#include <string.h>

#include "deepfir_dsp.h"

#ifndef M55M1_DEEPFIR_MVE
#define M55M1_DEEPFIR_MVE 0
#endif

#if M55M1_DEEPFIR_MVE && defined(__ARM_FEATURE_MVE) && (__ARM_FEATURE_MVE & 2)
#include "arm_math.h"
#include <arm_mve.h>
#define DEEPFIR_DSP_HAS_MVEF 1
#else
#define DEEPFIR_DSP_HAS_MVEF 0
#endif

void deepfir_feature_reset(deepfir_feature_state_t *state) {
    uint32_t index;
    if (!state) {
        return;
    }
    memset(state->history, 0, sizeof(state->history));
    for (index = 0; index < 256u; ++index) {
        // Symmetric Hamming uses N-1; a periodic window changes the exported frontend.
        state->window[index] =
            0.54f - 0.46f * cosf((float)(6.283185307179586 * (double)index / 255.0));
    }
}

static int quantize_feature(float value, const deepfir_feature_state_t *state, double inverse_scale,
                            int16_t *output) {
    double code = (double)value * inverse_scale + state->zero_point;
    // Extreme codes use division to preserve its overflow/non-finite behavior too.
    if (!isfinite(code) || fabs(code) >= 65536.0) {
        code = (double)value / state->scale + state->zero_point;
    }
    if (!isfinite(code)) {
        return -2;
    }
    if (code >= 32767.0) {
        *output = 32767;
        return 0;
    }
    if (code <= -32768.0) {
        *output = -32768;
        return 0;
    }
    // Below 65536 quantizer units, double reciprocal/multiply error is <1e-10.
    // Re-evaluate a wider 1e-8 band at half codes to preserve division's exact rounding.
    const int32_t whole = (int32_t)code;
    if (fabs(fabs(code - whole) - 0.5) <= 1.0e-8) {
        code = (double)value / state->scale + state->zero_point;
    }
    *output = (int16_t)(code >= 0.0 ? code + 0.5 : code - 0.5);
    return 0;
}

int deepfir_feature_float(const float *pcm, uint32_t count, int16_t *feature, void *context) {
    deepfir_feature_state_t *state = (deepfir_feature_state_t *)context;
    float spectrum[512];
    uint32_t index;
    if (!state || !pcm || !feature || count != 16u || !state->fft || !(state->scale > 0.0f) ||
        !isfinite(state->scale) || state->zero_point < -32768 || state->zero_point > 32767) {
        return -1;
    }
    for (index = 0; index < count; ++index) {
        if (!isfinite(pcm[index])) {
            return -2;
        }
    }
    // Shift only past samples: startup zeros and the current 16 samples form a causal frame.
    memmove(state->history, state->history + 16, 240u * sizeof(float));
    memcpy(state->history + 240u, pcm, 16u * sizeof(float));
#if DEEPFIR_DSP_HAS_MVEF
    for (index = 0u; index < 256u; index += 4u) {
        float32x4x2_t complex_values;
        complex_values.val[0] =
            vmulq_f32(vld1q_f32(state->history + index), vld1q_f32(state->window + index));
        complex_values.val[1] = vdupq_n_f32(0.0f);
        vst2q_f32(spectrum + index * 2u, complex_values);
    }
#else
    for (index = 0; index < 256u; ++index) {
        spectrum[index * 2u] = state->history[index] * state->window[index];
        spectrum[index * 2u + 1u] = 0.0f;
    }
#endif
    if (state->fft(spectrum, 256u, 0u, state->fft_context) != 0) {
        return -2;
    }
    float magnitude[129], gain[129];
    const double inverse_scale = 1.0 / state->scale;
    for (index = 0; index < 129u; ++index) {
        const float re = spectrum[2u * index];
        const float im = spectrum[2u * index + 1u];
        const float power = re * re + im * im;
        magnitude[index] = sqrtf(power);
        gain[index] = sqrtf(power + 1.0e-16f);
    }
#if DEEPFIR_DSP_HAS_MVEF
    // Positive epsilon protects signed channels; silence magnitude is restored below.
    for (index = 0; index < 129u; ++index) {
        if (magnitude[index] == 0.0f) {
            magnitude[index] = 1.0e-30f;
        }
    }
    arm_vlog_f32(magnitude, magnitude, 129u);
    arm_scale_f32(magnitude, 0.3f, magnitude, 129u);
    arm_vexp_f32(magnitude, magnitude, 129u);
    arm_vlog_f32(gain, gain, 129u);
    arm_scale_f32(gain, -0.7f, gain, 129u);
    arm_vexp_f32(gain, gain, 129u);
#else
    for (index = 0; index < 129u; ++index) {
        magnitude[index] = powf(magnitude[index], 0.3f);
        gain[index] = powf(gain[index], -0.7f);
    }
#endif
    for (index = 0; index < 129u; ++index) {
        float re = spectrum[2u * index], im = spectrum[2u * index + 1u];
        const float values[3] = {re == 0.0f && im == 0.0f ? 0.0f : magnitude[index],
                                 re * gain[index], im * gain[index]};
        for (unsigned channel = 0; channel < 3u; ++channel) {
            if (quantize_feature(values[channel], state, inverse_scale,
                                 &feature[3u * index + channel]) != 0) {
                return -2;
            }
        }
    }
    return 0;
}

int deepfir_feature_pcm16(const int16_t *pcm, uint32_t count, int16_t *feature, void *context) {
    float normalized[DEEPFIR_DSP_HOP];
    if (!pcm || count != DEEPFIR_DSP_HOP) {
        return -1;
    }
#if DEEPFIR_DSP_HAS_MVEF
    arm_q15_to_float((const q15_t *)pcm, normalized, count);
#else
    for (uint32_t i = 0; i < count; ++i) {
        normalized[i] = (float)pcm[i] / 32768.0f;
    }
#endif
    return deepfir_feature_float(normalized, count, feature, context);
}

bool deepfir_feature_mve_ready(void) {
    return DEEPFIR_DSP_HAS_MVEF != 0;
}

#if DEEPFIR_DSP_HAS_MVEF
static int feature_check_fft(float *buffer, uint32_t length, uint8_t inverse, void *context) {
    const unsigned mode = *(const unsigned *)context;
    (void)length;
    (void)inverse;
    for (unsigned i = 0; i < 129; ++i) {
        const float values[] = {0.0f, 1.0e-20f, 1.0e-8f, 0.125f, 1.0f, 100.0f};
        const float value = values[(i + mode) % 6];
        buffer[2 * i] = (i & 1) ? -value : value;
        buffer[2 * i + 1] = (i & 2) ? -value * 0.5f : value * 0.5f;
    }
    return 0;
}
#endif

bool deepfir_feature_prepare_mve(deepfir_feature_state_t *state) {
#if DEEPFIR_DSP_HAS_MVEF
    if (!state) {
        return false;
    }
    deepfir_fft_callback saved_fft = state->fft;
    void *saved_context = state->fft_context;
    int16_t feature[387], pcm[16] = {0};
    unsigned mode;
    bool valid = true;
    state->fft = feature_check_fft;
    state->fft_context = &mode;
    deepfir_feature_reset(state);
    for (mode = 0; mode < 6 && valid; ++mode) {
        float spectrum[258];
        feature_check_fft(spectrum, 256, 0, &mode);
        if (deepfir_feature_pcm16(pcm, 16, feature, state) != 0) {
            valid = false;
            break;
        }
        for (unsigned i = 0; i < 129 && valid; ++i) {
            const float re = spectrum[2 * i], im = spectrum[2 * i + 1];
            const float power = re * re + im * im;
            const float gain = powf(sqrtf(power + 1.0e-16f), -0.7f);
            const float values[3] = {powf(sqrtf(power), 0.3f), re * gain, im * gain};
            for (unsigned channel = 0; channel < 3; ++channel) {
                double code = (double)values[channel] / state->scale + state->zero_point;
                if (!isfinite(code)) {
                    valid = false;
                    break;
                }
                code = code >= 0 ? floor(code + 0.5) : -floor(-code + 0.5);
                int expected = code < -32768 ? -32768 : code > 32767 ? 32767 : (int)code;
                int delta = feature[3 * i + channel] - expected;
                if (delta < -1 || delta > 1) {
                    valid = false;
                    break;
                }
            }
        }
    }
    state->fft = saved_fft;
    state->fft_context = saved_context;
    deepfir_feature_reset(state);
    return valid;
#else
    (void)state;
    return false;
#endif
}
