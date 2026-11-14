/**
 * @file r2_numeric_harness.c
 * @brief Native entry points exercising actual R2 DSP and recurrent feedback.
 * @author Bui Minh Huy
 * @date 2026-11-08
 */
#include <math.h>
#include <string.h>
#include "services/deepfir/deepfir_model_binding.h"
#include "services/deepfir/deepfir_reconstruction.h"
#include "services/deepfir/deepfir_tensor_binding.h"
#if R2_TEST_CMSIS
#include "services/deepfir/deepfir_cmsis_fft_adapter.h"
static deepfir_cmsis_fft_context_t cmsis_context;
#endif

static deepfir_model_adapter_t adapter;
static deepfir_feature_state_t frontend;
static int failure;
static unsigned calls;

static int fft(float *buffer, uint32_t length, uint8_t inverse, void *user) {
    (void)user;
    if (failure == 2) {
        return -1;
    }
#if R2_TEST_CMSIS
    return deepfir_cmsis_fft_callback(buffer, length, inverse, &cmsis_context);
#else
    float transformed[512];
    for (unsigned k = 0; k < length; ++k) {
        double re = 0, im = 0;
        for (unsigned n = 0; n < length; ++n) {
            double angle = (inverse ? 1 : -1) * 6.283185307179586 * k * n / length;
            re += buffer[2 * n] * cos(angle) - buffer[2 * n + 1] * sin(angle);
            im += buffer[2 * n] * sin(angle) + buffer[2 * n + 1] * cos(angle);
        }
        transformed[2 * k] = (float)(re / (inverse ? length : 1));
        transformed[2 * k + 1] = (float)(im / (inverse ? length : 1));
    }
    memcpy(buffer, transformed, length * 2 * sizeof(float));
    return 0;
#endif
}

static int infer(const deepfir_tensor_map_t *in, uint32_t ni, deepfir_tensor_map_t *out,
                 uint32_t no, void *user) {
    (void)user;
    if (ni != 3 || no != 4 || out[0].elements != 65 || out[1].elements != 65 ||
        out[2].elements != 200 || out[3].elements != 200 || in[1].semantic != DEEPFIR_TENSOR_H ||
        in[2].semantic != DEEPFIR_TENSOR_C) {
        return -90;
    }
    for (unsigned i = 0; i < 4; ++i) {
        for (unsigned j = 0; j < out[i].elements; ++j) {
            out[i].data[j] = i == 0 ? 17675 : i == 1 ? 0 : i == 2 ? 32767 : -32768;
        }
    }
    ++calls;
    return failure == 1 ? -99 : 0;
}

void test_reset(void) {
    memset(&adapter, 0, sizeof(adapter));
    memset(&frontend, 0, sizeof(frontend));
    (void)deepfir_model_init_qparams(&adapter);
    deepfir_model_adapter_reset(&adapter);
    adapter.fft = fft;
    adapter.infer_mapped = infer;
    adapter.feature = deepfir_feature_float;
    adapter.feature_context = &frontend;
    frontend.scale = deepfir_input_scale[0];
    frontend.fft = fft;
    deepfir_feature_reset(&frontend);
    calls = 0;
    failure = 0;
#if R2_TEST_CMSIS
    (void)deepfir_cmsis_fft_init(&cmsis_context);
#endif
}

void test_failure(int value) {
    failure = value;
}
int test_step(const int16_t *pcm, float *out) {
    return deepfir_model_adapter_process_pcm16(&adapter, pcm, 16, out);
}
int test_step24(const int32_t *pcm, float *out) {
    return deepfir_model_adapter_process_pcm24(&adapter, pcm, 16, out);
}
void test_feature_context(int16_t *out) {
    memcpy(out, adapter.feature_context_buffer, sizeof(adapter.feature_context_buffer));
}
int test_feedback(int lane, int code) {
    return deepfir_state_feedback((int16_t)code, DF_FEEDBACK_MULTIPLIER[lane],
                                  DF_FEEDBACK_SHIFT[lane], 0, 0);
}
int test_taps(const int16_t *gain, const int16_t *phase, float *out) {
    return deepfir_reconstruct_taps(gain, phase, deepfir_output_scale, deepfir_output_zero, fft,
                                    NULL, out);
}
int test_features(const int16_t *pcm, int16_t *out) {
    return deepfir_feature_pcm16(pcm, 16, out, &frontend);
}
int test_context(void) {
    test_reset();
    int16_t feature[387];
    float pcm[16] = {0}, output[16];
    for (unsigned i = 0; i < 774; ++i) {
        if (adapter.feature_context_buffer[i]) {
            return 1;
        }
    }
    for (unsigned step = 1; step <= 3; ++step) {
        for (unsigned i = 0; i < 387; ++i) {
            feature[i] = (int16_t)step;
        }
        if (deepfir_model_adapter_process_hop(&adapter, feature, pcm, 16, output)) {
            return 2;
        }
        for (unsigned i = 0; i < 387; ++i) {
            if (adapter.feature_context_buffer[i] != (int)step) {
                return 3;
            }
        }
        if (adapter.h[0] != 32765 || adapter.c[0] != -32768) {
            return 4;
        }
    }
    if (calls != 3 || adapter.feedback_direct[0] || adapter.feedback_direct[1]) {
        return 5;
    }
    return 0;
}
int test_binding_rejections(void) {
    for (unsigned direction = 0; direction < 2; ++direction) {
        unsigned count = direction ? 3 : 4;
        for (unsigned i = 0; i < count; ++i) {
            deepfir_tensor_descriptor_t d = *deepfir_tensor_expected(direction != 0, i);
            if (!deepfir_tensor_validate(direction != 0, i, &d)) {
                return 1;
            }
            d.bytes += 2;
            if (deepfir_tensor_validate(direction != 0, i, &d)) {
                return 2;
            }
            d = *deepfir_tensor_expected(direction != 0, i);
            d.port ^= 1;
            if (deepfir_tensor_validate(direction != 0, i, &d)) {
                return 3;
            }
            d = *deepfir_tensor_expected(direction != 0, i);
            d.shape[1]++;
            if (deepfir_tensor_validate(direction != 0, i, &d)) {
                return 4;
            }
            d = *deepfir_tensor_expected(direction != 0, i);
            d.scale *= 1.01f;
            if (deepfir_tensor_validate(direction != 0, i, &d)) {
                return 5;
            }
            d = *deepfir_tensor_expected(direction != 0, i);
            d.zero_point++;
            if (deepfir_tensor_validate(direction != 0, i, &d)) {
                return 6;
            }
            d = *deepfir_tensor_expected(direction != 0, i);
            d.int16_type = false;
            if (deepfir_tensor_validate(direction != 0, i, &d)) {
                return 7;
            }
            d = *deepfir_tensor_expected(direction != 0, i);
            d.name = "old_model";
            if (deepfir_tensor_validate(direction != 0, i, &d)) {
                return 8;
            }
        }
    }
    return 0;
}
int test_fir_pcm(void) {
    deepfir_dsp_state_t state;
    float samples[16] = {0}, taps[128] = {0}, output[16];
    taps[0] = 2;
    deepfir_dsp_reset(&state, NULL);
    samples[0] = .5f;
    if (deepfir_dsp_process(&state, samples, 16, taps, output)) {
        return 1;
    }
    if (fabsf(output[0] - (.5f - .5f * cosf(3.141592653589793f * .5f / 16))) > 2e-6f) {
        return 2;
    }
    taps[16] = 3;
    memset(samples, 0, sizeof(samples));
    if (deepfir_dsp_process(&state, samples, 16, taps, output)) {
        return 3;
    }
    if (fabsf(output[0] - 1.5f * (.5f - .5f * cosf(3.141592653589793f * .5f / 16))) > 2e-6f) {
        return 4;
    }
    if (deepfir_pcm16_from_float(.5f / 32768) != 1 ||
        deepfir_pcm16_from_float(-.5f / 32768) != -1 || deepfir_pcm16_from_float(2) != 32767 ||
        deepfir_pcm16_from_float(-2) != -32768) {
        return 5;
    }
    return 0;
}
