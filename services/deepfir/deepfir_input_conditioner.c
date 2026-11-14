/**
 * @file deepfir_input_conditioner.c
 * @brief PCM24 normalization and fixed input dynamics before frontend/FIR.
 * @author Bui Minh Huy
 * @date 2026-11-14
 */
#include <math.h>
#include <stddef.h>
#include <string.h>

#include "common/audio_pcm.h"
#include "deepfir_dsp.h"
#include "deepfir_input_conditioner.h"

#if M55M1_DEEPFIR_MVE && defined(__ARM_FEATURE_MVE) && (__ARM_FEATURE_MVE & 2)
#include <arm_mve.h>
#define DEEPFIR_INPUT_HAS_MVEF 1
#else
#define DEEPFIR_INPUT_HAS_MVEF 0
#endif

// Fixed 16 kHz coefficients: exp(-ln(9)/(Fs*T)); detector runs once per hop.
#define DEEPFIR_INPUT_ATTACK_ALPHA 0.955256382f
#define DEEPFIR_INPUT_RELEASE_ALPHA 0.998856267f
#define DEEPFIR_INPUT_LIMITER_ALPHA 0.993157193f
#define DEEPFIR_INPUT_DETECTOR_ALPHA 0.895958460f
#define DEEPFIR_INPUT_PREAMP 11.220184543f
#define DEEPFIR_INPUT_CEILING 0.891250938f
#define DEEPFIR_INPUT_KNEE_START 0.177827941f
#define DEEPFIR_INPUT_MEANINGFUL_GAIN 0.988553095f
#define DEEPFIR_INPUT_RMS_SAMPLES 1600U

static int overlaps(const void *a, size_t a_size, const void *b, size_t b_size) {
    const uintptr_t x = (uintptr_t)a;
    const uintptr_t y = (uintptr_t)b;
    return x <= y ? y - x < a_size : x - y < b_size;
}

void deepfir_input_conditioner_reset(deepfir_input_conditioner_t *state) {
    if (state != NULL) {
        memset(state, 0, sizeof(*state));
        state->min_gain = 1.0f;
        deepfir_input_conditioner_reset_stream(state);
    }
}

void deepfir_input_conditioner_reset_stream(deepfir_input_conditioner_t *state) {
    if (state != NULL) {
        state->gain = 1.0f;
        state->compressor_gain = 1.0f;
        state->limiter_gain = 1.0f;
        state->detector_peak = 0.0f;
        state->input_square_sum = 0.0f;
        state->output_square_sum = 0.0f;
        state->rms_window_samples = 0;
    }
}

static int in_range(float value, float minimum, float maximum) {
    return isfinite(value) && value >= minimum && value <= maximum;
}

int deepfir_input_conditioner_process(deepfir_input_conditioner_t *state,
                                      const int32_t *input, uint32_t count, float *output) {
    const size_t bytes = DEEPFIR_DSP_HOP * sizeof(float);
    if (state == NULL || input == NULL || output == NULL || count != DEEPFIR_DSP_HOP ||
        overlaps(input, bytes, output, bytes) ||
        overlaps(state, sizeof(*state), input, bytes) ||
        overlaps(state, sizeof(*state), output, bytes)) {
        return -1;
    }
    if (!in_range(state->gain, 0.0f, 1.0f) || state->gain == 0.0f ||
        !in_range(state->compressor_gain, 0.0f, 1.0f) || state->compressor_gain == 0.0f ||
        !in_range(state->limiter_gain, 0.0f, 1.0f) || state->limiter_gain == 0.0f ||
        !in_range(state->detector_peak, 0.0f, DEEPFIR_INPUT_PREAMP) ||
        !in_range(state->input_peak, 0.0f, 1.0f) ||
        !in_range(state->output_peak, 0.0f, DEEPFIR_INPUT_CEILING) ||
        !in_range(state->min_gain, 0.0f, 1.0f) ||
        !in_range(state->input_rms, 0.0f, 1.0f) ||
        !in_range(state->output_rms, 0.0f, DEEPFIR_INPUT_CEILING + 1e-6f) ||
        !in_range(state->input_square_sum, 0.0f, DEEPFIR_INPUT_RMS_SAMPLES) ||
        !in_range(state->output_square_sum, 0.0f, DEEPFIR_INPUT_RMS_SAMPLES) ||
        state->rms_window_samples >= DEEPFIR_INPUT_RMS_SAMPLES ||
        state->rms_window_samples % DEEPFIR_DSP_HOP != 0) {
        return -1;
    }
    for (uint32_t i = 0; i < count; ++i) {
        if (input[i] < AUDIO_PCM24_MIN || input[i] > AUDIO_PCM24_MAX) {
            return -1;
        }
    }
    float pcm[DEEPFIR_DSP_HOP];
#if DEEPFIR_INPUT_HAS_MVEF
    for (uint32_t i = 0; i < count; i += 4U) {
        vst1q_f32(pcm + i, vcvtq_n_f32_s32(vld1q_s32(input + i), 23));
    }
#else
    for (uint32_t i = 0; i < count; ++i) {
        pcm[i] = audio_pcm24_to_float(input[i]);
    }
#endif
    deepfir_input_conditioner_t next = *state;
    float peak = 0.0f;
    float input_square_sum = 0.0f;
    float output_square_sum = 0.0f;
    for (uint32_t i = 0; i < count; ++i) {
        peak = fmaxf(peak, fabsf(pcm[i]));
        input_square_sum += pcm[i] * pcm[i];
        pcm[i] *= DEEPFIR_INPUT_PREAMP;
    }
    next.input_peak = fmaxf(next.input_peak, peak);
    peak *= DEEPFIR_INPUT_PREAMP;
    next.detector_peak = fmaxf(peak, next.detector_peak * DEEPFIR_INPUT_DETECTOR_ALPHA);
    peak = next.detector_peak;
    float target = 1.0f;
    if (peak > DEEPFIR_INPUT_KNEE_START) {
        const float over = 20.0f * log10f(peak) + 12.0f;
        const float gain_db = over < 3.0f ? -(over + 3.0f) * (over + 3.0f) / 18.0f
                                          : -(2.0f / 3.0f) * over;
        target = expf(gain_db * 0.115129255f);
    }
    for (uint32_t i = 0; i < count; ++i) {
        const float alpha = target < next.compressor_gain ? DEEPFIR_INPUT_ATTACK_ALPHA
                                              : DEEPFIR_INPUT_RELEASE_ALPHA;
        next.compressor_gain = target + alpha * (next.compressor_gain - target);
        next.limiter_gain = 1.0f + DEEPFIR_INPUT_LIMITER_ALPHA * (next.limiter_gain - 1.0f);
        const float compressed = pcm[i] * next.compressor_gain;
        const float magnitude = fabsf(compressed);
        if (magnitude * next.limiter_gain > DEEPFIR_INPUT_CEILING) {
            next.limiter_gain = DEEPFIR_INPUT_CEILING / magnitude;
            ++next.limited_samples;
        }
        next.gain = next.compressor_gain * next.limiter_gain;
        next.min_gain = fminf(next.min_gain, next.gain);
        if (next.gain <= DEEPFIR_INPUT_MEANINGFUL_GAIN) {
            ++next.compressed_samples;
        }
        // Roundoff guard only; the gain reduction above handles actual limiting.
        pcm[i] = fmaxf(-DEEPFIR_INPUT_CEILING,
                      fminf(DEEPFIR_INPUT_CEILING, compressed * next.limiter_gain));
        next.output_peak = fmaxf(next.output_peak, fabsf(pcm[i]));
        output_square_sum += pcm[i] * pcm[i];
    }
    // Accumulate per hop to reduce rounding over the 100 ms RMS window.
    next.input_square_sum += input_square_sum;
    next.output_square_sum += output_square_sum;
    next.rms_window_samples += count;
    if (next.rms_window_samples == DEEPFIR_INPUT_RMS_SAMPLES) {
        next.input_rms = sqrtf(next.input_square_sum / DEEPFIR_INPUT_RMS_SAMPLES);
        next.output_rms = sqrtf(next.output_square_sum / DEEPFIR_INPUT_RMS_SAMPLES);
        next.input_square_sum = 0.0f;
        next.output_square_sum = 0.0f;
        next.rms_window_samples = 0;
    }
    next.processed_samples += count;
    memcpy(output, pcm, sizeof(pcm));
    *state = next;
    return 0;
}
