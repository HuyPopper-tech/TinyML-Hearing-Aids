/**
 * @file input_conditioner_harness.c
 * @brief Independent double oracle for streaming dynamics and adapter routing.
 * @author Bui Minh Huy
 * @date 2026-11-14
 */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "services/deepfir/deepfir_input_conditioner.h"
#include "services/deepfir/deepfir_model_adapter.h"

static double reference_gain = 1.0;
static double reference_limiter = 1.0;
static double reference_peak;
static double max_error;
static uint64_t stream_hash = UINT64_C(14695981039346656037);

static void reference(const int32_t *input, double *output) {
    const double preamp = pow(10.0, 21.0 / 20.0);
    const double ceiling = pow(10.0, -1.0 / 20.0);
    double peak = 0.0;
    for (unsigned i = 0; i < 16; ++i) {
        const double value = fabs(input[i] / 8388608.0 * preamp);
        if (value > peak) {
            peak = value;
        }
    }
    reference_peak = fmax(peak, reference_peak * exp(-log(9.0) * 16 / (16000 * .020)));
    peak = reference_peak;
    double target = 1.0;
    if (peak > pow(10.0, -15.0 / 20.0)) {
        const double level = 20.0 * log10(peak);
        const double over = level + 12.0;
        const double reduction = over < 3.0 ? -(over + 3.0) * (over + 3.0) / 18.0
                                            : -(2.0 / 3.0) * over;
        target = pow(10.0, reduction / 20.0);
    }
    for (unsigned i = 0; i < 16; ++i) {
        const double x = input[i] / 8388608.0 * preamp;
        const double duration = target < reference_gain ? 0.003 : 0.120;
        const double alpha = exp(-log(9.0) / (16000.0 * duration));
        reference_gain = target + alpha * (reference_gain - target);
        reference_limiter = 1.0 + (reference_limiter - 1.0) * exp(-log(9.0)/(16000 * .020));
        if (fabs(x) * reference_gain * reference_limiter > ceiling) {
            reference_limiter = ceiling / (fabs(x) * reference_gain);
        }
        output[i] = x * reference_gain * reference_limiter;
    }
}

static void compare(deepfir_input_conditioner_t *state, const int32_t *input) {
    float output[16];
    double expected[16];
    reference(input, expected);
    assert(deepfir_input_conditioner_process(state, input, 16, output) == 0);
    for (unsigned i = 0; i < 16; ++i) {
        const double error = fabs(output[i] - expected[i]);
        if (error > max_error) {
            max_error = error;
        }
        assert(isfinite(output[i]));
        assert(fabs(output[i]) <= pow(10.0, -1.0 / 20.0) + 2e-7);
        assert(error <= 3e-5);
        uint32_t bits;
        memcpy(&bits, output + i, sizeof(bits));
        stream_hash = (stream_hash ^ bits) * UINT64_C(1099511628211);
    }
}

static void numeric_stream(void) {
    deepfir_input_conditioner_t state;
    int32_t input[16] = {0};
    deepfir_input_conditioner_reset(&state);
    reference_gain = 1.0;
    reference_limiter = 1.0;
    reference_peak = 0.0;
    compare(&state, input);
    assert(state.gain == 1.0f);
    for (unsigned i = 0; i < 16; ++i) {
        input[i] = (int32_t)i - 8; // PCM24 low bits survive the preamp.
    }
    compare(&state, input);
    assert(state.gain == 1.0f);
    for (unsigned hop = 0; hop < 1000; ++hop) {
        for (unsigned i = 0; i < 16; ++i) {
            input[i] = (i & 1) ? 8388607 : -8388608;
        }
        compare(&state, input);
    }
    assert(state.limited_samples > 0 && state.compressed_samples > 0);
    const float reduced = state.gain;
    memset(input, 0, sizeof(input));
    for (unsigned hop = 0; hop < 800; ++hop) {
        compare(&state, input);
    }
    assert(state.gain > reduced && state.gain > 0.999f);
    // Sweep steady amplitudes across both knee boundaries and strong compression.
    for (int db = -70; db <= 0; ++db) {
        for (unsigned i = 0; i < 16; ++i) {
            input[i] = (int32_t)(8388607.0 * pow(10.0, db / 20.0));
        }
        for (unsigned hop = 0; hop < 100; ++hop) {
            compare(&state, input);
        }
    }
    uint32_t random = 123;
    for (unsigned hop = 0; hop < 20000; ++hop) {
        for (unsigned i = 0; i < 16; ++i) {
            random = random * 1664525u + 1013904223u;
            input[i] = ((int32_t)(random >> 8) - 8388608) / (int32_t)(1u << (hop % 12));
        }
        compare(&state, input);
    }
    deepfir_input_conditioner_reset(&state);
    assert(state.gain == 1 && state.processed_samples == 0);
    reference_gain = 1;
    reference_limiter = 1;
    reference_peak = 0;
    compare(&state, input);
    printf("double oracle: max absolute error %.9g; 462448 streaming samples; hash %016llx\n",
           max_error, (unsigned long long)stream_hash);
}

static void analytic_behavior(void) {
    deepfir_input_conditioner_t state;
    int32_t input[16];
    float output[16];
    // Published static curve at knee center and beyond the knee (virtual input dBFS).
    const double levels[] = {-30, -15, -12, -9, -6, 0, 6, 18};
    const double expected_db[] = {-30, -15, -12.5, -11, -10, -8, -6, -2};
    for (unsigned test = 0; test < sizeof(levels) / sizeof(levels[0]); ++test) {
        deepfir_input_conditioner_reset(&state);
        for (unsigned i = 0; i < 16; ++i) {
            input[i] = (int32_t)(8388607.0 * pow(10.0, (levels[test] - 21.0) / 20.0));
        }
        for (unsigned hop = 0; hop < 100; ++hop) {
            assert(!deepfir_input_conditioner_process(&state, input, 16, output));
        }
        assert(fabs(20.0 * log10(fabs(output[15])) - expected_db[test]) < 0.001);
    }
    // At knee center, three hops (3 ms) leave 1/9 of the gain step.
    deepfir_input_conditioner_reset(&state);
    for (unsigned i = 0; i < 16; ++i) {
        input[i] = (int32_t)(8388608.0 * pow(10.0, -33.0 / 20.0));
    }
    for (unsigned hop = 0; hop < 3; ++hop) {
        assert(!deepfir_input_conditioner_process(&state, input, 16, output));
    }
    const double target = pow(10.0, -0.5 / 20.0);
    assert(fabs(state.gain - (target + (1.0 - target) / 9.0)) < 2e-6);
    const double start_gain = state.gain;
    memset(input, 0, sizeof(input));
    state.detector_peak = 0; // Isolate release from detector decay.
    for (unsigned hop = 0; hop < 120; ++hop) {
        assert(!deepfir_input_conditioner_process(&state, input, 16, output));
    }
    assert(fabs(state.gain - (1.0 + (start_gain - 1.0) / 9.0)) < 2e-5);
    // Detector retains 1/9 after 20 silent hops, independently of gain dynamics.
    deepfir_input_conditioner_reset(&state);
    state.detector_peak = 1;
    for (unsigned hop = 0; hop < 20; ++hop) {
        assert(!deepfir_input_conditioner_process(&state, input, 16, output));
    }
    assert(fabs(state.detector_peak - 1.0/9.0) < 2e-7);
    // Constant knee-center input cannot jump faster than its smoothed gain step.
    deepfir_input_conditioner_reset(&state);
    for (unsigned i = 0; i < 16; ++i) {
        input[i] = (int32_t)(8388608 * pow(10, -33.0/20));
    }
    const double amplitude = input[0] / 8388608.0 * pow(10, 21.0/20);
    double previous = amplitude;
    for (unsigned hop = 0; hop < 30; ++hop) {
        assert(!deepfir_input_conditioner_process(&state, input, 16, output));
        assert(state.limiter_gain == 1);
        for (unsigned i = 0; i < 16; ++i) {
            assert(output[i] <= previous + 1e-7);
            assert(previous - output[i] <= amplitude * (1-exp(-log(9)/48)) + 1e-7);
            previous = output[i];
        }
    }
    // Equal magnitudes produce exactly opposite samples and identical state.
    deepfir_input_conditioner_t positive, negative;
    deepfir_input_conditioner_reset(&positive);
    deepfir_input_conditioner_reset(&negative);
    float negative_output[16];
    for (unsigned i = 0; i < 16; ++i) {
        input[i] = 8388607;
    }
    assert(!deepfir_input_conditioner_process(&positive, input, 16, output));
    for (unsigned i = 0; i < 16; ++i) {
        input[i] = -input[i];
    }
    assert(!deepfir_input_conditioner_process(&negative, input, 16, negative_output));
    for (unsigned i = 0; i < 16; ++i) {
        assert(output[i] == -negative_output[i]);
    }
    assert(!memcmp(&positive, &negative, sizeof(positive)));
    // A single full-scale transient at every lane is guarded immediately.
    for (unsigned lane = 0; lane < 16; ++lane) {
        for (int sign = -1; sign <= 1; sign += 2) {
            deepfir_input_conditioner_reset(&state);
            memset(input, 0, sizeof(input));
            input[lane] = sign < 0 ? -8388608 : 8388607;
            assert(!deepfir_input_conditioner_process(&state, input, 16, output));
            assert(state.limited_samples == 1);
            for (unsigned i = 0; i < 16; ++i) {
                assert(i == lane ? fabs(output[i]) <= pow(10.0, -1.0 / 20.0) + 2e-7
                                 : output[i] == 0);
            }
        }
    }
}

static void statistics_and_lifecycle(void) {
    deepfir_input_conditioner_t state, saved;
    int32_t input[16];
    float output[16];
    deepfir_input_conditioner_reset(&state);
    // Known quiet level: no compressor/limiter action, independent RMS identity.
    for (unsigned i = 0; i < 16; ++i) {
        input[i] = (i & 1) ? 4096 : -4096;
    }
    for (unsigned hop = 0; hop < 99; ++hop) {
        assert(!deepfir_input_conditioner_process(&state, input, 16, output));
    }
    assert(state.input_rms == 0 && state.output_rms == 0 && state.rms_window_samples == 1584);
    assert(!deepfir_input_conditioner_process(&state, input, 16, output));
    assert(fabs(state.input_rms - 4096.0 / 8388608) < 1e-9);
    assert(fabs(state.output_rms - 4096.0 / 8388608 * pow(10, 21.0/20)) < 1e-8);
    assert(state.rms_window_samples == 0 && state.compressed_samples == 0);
    state.processed_samples = UINT64_C(4294967290);
    state.compressed_samples = UINT64_C(4294967290);
    state.limited_samples = UINT64_C(4294967295);
    input[0] = 8388607;
    assert(!deepfir_input_conditioner_process(&state, input, 16, output));
    assert(state.processed_samples == UINT64_C(4294967306));
    assert(state.compressed_samples > UINT32_MAX && state.limited_samples > UINT32_MAX);
    assert(state.min_gain < 1 && state.input_peak > .99f);
    saved = state;
    deepfir_input_conditioner_reset_stream(&state);
    assert(state.processed_samples == saved.processed_samples);
    assert(state.compressed_samples == saved.compressed_samples && state.limited_samples == saved.limited_samples);
    assert(state.input_peak == saved.input_peak && state.output_peak == saved.output_peak);
    assert(state.input_rms == saved.input_rms && state.output_rms == saved.output_rms);
    assert(state.min_gain == saved.min_gain);
    assert(state.gain == 1 && state.compressor_gain == 1 && state.limiter_gain == 1);
    assert(state.detector_peak == 0 && state.rms_window_samples == 0);
    assert(state.input_square_sum == 0 && state.output_square_sum == 0);
    // A partial pre-reset window must not leak into the next complete window.
    memset(input, 0, sizeof(input));
    for (unsigned hop = 0; hop < 99; ++hop) {
        assert(!deepfir_input_conditioner_process(&state, input, 16, output));
    }
    assert(state.input_rms == saved.input_rms);
    assert(!deepfir_input_conditioner_process(&state, input, 16, output));
    assert(state.input_rms == 0 && state.output_rms == 0);
    // Residual sub-0.1 dB reduction must not inflate meaningful compression counts.
    deepfir_input_conditioner_reset(&state);
    state.compressor_gain = .999982655f;
    state.gain = state.compressor_gain;
    assert(!deepfir_input_conditioner_process(&state, input, 16, output));
    assert(state.gain < 1 && state.compressed_samples == 0);
    // Bracket the 0.1 dB counter boundary (tails release during this hop).
    for (unsigned test = 0; test < 2; ++test) {
        deepfir_input_conditioner_reset(&state);
        state.compressor_gain = test ? .988f : .989f;
        state.gain = state.compressor_gain;
        assert(!deepfir_input_conditioner_process(&state, input, 16, output));
        assert(state.compressed_samples == (test ? 16 : 0));
    }
    // Independent recovery: compressor 120 ms and limiter 20 ms, not one shared gain.
    deepfir_input_conditioner_reset(&state);
    state.compressor_gain = .7f;
    state.limiter_gain = .2f;
    state.gain = .14f;
    assert(!deepfir_input_conditioner_process(&state, input, 16, output));
    assert(fabs(state.compressor_gain - (1-.3*exp(-log(9)/120))) < 2e-6);
    assert(fabs(state.limiter_gain - (1-.8*exp(-log(9)/20))) < 2e-6);
    assert(fabs(state.gain - state.compressor_gain * state.limiter_gain) < 1e-7);
    deepfir_input_conditioner_reset(&state);
    assert(state.processed_samples == 0 && state.min_gain == 1 && state.input_peak == 0);
    assert(state.input_rms == 0 && state.gain == 1);
    deepfir_input_conditioner_reset(NULL);
    deepfir_input_conditioner_reset_stream(NULL);
}

static void invalid_atomic(void) {
    deepfir_input_conditioner_t state, before;
    int32_t input[16] = {0};
    float output[16], saved[16];
    deepfir_input_conditioner_reset(&state);
    before = state;
    memset(output, 0x35, sizeof(output));
    memcpy(saved, output, sizeof(output));
    assert(deepfir_input_conditioner_process(NULL, input, 16, output));
    assert(deepfir_input_conditioner_process(&state, NULL, 16, output));
    assert(deepfir_input_conditioner_process(&state, input, 16, NULL));
    assert(deepfir_input_conditioner_process(&state, input, 15, output));
    assert(deepfir_input_conditioner_process(&state, input, 16, (float *)input));
    assert(deepfir_input_conditioner_process(&state, input, 16, (float *)(input + 1)));
    assert(deepfir_input_conditioner_process(&state, input, 16, (float *)&state));
    assert(deepfir_input_conditioner_process(&state, (const int32_t *)&state, 16, output));
    for (unsigned i = 0; i < 16; ++i) {
        input[i] = 8388608;
        assert(deepfir_input_conditioner_process(&state, input, 16, output));
        input[i] = -8388609;
        assert(deepfir_input_conditioner_process(&state, input, 16, output));
        input[i] = 0;
    }
    assert(!memcmp(&state, &before, sizeof(state)));
    for (unsigned test = 0; test < 7; ++test) {
        deepfir_input_conditioner_reset(&state);
        switch (test) {
            case 0: state.compressor_gain = NAN; break;
            case 1: state.limiter_gain = 0; break;
            case 2: state.detector_peak = INFINITY; break;
            case 3: state.input_square_sum = -1; break;
            case 4: state.output_rms = NAN; break;
            case 5: state.rms_window_samples = 1600; break;
            default: state.min_gain = 2; break;
        }
        memcpy(&before, &state, sizeof(state));
        assert(deepfir_input_conditioner_process(&state, input, 16, output));
        assert(!memcmp(&state, &before, sizeof(state)));
        assert(!memcmp(output, saved, sizeof(output)));
    }
    assert(!memcmp(output, saved, sizeof(output)));
    deepfir_input_conditioner_reset(&state);
    state.gain = 0;
    assert(deepfir_input_conditioner_process(&state, input, 16, output));
    state.gain = 1.01f;
    assert(deepfir_input_conditioner_process(&state, input, 16, output));
    deepfir_input_conditioner_reset(&state);
    state.output_peak = INFINITY;
    assert(deepfir_input_conditioner_process(&state, input, 16, output));
    assert(!memcmp(output, saved, sizeof(output)));
    state.gain = NAN;
    memcpy(&before, &state, sizeof(state));
    assert(deepfir_input_conditioner_process(&state, input, 16, output));
    assert(!memcmp(&state, &before, sizeof(state)));
}

static unsigned feature_calls, infer_calls;
static float expected_pcm[16];
static int fail_feature;

static int feature(const float *pcm, uint32_t count, int16_t *out, void *context) {
    (void)context;
    assert(count == 16 && !memcmp(pcm, expected_pcm, sizeof(expected_pcm)));
    ++feature_calls;
    memset(out, 0, 387 * sizeof(*out));
    return fail_feature ? -93 : 0;
}

static int infer(const deepfir_tensor_map_t *inputs, uint32_t input_count,
                 deepfir_tensor_map_t *outputs, uint32_t output_count, void *context) {
    (void)inputs; (void)context;
    assert(input_count == 3 && output_count == 4);
    ++infer_calls;
    for (unsigned i = 0; i < output_count; ++i) {
        memset(outputs[i].data, 0, outputs[i].elements * sizeof(int16_t));
    }
    return 0;
}

static int fft(float *buffer, uint32_t length, uint8_t inverse, void *context) {
    (void)context;
    assert(length == 128 && inverse == 1);
    memset(buffer, 0, 256 * sizeof(float));
    return 0;
}

static int check_raw_normalization(const float *pcm, uint32_t count, int16_t *out, void *context) {
    const int32_t *raw = context;
    (void)out;
    assert(count == 16);
    for (unsigned i = 0; i < count; ++i) {
        assert(pcm[i] == (float)raw[i] / 8388608.0f);
    }
    return -93; // Stop after observing normalization, before inference/FIR.
}

static void adapter_integration(void) {
    static deepfir_model_adapter_t adapter;
    deepfir_input_conditioner_t state;
    int32_t input[16];
    float output[16];
    const float in_scales[3] = {0.001f, 0.001f, 0.001f};
    const float out_scales[4] = {0.001f, 0.001f, 0.001f, 0.001f};
    const int32_t in_zero[3] = {0}, out_zero[4] = {0};
    assert(deepfir_model_adapter_set_qparams(&adapter, in_scales, in_zero,
                                            out_scales, out_zero) == 0);
    adapter.feature = feature;
    adapter.infer_mapped = infer;
    adapter.fft = fft;
    deepfir_model_adapter_reset(&adapter);
    deepfir_input_conditioner_reset(&state);
    for (unsigned hop = 0; hop < 100; ++hop) {
        for (unsigned i = 0; i < 16; ++i) {
            input[i] = (int32_t)(i * 50000) - 400000;
        }
        assert(deepfir_input_conditioner_process(&state, input, 16, expected_pcm) == 0);
        assert(deepfir_model_adapter_process_float(&adapter, expected_pcm, 16, output) == 0);
        // FIR history retains exactly the same conditioned samples as the frontend.
        for (unsigned i = 0; i < 16; ++i) {
            assert(adapter.dsp.history[i] == expected_pcm[15 - i]);
        }
    }
    assert(feature_calls == 100 && infer_calls == 100);
    for (unsigned i = 0; i < 16; ++i) {
        const float value = expected_pcm[i];
        expected_pcm[i] = NAN;
        assert(deepfir_model_adapter_process_float(&adapter, expected_pcm, 16, output));
        expected_pcm[i] = value;
    }
    assert(deepfir_model_adapter_process_float(&adapter, expected_pcm, 15, output));
    assert(deepfir_model_adapter_process_float(&adapter, expected_pcm, 16, expected_pcm));
    assert(deepfir_model_adapter_process_float(&adapter, expected_pcm, 16, adapter.dsp.history));
    assert(deepfir_model_adapter_process_float(&adapter, adapter.dsp.history, 16, output));
    assert(deepfir_model_adapter_process_float(NULL, expected_pcm, 16, output));
    assert(deepfir_model_adapter_process_float(&adapter, NULL, 16, output));
    assert(deepfir_model_adapter_process_float(&adapter, expected_pcm, 16, NULL));
    assert(feature_calls == 100 && infer_calls == 100 && !adapter.faulted);
    fail_feature = 1;
    assert(deepfir_model_adapter_process_float(&adapter, expected_pcm, 16, output) == -93);
    assert(adapter.faulted && infer_calls == 100);
    fail_feature = 0;
    assert(deepfir_model_adapter_process_float(&adapter, expected_pcm, 16, output));
    deepfir_model_adapter_reset(&adapter);
    assert(deepfir_model_adapter_process_float(&adapter, expected_pcm, 16, output) == 0);
    // Preserve the old helper's exhaustive PCM16-grid check through the real adapter.
    adapter.feature = check_raw_normalization;
    adapter.feature_context = input;
    for (unsigned low = 0; low <= 255; low += 255) {
        for (int32_t base = -32768; base <= 32752; base += 16) {
            for (unsigned i = 0; i < 16; ++i) {
                input[i] = (base + (int32_t)i) * 256 + (int32_t)low;
            }
            deepfir_model_adapter_reset(&adapter);
            assert(deepfir_model_adapter_process_pcm24(&adapter, input, 16, output) == -93);
            assert(adapter.faulted);
        }
    }
    puts("raw PCM24 normalization: 131072 PCM16-grid/low-bit values exact through actual adapter");
}

int main(void) {
    numeric_stream();
    analytic_behavior();
    statistics_and_lifecycle();
    invalid_atomic();
    adapter_integration();
    return 0;
}
