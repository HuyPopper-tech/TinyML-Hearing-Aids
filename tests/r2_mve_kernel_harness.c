/**
 * @file r2_mve_kernel_harness.c
 * @brief Run production vector branches under an explicit native lane model.
 * @author Bui Minh Huy
 * @date 2026-11-08
 */
#include <assert.h>
#include <stdio.h>
#include "services/deepfir/deepfir_features.c"
#include "services/deepfir/deepfir_model_adapter.c"
#include "services/deepfir/deepfir_model_binding.h"
#include "services/deepfir/deepfir_reconstruction.h"

static unsigned comparisons;

static void check_block(const float *values, unsigned count, deepfir_feature_state_t *state) {
    int16_t output[14];
    for (unsigned i = 0; i < 14; ++i) {
        output[i] = 1234;
    }
    int16_t expected[12];
    int status = 0;
    for (unsigned i = 0; i < count; ++i) {
        double code = (double)values[i] / state->scale + state->zero_point;
        if (!isfinite(code)) {
            status = -2;
            break;
        }
        code = code >= 0 ? floor(code + 0.5) : -floor(-code + 0.5);
        expected[i] = code < -32768 ? -32768 : code > 32767 ? 32767 : (int16_t)code;
    }
    assert(deepfir_quantize_feature_block(values, count, state, 1.0 / state->scale, output + 1) ==
           status);
    assert(output[0] == 1234 && output[count + 1] == 1234);
    if (!status) {
        assert(memcmp(output + 1, expected, count * sizeof(int16_t)) == 0);
    }
    comparisons += count;
}

static void quantizer(void) {
    deepfir_feature_state_t state = {0};
    const double scales[] = {9.672559826867655e-05, 0.5, 1.0};
    const int zeros[] = {0, -32768, 32767};
    const double offsets[] = {0, 0.46875, 0.5, 0.53125};
    for (unsigned s = 0; s < 3; ++s) {
        state.scale = scales[s];
        for (unsigned z = 0; z < 3; ++z) {
            state.zero_point = zeros[z];
            assert(quantizer_check_mve(&state));
            for (int base = -32768; base <= 32764; base += 4) {
                for (unsigned edge = 0; edge < 4; ++edge) {
                    for (int direction = -1; direction <= 1; ++direction) {
                        float values[4];
                        for (unsigned lane = 0; lane < 4; ++lane) {
                            float v =
                                (float)((base + (int)lane + offsets[edge] - zeros[z]) * scales[s]);
                            values[lane] =
                                direction ? nextafterf(v, direction < 0 ? -INFINITY : INFINITY) : v;
                        }
                        check_block(values, 4, &state);
                    }
                }
            }
        }
    }
    const float mixed[12] = {0,    -0.0f, FLT_MIN / 2, -FLT_MIN / 2, FLT_MAX, -FLT_MAX,
                             0.5f, -0.5f, INFINITY,    -INFINITY,    NAN,     1};
    for (unsigned count = 0; count <= 12; ++count) {
        check_block(mixed, count, &state);
    }
    puts("native lane model: production quantizer boundaries/tails/exception masks PASS");
}

static int inject_fft(float *buffer, uint32_t n, uint8_t inverse, void *context) {
    assert(n == 128 && inverse == 1);
    for (unsigned i = 0; i < 256; ++i) {
        buffer[i] = ((int)i - 128) / 128.0f;
    }
    if (context) {
        buffer[255] = NAN;
    }
    return 0;
}

static int capture_pcm24_feature(const float *pcm, uint32_t count, int16_t *feature, void *context) {
    const int32_t *expected = context;
    (void)feature;
    assert(count == 16);
    for (unsigned i = 0; i < count; ++i) {
        assert(pcm[i] == (float)expected[i] / 8388608.0f);
    }
    return -73;
}

static int never_infer(const deepfir_tensor_map_t *in, uint32_t ni, deepfir_tensor_map_t *out,
                       uint32_t no, void *context) {
    (void)in; (void)ni; (void)out; (void)no; (void)context;
    assert(0 && "failed feature must not invoke");
    return -74;
}

int main(void) {
    quantizer();
    static deepfir_model_adapter_t adapter, saved;
    assert(deepfir_model_init_qparams(&adapter) == 0);
    saved = adapter;
    assert(deepfir_model_adapter_prepare_mve(&adapter));
    assert(memcmp(&saved, &adapter, sizeof(saved)) == 0);
    const int32_t pcm24[16] = {-8388608, 8388607, -1, 1, 255, -255, 256, -256,
                              1234567, -1234567, 32767, -32768, 0, 17, -17, 4194304};
    float normalized_output[16];
    for (unsigned i = 0; i < 16; ++i) { normalized_output[i] = 1234; }
    adapter.feature = capture_pcm24_feature;
    adapter.feature_context = (void *)pcm24;
    adapter.infer_mapped = never_infer;
    adapter.fft = inject_fft;
    assert(deepfir_model_adapter_process_pcm24(&adapter, pcm24, 16, normalized_output) == -73);
    assert(adapter.faulted);
    for (unsigned i = 0; i < 16; ++i) { assert(normalized_output[i] == 1234); }
    puts("native lane model: PCM24 endpoints/low bits normalization PASS");
    adapter = saved;
    for (int base = -32768; base <= 32764; base += 4) {
        int16_t source[6] = {1234, base, base + 1, base + 2, base + 3, 1234};
        for (unsigned state = 0; state < 2; ++state) {
            for (unsigned count = 0; count <= 4; ++count) {
                int16_t output[6] = {1234, 1234, 1234, 1234, 1234, 1234};
                deepfir_feedback_q31(source + 1, output + 1, count,
                                     (int32_t)adapter.feedback_multiplier[state],
                                     adapter.feedback_shift[state]);
                for (unsigned i = 0; i < count; ++i) {
                    assert(output[i + 1] ==
                           deepfir_state_feedback(source[i + 1], adapter.feedback_multiplier[state],
                                                  adapter.feedback_shift[state], 0, 0));
                }
                assert(output[0] == 1234 && output[count + 1] == 1234);
            }
        }
    }
    int16_t gain[65] = {0}, phase[65] = {0};
    float scales[2] = {1, 1}, output[128];
    int32_t zero[2] = {0};
    assert(deepfir_reconstruct_taps(gain, phase, scales, zero, inject_fft, NULL, output) == 0);
    for (unsigned n = 0; n < 128; ++n) {
        unsigned shifted = (n + 64) & 127;
        float expected = ((int)(2 * n) - 128) / 128.0f *
                         (0.5f + 0.5f * (1.0f - fabsf((float)shifted - 64.0f) / 64.0f));
        assert(output[shifted] == expected);
        output[shifted] = 1234;
    }
    assert(deepfir_reconstruct_taps(gain, phase, scales, zero, inject_fft, &adapter, output) == -3);
    for (unsigned n = 0; n < 128; ++n) {
        assert(output[n] == 1234);
    }
    printf("native lane model: quantizer comparisons=%u; feedback exhaustive/tails and finite "
           "taper PASS\n",
           comparisons);
    return 0;
}
