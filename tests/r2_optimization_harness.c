/**
 * @file r2_optimization_harness.c
 * @brief Differential quantizer, shared frontend and recurrent arithmetic gates.
 * @author Bui Minh Huy
 * @date 2026-11-08
 */
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "services/deepfir/deepfir_model_adapter.h"
#include "services/deepfir/deepfir_model_binding.h"
// Exercise the private quantizer without adding a production API solely for tests.
#include "services/deepfir/deepfir_features.c"

static uint32_t random_state = 0x7217b951U;

static uint32_t next_random(void) {
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}

static int reference_quantize(float value, double scale, int32_t zero, int16_t *out) {
    double code = (double)value / scale + zero;
    if (!isfinite(code)) {
        return -2;
    }
    code = code >= 0 ? floor(code + 0.5) : -floor(-code + 0.5);
    *out = (int16_t)(code < -32768 ? -32768 : code > 32767 ? 32767 : (int)code);
    return 0;
}

static int check_quantizer(float value, deepfir_feature_state_t *state) {
    int16_t actual = 1234, expected = 1234;
    const int wanted = reference_quantize(value, state->scale, state->zero_point, &expected);
    const int status = quantize_feature(value, state, 1.0 / state->scale, &actual);
    if (status != wanted || actual != expected) {
        fprintf(stderr, "quantizer value=%a scale=%a zp=%ld actual=%d expected=%d status=%d/%d\n",
                value, state->scale, (long)state->zero_point, actual, expected, status, wanted);
        return 1;
    }
    return 0;
}

static int quantization_edges(void) {
    const double scales[] = {9.672559826867655e-05, 0.5, 1.0, 1e-300, 1e300, DBL_MIN, 1e-320};
    const int32_t zeros[] = {0, -32768, 32767, -17};
    deepfir_feature_state_t state = {0};
    unsigned count = 0;
    for (unsigned s = 0; s < sizeof(scales) / sizeof(scales[0]); ++s) {
        state.scale = scales[s];
        for (unsigned z = 0; z < sizeof(zeros) / sizeof(zeros[0]); ++z) {
            state.zero_point = zeros[z];
            const float edges[] = {0,        -0.0f,    FLT_MIN,   -FLT_MIN, FLT_MAX,
                                   -FLT_MAX, INFINITY, -INFINITY, NAN};
            for (unsigned i = 0; i < sizeof(edges) / sizeof(edges[0]); ++i) {
                if (check_quantizer(edges[i], &state)) {
                    return 1;
                }
                ++count;
            }
            for (int code = -65536; code <= 65536; ++code) {
                const float midpoint = (float)((code + 0.5 - zeros[z]) * scales[s]);
                const float values[] = {midpoint, nextafterf(midpoint, -INFINITY),
                                        nextafterf(midpoint, INFINITY)};
                for (unsigned i = 0; i < 3; ++i) {
                    if (check_quantizer(values[i], &state)) {
                        return 2;
                    }
                    ++count;
                }
            }
            for (unsigned i = 0; i < 20000; ++i) {
                uint32_t bits = next_random();
                float value;
                memcpy(&value, &bits, sizeof(value));
                if (check_quantizer(value, &state)) {
                    return 3;
                }
                ++count;
            }
        }
    }
    // Reciprocal multiplication and exact division can straddle the double overflow boundary.
    state.zero_point = 0;
    const float large[] = {FLT_MAX, -FLT_MAX, 1.0f, -1.0f};
    for (unsigned i = 0; i < sizeof(large) / sizeof(large[0]); ++i) {
        double scale = fabs((double)large[i]) / DBL_MAX;
        for (unsigned j = 0; j < 32; ++j) {
            scale = nextafter(scale, 0.0);
        }
        for (unsigned j = 0; j < 65; ++j) {
            state.scale = scale;
            if (check_quantizer(large[i], &state)) {
                return 4;
            }
            scale = nextafter(scale, INFINITY);
            ++count;
        }
    }
    printf("quantizer comparisons=%u mismatches=0\n", count);
    return 0;
}

static int float_quantization_guard(void) {
    const double scales[] = {9.672559826867655e-05, 0.5, 1.0};
    const int32_t zeros[] = {0, -32768, 32767, -17};
    unsigned accepted = 0;
    unsigned guard_edges = 0;
    deepfir_feature_state_t state = {0};
    for (unsigned s = 0; s < 3; ++s) {
        state.scale = scales[s];
        for (unsigned z = 0; z < 4; ++z) {
            state.zero_point = zeros[z];
            for (int code = -32768; code <= 32767; ++code) {
                const float value = (float)((code - zeros[z]) * scales[s]);
                int16_t actual = 1234, expected = 1234;
                if (!quantize_feature_f32(value, (float)(1.0 / scales[s]), zeros[z], &actual) ||
                    reference_quantize(value, scales[s], zeros[z], &expected) ||
                    actual != expected) {
                    return 1;
                }
                ++accepted;
                const float halfway = (float)((code + 0.5 - zeros[z]) * scales[s]);
                if (quantize_feature_f32(halfway, (float)(1.0 / scales[s]), zeros[z], &actual)) {
                    return 2;
                }
                // Check both sides of the float/double handoff, including adjacent float values.
                const double offsets[] = {0.46875, 0.53125};
                for (unsigned edge = 0; edge < 2; ++edge) {
                    const float boundary = (float)((code + offsets[edge] - zeros[z]) * scales[s]);
                    const float values[] = {boundary, nextafterf(boundary, -INFINITY),
                                            nextafterf(boundary, INFINITY)};
                    for (unsigned i = 0; i < 3; ++i) {
                        if (check_quantizer(values[i], &state)) {
                            return 6;
                        }
                        ++guard_edges;
                    }
                }
            }
        }
    }
    int16_t output = 1234;
    const float invalid_inverse[] = {0, -1, INFINITY, NAN, FLT_MIN / 2};
    for (unsigned i = 0; i < 5; ++i) {
        if (quantize_feature_f32(1, invalid_inverse[i], 0, &output) || output != 1234) {
            return 3;
        }
    }
    if (quantize_feature_f32(INFINITY, 1, 0, &output) || quantize_feature_f32(NAN, 1, 0, &output) ||
        output != 1234) {
        return 4;
    }
    puts("float quantizer fast-path comparisons=786432 mismatches=0; half-code fallback PASS");
    printf("float/double guard-edge comparisons=%u mismatches=0\n", guard_edges);
    return accepted == 786432 ? 0 : 5;
}

static int frame_check_fft(float *frame, uint32_t length, uint8_t inverse, void *context) {
    const deepfir_feature_state_t *state = context;
    if (length != 256 || inverse != 0) {
        return -1;
    }
    for (unsigned i = 0; i < 256; ++i) {
        if (frame[2 * i] != state->history[i] * state->window[i] || frame[2 * i + 1] != 0) {
            return -2;
        }
    }
    return 0;
}

static int frontend_equivalence(void) {
    deepfir_feature_state_t a = {0}, b = {0};
    a.scale = b.scale = 9.672559826867655e-05;
    a.fft = b.fft = frame_check_fft;
    a.fft_context = &a;
    b.fft_context = &b;
    deepfir_feature_reset(&a);
    deepfir_feature_reset(&b);
    int16_t pcm[16], first[387], second[387];
    float normalized[16];
    for (unsigned hop = 0; hop < 4096; ++hop) {
        if (hop == 2000) {
            deepfir_feature_reset(&a);
            deepfir_feature_reset(&b);
        }
        for (unsigned i = 0; i < 16; ++i) {
            pcm[i] = hop == 0 ? 0 : (int16_t)next_random();
            normalized[i] = (float)pcm[i] / 32768.0f;
        }
        if (deepfir_feature_pcm16(pcm, 16, first, &a) ||
            deepfir_feature_float(normalized, 16, second, &b) ||
            memcmp(first, second, sizeof(first)) ||
            memcmp(a.history, b.history, sizeof(a.history))) {
            return 1;
        }
    }
    float saved[256];
    memcpy(saved, b.history, sizeof(saved));
    normalized[15] = NAN;
    if (deepfir_feature_float(normalized, 16, second, &b) == 0 ||
        memcmp(saved, b.history, sizeof(saved)) ||
        deepfir_feature_float(normalized, 15, second, &b) == 0 ||
        deepfir_feature_float(NULL, 16, second, &b) == 0) {
        return 2;
    }
    puts("frontend PCM/float comparisons=4096 mismatches=0; finite/size gates PASS");
    return 0;
}

static int feedback_edges(void) {
    const int64_t multipliers[] = {0, 1, 1075624845, 2147371134, INT64_C(2147483648)};
    for (unsigned shift = 0; shift <= 46; ++shift) {
        for (unsigned m = 0; m < sizeof(multipliers) / sizeof(multipliers[0]); ++m) {
            for (int code = -32768; code <= 32767; ++code) {
                const int source_zero = (code & 1) ? -32768 : 32767;
                const int target_zero = (code & 2) ? -17 : 19;
                const int64_t product = ((int64_t)code - source_zero) * multipliers[m];
                const int64_t denominator = INT64_C(1) << shift;
                int64_t expected =
                    ((product < 0 ? -product : product) + denominator / 2) / denominator;
                expected = (product < 0 ? -expected : expected) + target_zero;
                expected = expected < -32768 ? -32768 : expected > 32767 ? 32767 : expected;
                if (deepfir_state_feedback((int16_t)code, multipliers[m], shift, source_zero,
                                           target_zero) != expected) {
                    return 1;
                }
            }
        }
    }
    if (deepfir_state_feedback(1, 1, 47, 0, 0) != 0 ||
        deepfir_state_feedback(1, -1, 1, 0, 0) != 0) {
        return 2;
    }
    puts("feedback comparisons=15400960 mismatches=0; shifts0..46, offsets and saturation PASS");
    return 0;
}

typedef struct {
    deepfir_model_adapter_t adapter;
    unsigned hop;
    int feature_failure;
    unsigned invoked;
} stream_fixture_t;

static int shared_feature(const float *pcm, uint32_t count, int16_t *feature, void *context) {
    stream_fixture_t *fixture = context;
    if (count != 16 || feature != fixture->adapter.feature_context_buffer + 387) {
        return -91;
    }
    for (unsigned i = 0; i < 16; ++i) {
        if (pcm[i] != ((int)i - 8) / 32768.0f) {
            return -92;
        }
    }
    for (unsigned i = 0; i < 387; ++i) {
        feature[i] = (int16_t)(fixture->hop + 1);
    }
    return fixture->feature_failure ? -93 : 0;
}

static int stream_infer(const deepfir_tensor_map_t *in, uint32_t ni, deepfir_tensor_map_t *out,
                        uint32_t no, void *context) {
    stream_fixture_t *fixture = context;
    if (ni != 3 || no != 4) {
        return -94;
    }
    for (unsigned i = 0; i < 387; ++i) {
        if (in[0].data[i] != (int)fixture->hop || in[0].data[i + 387] != (int)fixture->hop + 1) {
            return -95;
        }
    }
    for (unsigned i = 0; i < no; ++i) {
        memset(out[i].data, 0, out[i].elements * sizeof(int16_t));
    }
    ++fixture->invoked;
    return 0;
}

static int stream_fft(float *buffer, uint32_t length, uint8_t inverse, void *context) {
    (void)context;
    if (length != 128 || inverse != 1) {
        return -1;
    }
    memset(buffer, 0, 256 * sizeof(float));
    return 0;
}

static int direct_feature_context(void) {
    static stream_fixture_t fixture;
    int16_t pcm[16];
    float output[16];
    if (deepfir_model_init_qparams(&fixture.adapter)) {
        return 1;
    }
    fixture.adapter.feature = shared_feature;
    fixture.adapter.feature_context = &fixture;
    fixture.adapter.infer_mapped = stream_infer;
    fixture.adapter.infer_context = &fixture;
    fixture.adapter.fft = stream_fft;
    deepfir_model_adapter_reset(&fixture.adapter);
    for (unsigned i = 0; i < 16; ++i) {
        pcm[i] = (int16_t)((int)i - 8);
    }
    for (fixture.hop = 0; fixture.hop < 10; ++fixture.hop) {
        const int status = deepfir_model_adapter_process_pcm16(&fixture.adapter, pcm, 16, output);
        if (status) {
            fprintf(stderr, "shared feature/context status=%d\n", status);
            return 2;
        }
    }
    fixture.feature_failure = 1;
    for (unsigned i = 0; i < 16; ++i) {
        output[i] = 42;
    }
    if (deepfir_model_adapter_process_pcm16(&fixture.adapter, pcm, 16, output) != -93 ||
        !fixture.adapter.faulted || fixture.invoked != 10) {
        return 3;
    }
    for (unsigned i = 0; i < 16; ++i) {
        if (output[i] != 42) {
            return 4;
        }
    }
    if (deepfir_model_adapter_process_pcm16(&fixture.adapter, pcm, 16, output) == 0 ||
        fixture.invoked != 10) {
        return 5;
    }
    fixture.hop = 0;
    fixture.feature_failure = 0;
    deepfir_model_adapter_reset(&fixture.adapter);
    if (deepfir_model_adapter_process_pcm16(&fixture.adapter, pcm, 16, output)) {
        return 6;
    }
    puts("shared PCM/current-context lifecycle PASS; failed feature never invoked/published");
    return 0;
}

int main(void) {
    if (quantization_edges() || float_quantization_guard() || frontend_equivalence() ||
        feedback_edges() || direct_feature_context()) {
        return 1;
    }
    return 0;
}
