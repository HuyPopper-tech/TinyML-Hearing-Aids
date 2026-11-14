/**
 * @file deepfir_features.c
 * @brief R2 feature extraction and startup validation.
 * @author Bui Minh Huy
 * @date 2026-10-23
 */
#include <float.h>
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
    state->history_position = 0;
    for (index = 0; index < 256u; ++index) {
        // Symmetric Hamming uses N-1; a periodic window changes the exported frontend.
        state->window[index] =
            0.54f - 0.46f * cosf((float)(6.283185307179586 * (double)index / 255.0));
    }
}

static bool quantize_feature_f32(float value, float inverse_scale, int32_t zero_point,
                                 int16_t *output) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    // Leave subnormal inputs to the reference path, independent of target flush-to-zero mode.
    if (((bits & 0x7f800000U) == 0U && (bits & 0x007fffffU) != 0U) || !(inverse_scale >= FLT_MIN) ||
        !isfinite(inverse_scale)) {
        return false;
    }
    const float scaled = value * inverse_scale;
    if (!isfinite(scaled) || fabsf(scaled) > 65536.0f) {
        return false;
    }
    const float code = scaled + zero_point;
    const int32_t whole = (int32_t)code;
    // Normal reciprocal/product/addition error is <0.012 codes in this bounded range.
    // A wider 1/32-code guard delegates ambiguous halfway decisions to double arithmetic.
    if (fabsf(fabsf(code - whole) - 0.5f) <= 0.03125f) {
        return false;
    }
    const int32_t rounded = (int32_t)(code >= 0.0f ? code + 0.5f : code - 0.5f);
    *output = (int16_t)(rounded < -32768 ? -32768 : rounded > 32767 ? 32767 : rounded);
    return true;
}

static int quantize_feature(float value, const deepfir_feature_state_t *state, double inverse_scale,
                            int16_t *output) {
    if (quantize_feature_f32(value, (float)inverse_scale, state->zero_point, output)) {
        return 0;
    }
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

#if DEEPFIR_DSP_HAS_MVEF
__attribute__((noinline))
#endif
static int
deepfir_quantize_feature_block(const float *values, uint32_t count,
                               const deepfir_feature_state_t *state, double inverse_scale,
                               int16_t *output) {
    uint32_t i = 0;
#if DEEPFIR_DSP_HAS_MVEF
    const float inverse = (float)inverse_scale;
    if (inverse >= FLT_MIN && isfinite(inverse)) {
        for (; i + 4u <= count; i += 4u) {
            const float32x4_t value = vld1q_f32(values + i);
            const uint32x4_t bits =
                vandq_u32(vreinterpretq_u32_f32(value), vdupq_n_u32(0x7fffffffU));
            const float32x4_t scaled = vmulq_n_f32(value, inverse);
            mve_pred16_t accepted = (vcmpeqq_n_u32(bits, 0) | vcmpcsq_n_u32(bits, 0x00800000U)) &
                                    vcmpleq_n_f32(vabsq_f32(scaled), 65536.0f);
            // Mask exceptional lanes before integer conversion; they use the scalar reference.
            const float32x4_t code =
                vaddq_n_f32(vpselq_f32(scaled, vdupq_n_f32(0), accepted), (float)state->zero_point);
            const int32x4_t whole = vcvtq_s32_f32(code);
            const float32x4_t fraction = vabsq_f32(vsubq_f32(code, vcvtq_f32_s32(whole)));
            accepted &= vcmpgtq_n_f32(vabsq_f32(vsubq_n_f32(fraction, 0.5f)), 0.03125f);
            const float32x4_t half =
                vpselq_f32(vdupq_n_f32(-0.5f), vdupq_n_f32(0.5f), vcmpltq_n_f32(code, 0));
            int32x4_t rounded = vcvtq_s32_f32(vaddq_f32(code, half));
            rounded = vmaxq_s32(vdupq_n_s32(-32768), vminq_s32(vdupq_n_s32(32767), rounded));
            vstrhq_s32(output + i, rounded);
            if (accepted != UINT16_MAX) {
                for (unsigned lane = 0; lane < 4u; ++lane) {
                    if (!(accepted & (1u << (4u * lane))) &&
                        quantize_feature(values[i + lane], state, inverse_scale,
                                         output + i + lane)) {
                        return -2;
                    }
                }
            }
        }
    }
#endif
    for (; i < count; ++i) {
        if (quantize_feature(values[i], state, inverse_scale, output + i)) {
            return -2;
        }
    }
    return 0;
}

int deepfir_feature_float(const float *pcm, uint32_t count, int16_t *feature, void *context) {
    deepfir_feature_state_t *state = (deepfir_feature_state_t *)context;
    float spectrum[512];
    uint32_t index;
    if (!state || !pcm || !feature || count != 16u || !state->fft || !(state->scale > 0.0f) ||
        !isfinite(state->scale) || state->zero_point < -32768 || state->zero_point > 32767 ||
        state->history_position >= 256u || state->history_position % 16u != 0u) {
        return -1;
    }
    for (index = 0; index < count; ++index) {
        if (!isfinite(pcm[index])) {
            return -2;
        }
    }
    // The ring changes storage only: FFT input remains oldest-to-newest, with startup zeros.
    memcpy(state->history + state->history_position, pcm, 16u * sizeof(float));
    state->history_position = (state->history_position + 16u) & 255u;
    uint32_t source = state->history_position;
    for (uint32_t start = 0; start < 256u;) {
        const uint32_t available = 256u - source;
        const uint32_t remaining = 256u - start;
        const uint32_t segment = available < remaining ? available : remaining;
#if DEEPFIR_DSP_HAS_MVEF
        for (index = 0u; index < segment; index += 4u) {
            float32x4x2_t complex_values;
            complex_values.val[0] = vmulq_f32(vld1q_f32(state->history + source + index),
                                             vld1q_f32(state->window + start + index));
            complex_values.val[1] = vdupq_n_f32(0.0f);
            vst2q_f32(spectrum + (start + index) * 2u, complex_values);
        }
#else
        for (index = 0; index < segment; ++index) {
            spectrum[(start + index) * 2u] =
                state->history[source + index] * state->window[start + index];
            spectrum[(start + index) * 2u + 1u] = 0.0f;
        }
#endif
        start += segment;
        source = 0;
    }
    if (state->fft(spectrum, 256u, 0u, state->fft_context) != 0) {
        return -2;
    }
    float magnitude[129], gain[129];
    const double inverse_scale = 1.0 / state->scale;
    for (index = 0; index < 129u; ++index) {
        const float re = spectrum[2u * index];
        const float im = spectrum[2u * index + 1u];
        const float power = re * re + im * im;
#if DEEPFIR_DSP_HAS_MVEF
        // The vector log needs a positive normal input. This floor is below half a
        // feature code at the model scale, and exact silence is restored at packing.
        magnitude[index] = power < FLT_MIN ? FLT_MIN : power;
        gain[index] = power + 1.0e-16f;
#else
        magnitude[index] = sqrtf(power);
        gain[index] = sqrtf(power + 1.0e-16f);
#endif
    }
#if DEEPFIR_DSP_HAS_MVEF
    // Fold sqrt into the two exponents so the vector path works directly on power.
    arm_vlog_f32(magnitude, magnitude, 129u);
    arm_scale_f32(magnitude, 0.15f, magnitude, 129u);
    arm_vexp_f32(magnitude, magnitude, 129u);
    arm_vlog_f32(gain, gain, 129u);
    arm_scale_f32(gain, -0.35f, gain, 129u);
    arm_vexp_f32(gain, gain, 129u);
#else
    for (index = 0; index < 129u; ++index) {
        magnitude[index] = powf(magnitude[index], 0.3f);
        gain[index] = powf(gain[index], -0.7f);
    }
#endif
    for (index = 0; index < 129u; index += 4u) {
        const unsigned bins = index + 4u <= 129u ? 4u : 1u;
        float values[12];
        for (unsigned lane = 0; lane < bins; ++lane) {
            const unsigned bin = index + lane;
            const float re = spectrum[2u * bin], im = spectrum[2u * bin + 1u];
            values[3u * lane] = re == 0.0f && im == 0.0f ? 0.0f : magnitude[bin];
            values[3u * lane + 1u] = re * gain[bin];
            values[3u * lane + 2u] = im * gain[bin];
        }
        if (deepfir_quantize_feature_block(values, bins * 3u, state, inverse_scale,
                                           feature + 3u * index)) {
            return -2;
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
static bool quantizer_check_mve(const deepfir_feature_state_t *state) {
    const int codes[] = {-65536, -32768, -32767, -128, -1, 0, 1, 127, 32766, 32767, 65535};
    const double offsets[] = {0.0, 0.46875, 0.5, 0.53125};
    for (unsigned c = 0; c < sizeof(codes) / sizeof(codes[0]); ++c) {
        for (int direction = -1; direction <= 1; ++direction) {
            float values[4];
            int16_t actual[4];
            for (unsigned lane = 0; lane < 4u; ++lane) {
                values[lane] =
                    (float)((codes[c] + offsets[lane] - state->zero_point) * state->scale);
                if (direction) {
                    // Microlib lacks nextafterf; step IEEE binary32 bits for startup checks only.
                    uint32_t bits;
                    memcpy(&bits, &values[lane], sizeof(bits));
                    if ((bits & 0x7fffffffU) == 0) {
                        bits = direction < 0 ? 0x80000001U : 1u;
                    } else if ((values[lane] > 0) == (direction > 0)) {
                        ++bits;
                    } else {
                        --bits;
                    }
                    memcpy(&values[lane], &bits, sizeof(bits));
                }
            }
            if (deepfir_quantize_feature_block(values, 4u, state, 1.0 / state->scale, actual)) {
                return false;
            }
            for (unsigned lane = 0; lane < 4u; ++lane) {
                double code = (double)values[lane] / state->scale + state->zero_point;
                code = code >= 0 ? floor(code + 0.5) : -floor(-code + 0.5);
                const int expected = code < -32768 ? -32768 : code > 32767 ? 32767 : (int)code;
                if (actual[lane] != expected) {
                    return false;
                }
            }
        }
    }
    return true;
}

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
    deepfir_fft_callback_t saved_fft = state->fft;
    void *saved_context = state->fft_context;
    int16_t feature[387], pcm[16] = {0};
    unsigned mode;
    bool valid = quantizer_check_mve(state);
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
