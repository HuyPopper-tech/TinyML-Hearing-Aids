/**
 * @file r2_dsp_optimization_harness.c
 * @brief Exact PCM conversion and pre-optimization MVE FIR numerical oracle.
 * @author Bui Minh Huy
 * @date 2026-11-14
 */
#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "services/deepfir/deepfir_dsp.h"

static unsigned pcm_codes;
static uint32_t rng = 0x34cb1267u;

static uint32_t random_word(void) {
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

// Frozen scalar contract, independent of the production helper under test.
static int16_t pcm_reference(float value) {
    if (!isfinite(value)) {
        return 0;
    }
    float scaled = value * 32768.0f;
    if (scaled >= 32767.0f) {
        return 32767;
    }
    if (scaled <= -32768.0f) {
        return -32768;
    }
    return (int16_t)(scaled >= 0 ? scaled + 0.5f : scaled - 0.5f);
}

static void check_pcm(const float *input) {
    int16_t output[18];
    for (unsigned i = 0; i < 18; ++i) {
        output[i] = 12345;
    }
    assert(deepfir_pcm16_render_hop(input, output + 1) == 0);
    assert(output[0] == 12345 && output[17] == 12345);
    for (unsigned i = 0; i < 16; ++i) {
        assert(output[i + 1] == pcm_reference(input[i]));
        assert(deepfir_pcm16_from_float(input[i]) == pcm_reference(input[i]));
        ++pcm_codes;
    }
}

static void pcm_tests(void) {
    float input[16] = {0};
    // Every representable PCM16 half-code and both adjacent float neighbors.
    for (int code = -32769; code <= 32768; ++code) {
        float center = ((float)code + 0.5f) / 32768.0f;
        for (unsigned lane = 0; lane < 16; ++lane) {
            input[lane] = lane % 3 == 0 ? center :
                          nextafterf(center, lane % 3 == 1 ? -INFINITY : INFINITY);
        }
        check_pcm(input);
    }
    const float edges[16] = {0, -0.0f, FLT_MIN, -FLT_MIN, FLT_MAX, -FLT_MAX,
                            1, -1, 10, -10, 32767.0f / 32768.0f,
                            -32767.0f / 32768.0f, 0x1p-149f, -0x1p-149f, 0.5f, -0.5f};
    check_pcm(edges);
    for (unsigned block = 0; block < 65536; ++block) {
        for (unsigned lane = 0; lane < 16; ++lane) {
            uint32_t bits = random_word();
            if ((bits & 0x7f800000u) == 0x7f800000u) {
                bits ^= 0x00800000u;
            }
            memcpy(&input[lane], &bits, sizeof(bits));
        }
        check_pcm(input);
    }
    const float invalid[] = {NAN, INFINITY, -INFINITY};
    for (unsigned lane = 0; lane < 16; ++lane) {
        for (unsigned kind = 0; kind < 3; ++kind) {
            int16_t output[16], before[16];
            memset(input, 0, sizeof(input));
            input[lane] = invalid[kind];
            memset(output, 0x35, sizeof(output));
            memcpy(before, output, sizeof(output));
            assert(deepfir_pcm16_render_hop(input, output) == -2);
            assert(memcmp(output, before, sizeof(output)) == 0);
        }
    }
    int16_t output[16];
    memset(output, 0x35, sizeof(output));
    assert(deepfir_pcm16_render_hop(NULL, output) == -1);
    assert(deepfir_pcm16_render_hop(input, NULL) == -1);
    assert(deepfir_pcm16_render_hop(input, (int16_t *)input) == -1);
    assert(deepfir_pcm16_render_hop(input, (int16_t *)(input + 15)) == -1);
}

#if M55M1_DEEPFIR_MVE
// Frozen group4 algorithm: each lane uses the same 128 fused accumulations as before.
static void fir_reference(deepfir_dsp_state_t *state, const float *samples,
                          const float *taps, float *output) {
    float extended[143];
    for (unsigned i = 0; i < 127; ++i) {
        extended[i] = state->history[126 - i];
    }
    memcpy(extended + 127, samples, 16 * sizeof(float));
    memcpy(state->current_taps, taps, sizeof(state->current_taps));
    for (unsigned group = 0; group < 16; group += 4) {
        for (unsigned lane = 0; lane < 4; ++lane) {
            unsigned sample = group + lane;
            float previous = 0, current = 0;
            for (unsigned tap = 0; tap < 128; ++tap) {
                float value = extended[127 + sample - tap];
                previous = fmaf(value, state->previous_taps[tap], previous);
                current = fmaf(value, state->current_taps[tap], current);
            }
            float blend = state->crossfade_blend[sample];
            output[sample] = previous * (1.0f - blend) + current * blend;
        }
    }
    memmove(state->history + 16, state->history, 112 * sizeof(float));
    for (unsigned i = 0; i < 16; ++i) {
        state->history[i] = samples[15 - i];
    }
    memcpy(state->previous_taps, state->current_taps, sizeof(state->previous_taps));
    state->sample_count += 16;
}

static void fir_tests(void) {
    deepfir_dsp_state_t actual, reference;
    float input[16], taps[128], output[16], expected[16];
    assert(deepfir_dsp_prepare_mve());
    assert(deepfir_dsp_mve_ready());
    deepfir_dsp_reset(&actual, NULL);
    reference = actual;
    for (unsigned hop = 0; hop < 8192; ++hop) {
        if (hop % 997 == 0) {
            deepfir_dsp_reset(&actual, NULL);
            reference = actual;
        }
        for (unsigned i = 0; i < 16; ++i) {
            input[i] = ((int32_t)(random_word() & 0xffffff) - 8388608) / 8388608.0f;
            if (hop % 8 == 0) {
                input[i] = 0;
            } else if (hop % 8 == 1) {
                input[i] = i == 0 ? 1.0f : 0.0f;
            }
        }
        for (unsigned i = 0; i < 128; ++i) {
            taps[i] = ((int32_t)(random_word() & 65535) - 32768) / 32768.0f;
        }
        fir_reference(&reference, input, taps, expected);
        assert(deepfir_dsp_process(&actual, input, 16, taps, output) == 0);
        assert(memcmp(output, expected, sizeof(output)) == 0);
        assert(memcmp(&actual, &reference, sizeof(actual)) == 0);
    }
    puts("FIR: 8192 hops / 131072 samples, exact group4-output/state matches; zero mismatches");
}
#endif

int main(void) {
    pcm_tests();
    printf("PCM: %u exact codes; all-lane NaN/Inf atomicity and invalid/alias checks passed\n",
           pcm_codes);
#if M55M1_DEEPFIR_MVE
    fir_tests();
#endif
    return 0;
}
