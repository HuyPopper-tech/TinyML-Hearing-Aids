/**
 * @file deepfir_model_adapter.c
 * @brief R2 context, encoded recurrent carry and FIR adapter.
 * @author Bui Minh Huy
 * @date 2026-11-08
 */
#include <math.h>
#include <string.h>

#include "deepfir_model_adapter.h"
#include "deepfir_reconstruction.h"

#ifndef M55M1_DEEPFIR_MVE
#define M55M1_DEEPFIR_MVE 0
#endif

#if M55M1_DEEPFIR_MVE && defined(__ARM_FEATURE_MVE) && (__ARM_FEATURE_MVE & 2)
#include "arm_math.h"
#define DEEPFIR_ADAPTER_HAS_MVEF 1
#else
#define DEEPFIR_ADAPTER_HAS_MVEF 0
#endif

void deepfir_model_adapter_init_maps(deepfir_model_adapter_t *adapter) {
    uint32_t index;
    if (!adapter) {
        return;
    }
    adapter->input_map[0] = (deepfir_tensor_map_t){
        DEEPFIR_TENSOR_FEATURE, adapter->feature_context_buffer, DEEPFIR_MODEL_CONTEXT,
        adapter->input_scales[0], adapter->input_zero_points[0]};
    adapter->input_map[1] =
        (deepfir_tensor_map_t){DEEPFIR_TENSOR_H, adapter->h, DEEPFIR_MODEL_STATE_UNITS,
                               adapter->input_scales[1], adapter->input_zero_points[1]};
    adapter->input_map[2] =
        (deepfir_tensor_map_t){DEEPFIR_TENSOR_C, adapter->c, DEEPFIR_MODEL_STATE_UNITS,
                               adapter->input_scales[2], adapter->input_zero_points[2]};
    adapter->output_map[0] =
        (deepfir_tensor_map_t){DEEPFIR_TENSOR_GAIN, adapter->gain_buffer, 65,
                               adapter->output_scales[0], adapter->output_zero_points[0]};
    adapter->output_map[1] =
        (deepfir_tensor_map_t){DEEPFIR_TENSOR_PHASE, adapter->phase_buffer, 65,
                               adapter->output_scales[1], adapter->output_zero_points[1]};
    adapter->output_map[2] =
        (deepfir_tensor_map_t){DEEPFIR_TENSOR_H_NEXT, adapter->next_h, DEEPFIR_MODEL_STATE_UNITS,
                               adapter->output_scales[2], adapter->output_zero_points[2]};
    adapter->output_map[3] =
        (deepfir_tensor_map_t){DEEPFIR_TENSOR_C_NEXT, adapter->next_c, DEEPFIR_MODEL_STATE_UNITS,
                               adapter->output_scales[3], adapter->output_zero_points[3]};
    for (index = 0u; index < DEEPFIR_MODEL_INPUT_COUNT; ++index) {
        adapter->input_map[index].scale = adapter->input_scales[index];
        adapter->input_map[index].zero_point = adapter->input_zero_points[index];
    }
    for (index = 0u; index < DEEPFIR_MODEL_OUTPUT_COUNT; ++index) {
        adapter->output_map[index].scale = adapter->output_scales[index];
        adapter->output_map[index].zero_point = adapter->output_zero_points[index];
    }
}

int deepfir_model_adapter_set_qparams(
    deepfir_model_adapter_t *adapter, const float input_scales[DEEPFIR_MODEL_INPUT_COUNT],
    const int32_t input_zero_points[DEEPFIR_MODEL_INPUT_COUNT],
    const float output_scales[DEEPFIR_MODEL_OUTPUT_COUNT],
    const int32_t output_zero_points[DEEPFIR_MODEL_OUTPUT_COUNT]) {
    uint32_t index;
    if (!adapter || !input_scales || !input_zero_points || !output_scales || !output_zero_points) {
        return -1;
    }
    adapter->qparams_ready = 0u;
    for (index = 0u; index < DEEPFIR_MODEL_INPUT_COUNT; ++index) {
        if (!(input_scales[index] > 0.0f) || !isfinite(input_scales[index])) {
            return -2;
        }
        if (input_zero_points[index] < -32768 || input_zero_points[index] > 32767) {
            return -2;
        }
        adapter->input_scales[index] = input_scales[index];
        adapter->input_zero_points[index] = input_zero_points[index];
    }
    for (index = 0u; index < DEEPFIR_MODEL_OUTPUT_COUNT; ++index) {
        if (!(output_scales[index] > 0.0f) || !isfinite(output_scales[index])) {
            return -2;
        }
        if (output_zero_points[index] < -32768 || output_zero_points[index] > 32767) {
            return -2;
        }
        adapter->output_scales[index] = output_scales[index];
        adapter->output_zero_points[index] = output_zero_points[index];
    }
    for (index = 0; index < 2u; ++index) {
        int exponent;
        double mantissa =
            frexp((double)output_scales[index + 2u] / input_scales[index + 1u], &exponent);
        if (exponent < -8 || exponent > 8) {
            return -2;
        }
        adapter->feedback_multiplier[index] = (int64_t)floor(mantissa * 2147483648.0 + 0.5);
        adapter->feedback_shift[index] = (unsigned)(31 - exponent);
        adapter->feedback_direct[index] =
            output_scales[index + 2u] == input_scales[index + 1u] &&
            output_zero_points[index + 2u] == input_zero_points[index + 1u];
    }
    deepfir_model_adapter_init_maps(adapter);
    adapter->qparams_ready = 1u;
    return 0;
}

void deepfir_model_adapter_reset(deepfir_model_adapter_t *adapter) {
    uint32_t index;
    if (!adapter) {
        return;
    }
    deepfir_model_adapter_init_maps(adapter);
    for (index = 0u; index < DEEPFIR_MODEL_STATE_UNITS; ++index) {
        adapter->h[index] = (int16_t)adapter->input_map[1].zero_point;
        adapter->c[index] = (int16_t)adapter->input_map[2].zero_point;
        adapter->next_h[index] = (int16_t)adapter->output_zero_points[2];
        adapter->next_c[index] = (int16_t)adapter->output_zero_points[3];
    }
    for (index = 0; index < DEEPFIR_MODEL_CONTEXT; ++index) {
        adapter->feature_context_buffer[index] = (int16_t)adapter->input_zero_points[0];
    }
    deepfir_dsp_reset(&adapter->dsp, 0);
    adapter->faulted = 0;
}

int16_t deepfir_state_feedback(int16_t code, int64_t multiplier, unsigned shift,
                               int32_t source_zero, int32_t target_zero) {
    if (shift > 46 || multiplier < 0 || multiplier > INT64_C(2147483648) || source_zero < -32768 ||
        source_zero > 32767 || target_zero < -32768 || target_zero > 32767) {
        return 0;
    }
    // Round the magnitude then restore the sign so negative ties also round away.
    const int64_t denominator = INT64_C(1) << shift;
    const int64_t value = ((int64_t)code - source_zero) * multiplier;
    // The rounded magnitude is nonnegative; division by 2^shift is exactly a shift.
    int64_t rounded = ((value < 0 ? -value : value) + denominator / 2) >> shift;
    if (value < 0) {
        rounded = -rounded;
    }
    rounded += target_zero;
    return (int16_t)(rounded < -32768 ? -32768 : rounded > 32767 ? 32767 : rounded);
}

static void carry_state(deepfir_model_adapter_t *adapter, unsigned state) {
    const deepfir_tensor_map_t *source = &adapter->output_map[state + 2u];
    const deepfir_tensor_map_t *destination = &adapter->input_map[state + 1u];
    if (adapter->feedback_direct[state]) {
        memcpy(destination->data, source->data, DEEPFIR_MODEL_STATE_UNITS * sizeof(int16_t));
        return;
    }
    for (unsigned i = 0; i < DEEPFIR_MODEL_STATE_UNITS; ++i) {
        destination->data[i] = deepfir_state_feedback(
            source->data[i], adapter->feedback_multiplier[state], adapter->feedback_shift[state],
            source->zero_point, destination->zero_point);
    }
}

#if M55M1_RUNTIME_TELEMETRY || M55M1_AUDIO_RELEASE_PROBE
static void mark_probe(deepfir_model_adapter_t *adapter, uint32_t stage) {
    if (adapter->probe_mark) {
        adapter->probe_mark(stage, adapter->probe_context);
    }
}
#endif

void deepfir_model_adapter_commit_state(deepfir_model_adapter_t *adapter) {
    carry_state(adapter, 0u);
    carry_state(adapter, 1u);
}

int deepfir_model_adapter_process_hop(deepfir_model_adapter_t *adapter, const int16_t *feature,
                                      const float *pcm, uint32_t sample_count, float *output) {
    float tap_float[DEEPFIR_MODEL_TAPS];
    int status;
    if (!adapter || !feature || !pcm || !output || sample_count != DEEPFIR_DSP_HOP) {
        return -1;
    }
    if (!adapter->qparams_ready || !adapter->infer_mapped || !adapter->fft || adapter->faulted) {
        return -2;
    }
    if ((uintptr_t)output < (uintptr_t)pcm + sample_count * sizeof(*pcm) &&
        (uintptr_t)pcm < (uintptr_t)output + sample_count * sizeof(*output)) {
        return -1;
    }
    for (unsigned i = 0; i < sample_count; ++i) {
        if (!isfinite(pcm[i])) {
            adapter->faulted = 1;
            return -3;
        }
    }
    int16_t *current_feature = adapter->feature_context_buffer + DEEPFIR_MODEL_FEATURES;
    if (feature != current_feature) {
        memcpy(current_feature, feature, DEEPFIR_MODEL_FEATURES * sizeof(feature[0]));
    }
#if M55M1_RUNTIME_TELEMETRY || M55M1_AUDIO_RELEASE_PROBE
    mark_probe(adapter, DEEPFIR_PROBE_BEFORE_INFER);
#endif
    status =
        adapter->infer_mapped(adapter->input_map, DEEPFIR_MODEL_INPUT_COUNT, adapter->output_map,
                              DEEPFIR_MODEL_OUTPUT_COUNT, adapter->infer_context);
    if (status != 0) {
        adapter->faulted = 1;
        return status;
    }
    for (unsigned i = 0; i < DEEPFIR_MODEL_OUTPUT_COUNT; ++i) {
        if (!adapter->output_map[i].data) {
            adapter->faulted = 1;
            return -3;
        }
    }
#if M55M1_RUNTIME_TELEMETRY || M55M1_AUDIO_RELEASE_PROBE
    mark_probe(adapter, DEEPFIR_PROBE_AFTER_INFER);
#endif
    // State remains encoded in graph order; apply only the Q31 output-to-input scaling.
    deepfir_model_adapter_commit_state(adapter);
    memcpy(adapter->feature_context_buffer,
           adapter->feature_context_buffer + DEEPFIR_MODEL_FEATURES,
           DEEPFIR_MODEL_FEATURES * sizeof(int16_t));
#if M55M1_RUNTIME_TELEMETRY || M55M1_AUDIO_RELEASE_PROBE
    mark_probe(adapter, DEEPFIR_PROBE_AFTER_CARRY);
#endif
    status = deepfir_reconstruct_taps(adapter->output_map[0].data, adapter->output_map[1].data,
                                      adapter->output_scales, adapter->output_zero_points,
                                      adapter->fft, adapter->fft_context, tap_float);
    if (status != 0) {
        adapter->faulted = 1;
        return status;
    }
#if M55M1_RUNTIME_TELEMETRY || M55M1_AUDIO_RELEASE_PROBE
    mark_probe(adapter, DEEPFIR_PROBE_AFTER_TAPS);
#endif
    // Keep output private until every stage succeeds; a partial context requires full reset.
    float rendered[DEEPFIR_DSP_HOP];
    status = deepfir_dsp_process(&adapter->dsp, pcm, sample_count, tap_float, rendered);
#if M55M1_RUNTIME_TELEMETRY || M55M1_AUDIO_RELEASE_PROBE
    mark_probe(adapter, DEEPFIR_PROBE_AFTER_FIR);
#endif
    if (status != 0) {
        adapter->faulted = 1;
        return status;
    }
    for (unsigned i = 0; i < sample_count; ++i) {
        if (!isfinite(rendered[i])) {
            adapter->faulted = 1;
            return -3;
        }
    }
    memcpy(output, rendered, sizeof(rendered));
    return 0;
}

int deepfir_model_adapter_process_pcm16(deepfir_model_adapter_t *adapter, const int16_t *pcm16,
                                        uint32_t sample_count, float *output) {
    int status;
    if (!adapter || !pcm16 || !output || !adapter->feature || sample_count != DEEPFIR_DSP_HOP) {
        return -1;
    }
    if (adapter->faulted || !adapter->qparams_ready || !adapter->infer_mapped || !adapter->fft) {
        return -2;
    }
    if ((uintptr_t)output < (uintptr_t)pcm16 + sample_count * sizeof(*pcm16) &&
        (uintptr_t)pcm16 < (uintptr_t)output + sample_count * sizeof(*output)) {
        return -1;
    }
    {
        float pcm[DEEPFIR_DSP_HOP];
#if DEEPFIR_ADAPTER_HAS_MVEF
        arm_q15_to_float((const q15_t *)pcm16, pcm, sample_count);
#else
        for (uint32_t index = 0u; index < sample_count; ++index) {
            pcm[index] = (float)pcm16[index] / 32768.0f;
        }
#endif
        int16_t *current_feature = adapter->feature_context_buffer + DEEPFIR_MODEL_FEATURES;
        status = adapter->feature(pcm, sample_count, current_feature, adapter->feature_context);
        if (status != 0) {
            adapter->faulted = 1;
            return status;
        }
        return deepfir_model_adapter_process_hop(adapter, current_feature, pcm, sample_count,
                                                 output);
    }
}
