/**
 * @file deepfir_dsp.c
 * @brief R2 feature and output crossfade.
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

static float clamp01(float value) {
    return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
}

void deepfir_dsp_reset(deepfir_dsp_state_t *state, const float *initial_taps) {
    uint32_t index;
    if (state == 0) {
        return;
    }
    for (index = 0u; index < DEEPFIR_DSP_HOP; ++index) {
        // Midpoint samples avoid a zero/one endpoint in the 16-sample Hann fade.
        float phase = ((float)index + 0.5f) / (float)DEEPFIR_DSP_HOP;
        state->crossfade_blend[index] =
            0.5f - 0.5f * cosf(3.14159265358979323846f * clamp01(phase));
    }
    for (index = 0; index < DEEPFIR_DSP_TAPS; ++index) {
        state->history[index] = 0.0f;
        state->previous_taps[index] = initial_taps ? initial_taps[index] : 0.0f;
        state->current_taps[index] = state->previous_taps[index];
    }
    if (!initial_taps) {
        state->previous_taps[64] = 1.0f;
        state->current_taps[64] = 1.0f;
    }
    state->sample_count = 0u;
}

static int deepfir_dsp_process_scalar(deepfir_dsp_state_t *state, const float *samples,
                                      uint32_t sample_count, const float *next_taps,
                                      float *output) {
    uint32_t sample;
    uint32_t index;
    if (!state || !samples || !next_taps || !output) {
        return -1;
    }
    if (sample_count == 0u) {
        return -2;
    }
    for (sample = 0; sample < sample_count; ++sample) {
        uint32_t absolute_sample = state->sample_count + sample;
        uint32_t tap_row = sample_count > DEEPFIR_DSP_HOP ? absolute_sample / DEEPFIR_DSP_HOP : 0u;
        const float *tap_source = next_taps + tap_row * DEEPFIR_DSP_TAPS;
        float blend;
        float previous_sum = 0.0f, current_sum = 0.0f;
        if ((absolute_sample % DEEPFIR_DSP_HOP) == 0u) {
            for (index = 0; index < DEEPFIR_DSP_TAPS; ++index) {
                state->previous_taps[index] = state->current_taps[index];
                state->current_taps[index] = tap_source[index];
            }
        }
        for (index = DEEPFIR_DSP_TAPS - 1u; index > 0u; --index) {
            state->history[index] = state->history[index - 1u];
        }
        state->history[0] = samples[sample];
        blend = state->crossfade_blend[absolute_sample % DEEPFIR_DSP_HOP];
        // Both FIRs see the same current input and history, without an extra hop of delay.
        for (index = 0; index < DEEPFIR_DSP_TAPS; ++index) {
            previous_sum += state->previous_taps[index] * state->history[index];
            current_sum += state->current_taps[index] * state->history[index];
        }
        output[sample] = previous_sum * (1.0f - blend) + current_sum * blend;
    }
    state->sample_count += sample_count;
    if ((state->sample_count % DEEPFIR_DSP_HOP) == 0u) {
        memcpy(state->previous_taps, state->current_taps, sizeof(state->previous_taps));
    }
    return 0;
}

#if DEEPFIR_DSP_HAS_MVEF
static bool g_mve_ready = true;

// Four lanes are consecutive output samples; each lane accumulates taps 0..127.
__attribute__((noinline)) static int deepfir_dsp_process_mve(deepfir_dsp_state_t *state,
                                                             const float *samples,
                                                             const float *next_taps,
                                                             float *output) {
    float extended[DEEPFIR_DSP_TAPS - 1u + DEEPFIR_DSP_HOP];

    for (uint32_t index = 0u; index < DEEPFIR_DSP_TAPS - 1u; ++index) {
        extended[index] = state->history[DEEPFIR_DSP_TAPS - 2u - index];
    }
    memcpy(extended + DEEPFIR_DSP_TAPS - 1u, samples, DEEPFIR_DSP_HOP * sizeof(samples[0]));
    memcpy(state->previous_taps, state->current_taps, sizeof(state->previous_taps));
    memcpy(state->current_taps, next_taps, sizeof(state->current_taps));

    for (uint32_t sample = 0u; sample < DEEPFIR_DSP_HOP; sample += 4u) {
        const float32x4_t blend = vld1q_f32(state->crossfade_blend + sample);
        const float32x4_t previous_weight = vsubq_f32(vdupq_n_f32(1.0f), blend);
        float32x4_t previous_sum = vdupq_n_f32(0.0f);
        float32x4_t current_sum = vdupq_n_f32(0.0f);

        for (uint32_t tap_index = 0u; tap_index < DEEPFIR_DSP_TAPS; ++tap_index) {
            const float32x4_t history =
                vld1q_f32(extended + DEEPFIR_DSP_TAPS - 1u + sample - tap_index);
            previous_sum = vfmaq_n_f32(previous_sum, history, state->previous_taps[tap_index]);
            current_sum = vfmaq_n_f32(current_sum, history, state->current_taps[tap_index]);
        }
        vst1q_f32(output + sample, vaddq_f32(vmulq_f32(previous_sum, previous_weight),
                                             vmulq_f32(current_sum, blend)));
    }

    memmove(state->history + DEEPFIR_DSP_HOP, state->history,
            (DEEPFIR_DSP_TAPS - DEEPFIR_DSP_HOP) * sizeof(state->history[0]));
    for (uint32_t index = 0u; index < DEEPFIR_DSP_HOP; ++index) {
        state->history[index] = samples[DEEPFIR_DSP_HOP - 1u - index];
    }
    memcpy(state->previous_taps, state->current_taps, sizeof(state->previous_taps));
    state->sample_count += DEEPFIR_DSP_HOP;
    return 0;
}
#endif

bool deepfir_dsp_mve_ready(void) {
#if DEEPFIR_DSP_HAS_MVEF
    return g_mve_ready;
#else
    return false;
#endif
}

bool deepfir_dsp_prepare_mve(void) {
#if DEEPFIR_DSP_HAS_MVEF
    static deepfir_dsp_state_t reference_state;
    static deepfir_dsp_state_t vector_state;
    float samples[DEEPFIR_DSP_HOP];
    float taps[DEEPFIR_DSP_TAPS];
    float reference_output[DEEPFIR_DSP_HOP];
    float vector_output[DEEPFIR_DSP_HOP];
    uint32_t random_state = 20260923u;

    g_mve_ready = false;
    deepfir_dsp_reset(&reference_state, NULL);
    deepfir_dsp_reset(&vector_state, NULL);
    for (uint32_t hop = 0u; hop < 4u; ++hop) {
        if (hop == 2u) {
            deepfir_dsp_reset(&reference_state, NULL);
            deepfir_dsp_reset(&vector_state, NULL);
        }
        for (uint32_t index = 0u; index < DEEPFIR_DSP_HOP; ++index) {
            random_state ^= random_state << 13;
            random_state ^= random_state >> 17;
            random_state ^= random_state << 5;
            samples[index] = ((int32_t)(random_state & 65535u) - 32768) / 32768.0f;
        }
        for (uint32_t index = 0u; index < DEEPFIR_DSP_TAPS; ++index) {
            random_state ^= random_state << 13;
            random_state ^= random_state >> 17;
            random_state ^= random_state << 5;
            taps[index] = ((int32_t)(random_state & 1023u) - 512) / 8192.0f;
        }
        if (deepfir_dsp_process_scalar(&reference_state, samples, DEEPFIR_DSP_HOP, taps,
                                       reference_output) != 0 ||
            deepfir_dsp_process_mve(&vector_state, samples, taps, vector_output) != 0) {
            return false;
        }
        for (uint32_t index = 0u; index < DEEPFIR_DSP_HOP; ++index) {
            const float difference = fabsf(reference_output[index] - vector_output[index]);
            const float tolerance = 2.0e-5f + 2.0e-6f * fabsf(reference_output[index]);
            if (difference > tolerance) {
                return false;
            }
        }
        if (memcmp(&reference_state, &vector_state, sizeof(reference_state)) != 0) {
            return false;
        }
    }
    g_mve_ready = true;
    return true;
#else
    return false;
#endif
}

int deepfir_dsp_process(deepfir_dsp_state_t *state, const float *samples, uint32_t sample_count,
                        const float *next_taps, float *output) {
#if DEEPFIR_DSP_HAS_MVEF
    if (g_mve_ready && state != NULL && samples != NULL && next_taps != NULL && output != NULL &&
        sample_count == DEEPFIR_DSP_HOP && (state->sample_count % DEEPFIR_DSP_HOP) == 0u) {
        return deepfir_dsp_process_mve(state, samples, next_taps, output);
    }
#endif
    return deepfir_dsp_process_scalar(state, samples, sample_count, next_taps, output);
}

int16_t deepfir_pcm16_from_float(float value) {
    if (!isfinite(value)) {
        return 0;
    }
    // Saturate before conversion; C integer conversion must never wrap clipped PCM.
    const float scaled = value * 32768.0f;
    if (scaled >= 32767.0f) {
        return 32767;
    }
    if (scaled <= -32768.0f) {
        return -32768;
    }
    return (int16_t)(scaled >= 0.0f ? scaled + 0.5f : scaled - 0.5f);
}
