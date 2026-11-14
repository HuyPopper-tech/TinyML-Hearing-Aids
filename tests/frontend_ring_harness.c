/**
 * @file frontend_ring_harness.c
 * @brief Independent shift-history oracle for the causal FFT input frame.
 * @author Bui Minh Huy
 * @date 2026-11-14
 */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "services/deepfir/deepfir_dsp.h"

typedef struct {
    float history[256];
    const float *window;
    unsigned calls;
    int fail;
} frame_oracle_t;

static int check_frame(float *frame, uint32_t count, uint8_t inverse, void *context) {
    frame_oracle_t *oracle = context;
    assert(count == 256 && inverse == 0);
    for (unsigned i = 0; i < 256; ++i) {
        const float expected = oracle->history[i] * oracle->window[i];
        const float zero = 0;
        assert(!memcmp(frame + 2*i, &expected, sizeof(float)));
        assert(!memcmp(frame + 2*i + 1, &zero, sizeof(float)));
    }
    ++oracle->calls;
    return oracle->fail;
}

int main(void) {
    deepfir_feature_state_t state = {0}, saved;
    frame_oracle_t oracle = {0};
    state.scale = 9.672559826867655e-05;
    state.fft = check_frame;
    state.fft_context = &oracle;
    oracle.window = state.window;
    deepfir_feature_reset(&state);
    uint32_t random = 0x1455;
    float input[16];
    int16_t output[387], before[387];
    for (unsigned hop = 0; hop < 8192; ++hop) {
        if (hop % 503 == 0) {
            deepfir_feature_reset(&state);
            memset(oracle.history, 0, sizeof(oracle.history));
            assert(state.history_position == 0);
        }
        for (unsigned i = 0; i < 16; ++i) {
            random = random * 1664525u + 1013904223u;
            input[i] = hop < 3 ? 0 : ((int32_t)(random >> 8) - 8388608) / 8388608.0f;
            if (hop == 3) {
                input[i] = i == 15 ? 1.0f/8388608 : 0;
            } else if (hop == 4) {
                input[i] = i & 1 ? 8388607.0f/8388608 : -1;
            }
        }
        memmove(oracle.history, oracle.history + 16, 240*sizeof(float));
        memcpy(oracle.history + 240, input, sizeof(input));
        const uint32_t next = (state.history_position + 16) & 255;
        assert(!deepfir_feature_float(input, 16, output, &state));
        assert(state.history_position == next);
        for (unsigned i = 0; i < 256; ++i) {
            assert(state.history[(next + i) & 255] == oracle.history[i]);
        }
    }
    saved = state;
    memset(output, 0x35, sizeof(output));
    memcpy(before, output, sizeof(output));
    input[15] = NAN;
    assert(deepfir_feature_float(input, 16, output, &state));
    assert(!memcmp(&saved, &state, sizeof(state)) && !memcmp(before, output, sizeof(output)));
    input[15] = 0;
    assert(deepfir_feature_float(input, 15, output, &state));
    assert(deepfir_feature_float(NULL, 16, output, &state));
    for (unsigned test = 0; test < 2; ++test) {
        state.history_position = test ? 1 : 256;
        saved = state;
        assert(deepfir_feature_float(input, 16, output, &state));
        assert(!memcmp(&saved, &state, sizeof(state)) && !memcmp(before, output, sizeof(output)));
    }
    deepfir_feature_reset(&state);
    memset(oracle.history, 0, sizeof(oracle.history));
    memset(input, 0, sizeof(input));
    oracle.fail = 1;
    assert(deepfir_feature_float(input, 16, output, &state));
    deepfir_feature_reset(&state);
    assert(state.history_position == 0);
    oracle.fail = 0;
    assert(!deepfir_feature_float(input, 16, output, &state));
    assert(oracle.calls == 8194);
    puts("causal FFT frames: 8192 hops, 4194304 exact float lanes; wrap/reset/failure gates PASS");
    return 0;
}
