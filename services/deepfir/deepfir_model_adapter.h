/**
 * @file deepfir_model_adapter.h
 * @brief R2 streaming controller.
 * @author Bui Minh Huy
 * @date 2026-10-23
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "deepfir_dsp.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DEEPFIR_MODEL_INPUT_COUNT 3u
#define DEEPFIR_MODEL_OUTPUT_COUNT 4u
#define DEEPFIR_MODEL_FEATURES 387u
#define DEEPFIR_MODEL_CONTEXT 774u
#define DEEPFIR_MODEL_STATE_UNITS 200u
#define DEEPFIR_MODEL_TAPS 128u

typedef enum {
    DEEPFIR_TENSOR_FEATURE = 0,
    DEEPFIR_TENSOR_H,
    DEEPFIR_TENSOR_C,
    DEEPFIR_TENSOR_GAIN,
    DEEPFIR_TENSOR_PHASE,
    DEEPFIR_TENSOR_H_NEXT,
    DEEPFIR_TENSOR_C_NEXT,
} deepfir_tensor_semantic_t;

/** Static semantic map: no tensor discovery or allocation is performed on the
 * audio path.  The map records the exact signed-int16 buffer and qparams. */
typedef struct {
    deepfir_tensor_semantic_t semantic;
    int16_t *data;
    uint32_t elements;
    float scale;
    int32_t zero_point;
} deepfir_tensor_map_t;

typedef int (*deepfir_mapped_infer_callback)(const deepfir_tensor_map_t *inputs,
                                             uint32_t input_count, deepfir_tensor_map_t *outputs,
                                             uint32_t output_count, void *context);
// Borrow normalized PCM for this call only; the same immutable hop feeds the FIR.
typedef int (*deepfir_feature_callback)(const float *pcm, uint32_t sample_count, int16_t *feature,
                                        void *context);

#if M55M1_RUNTIME_TELEMETRY || M55M1_AUDIO_RELEASE_PROBE
typedef enum {
    DEEPFIR_PROBE_BEFORE_INFER = 0,
    DEEPFIR_PROBE_AFTER_INFER,
    DEEPFIR_PROBE_AFTER_CARRY,
    DEEPFIR_PROBE_AFTER_TAPS,
    DEEPFIR_PROBE_AFTER_FIR,
    DEEPFIR_PROBE_STAGE_COUNT
} deepfir_probe_stage_t;
typedef void (*deepfir_probe_mark_callback)(uint32_t stage, void *context);
#endif

typedef struct {
    // Foreground owner: configuration is frozen before DMA; reset only with audio stopped.
    deepfir_dsp_state_t dsp;
    uint8_t qparams_ready;
    int16_t h[DEEPFIR_MODEL_STATE_UNITS];
    int16_t c[DEEPFIR_MODEL_STATE_UNITS];
    int16_t next_h[DEEPFIR_MODEL_STATE_UNITS];
    int16_t next_c[DEEPFIR_MODEL_STATE_UNITS];
    // Frontend writes current directly; previous commits only after successful invoke.
    int16_t feature_context_buffer[DEEPFIR_MODEL_CONTEXT];
    int64_t feedback_multiplier[2];
    unsigned feedback_shift[2];
    int feedback_direct[2];
    int16_t gain_buffer[65];
    int16_t phase_buffer[65];
    // A failed hop is unusable until the stream owner performs a full reset.
    uint8_t faulted;
    deepfir_fft_callback fft;
    void *fft_context;
    deepfir_tensor_map_t input_map[DEEPFIR_MODEL_INPUT_COUNT];
    deepfir_tensor_map_t output_map[DEEPFIR_MODEL_OUTPUT_COUNT];
    float input_scales[DEEPFIR_MODEL_INPUT_COUNT];
    int32_t input_zero_points[DEEPFIR_MODEL_INPUT_COUNT];
    float output_scales[DEEPFIR_MODEL_OUTPUT_COUNT];
    int32_t output_zero_points[DEEPFIR_MODEL_OUTPUT_COUNT];
    deepfir_mapped_infer_callback infer_mapped;
    deepfir_feature_callback feature;
    void *infer_context;
    void *feature_context;
#if M55M1_RUNTIME_TELEMETRY || M55M1_AUDIO_RELEASE_PROBE
    // Foreground-only cycle marks; absent from the silent Release build.
    deepfir_probe_mark_callback probe_mark;
    void *probe_context;
#endif
} deepfir_model_adapter_t;

/** Reset context, recurrent coordinates and FIR while transport is stopped. */
void deepfir_model_adapter_reset(deepfir_model_adapter_t *adapter);
/**
 * @brief Bind semantic views to adapter-owned fixed buffers.
 * @param adapter Foreground context; ignored when NULL.
 * @note Maps use feature/h/c and gain/phase/h_next/c_next semantic order, not port order.
 */
void deepfir_model_adapter_init_maps(deepfir_model_adapter_t *adapter);
/** Startup-only semantic order: feature/h/c -> gain/phase/h_next/c_next. */
int deepfir_model_adapter_set_qparams(deepfir_model_adapter_t *adapter,
                                      const float input_scales[DEEPFIR_MODEL_INPUT_COUNT],
                                      const int32_t input_zero_points[DEEPFIR_MODEL_INPUT_COUNT],
                                      const float output_scales[DEEPFIR_MODEL_OUTPUT_COUNT],
                                      const int32_t output_zero_points[DEEPFIR_MODEL_OUTPUT_COUNT]);
/** One foreground hop: feature[387], normalized PCM[16], output[16]. */
int deepfir_model_adapter_process_hop(deepfir_model_adapter_t *adapter, const int16_t *feature,
                                      const float *pcm, uint32_t sample_count, float *output);
/**
 * @brief Run frontend, inference, state feedback, reconstruction and FIR for one hop.
 * @param adapter Initialized, non-faulted foreground context; never invoke concurrently.
 * @param pcm16 Separate caller-owned mono PCM16 input.
 * @param sample_count Must equal 16.
 * @param output Separate 16-float normalized destination; untouched on failure.
 * @return Zero on success; a negative status or callback failure otherwise.
 * @note Any processing failure latches the context until a synchronized full reset.
 */
int deepfir_model_adapter_process_pcm16(deepfir_model_adapter_t *adapter, const int16_t *pcm16,
                                        uint32_t sample_count, float *output);
/** Same lifecycle as PCM16; input is 16 sign-extended PCM24 codes in
 * [-8388608, 8388607]. Normalization by 2^23 feeds both frontend and FIR.
 * Invalid range/overlap returns -1 before callbacks or output writes. */
int deepfir_model_adapter_process_pcm24(deepfir_model_adapter_t *adapter, const int32_t *pcm24,
                                        uint32_t sample_count, float *output);
/** Same lifecycle as PCM16; borrow a finite normalized float hop for both
 * frontend and FIR. Separate input/output must not overlap adapter storage.
 * Invalid input returns -1 before callbacks, state changes or output writes. */
int deepfir_model_adapter_process_float(deepfir_model_adapter_t *adapter, const float *pcm,
                                        uint32_t sample_count, float *output);
/**
 * @brief Requantize encoded recurrent state without changing channel coordinates.
 * @param code Signed INT16 model output code.
 * @param multiplier Nonnegative Q31 multiplier, at most 2147483648.
 * @param shift Right-shift denominator exponent, at most 46.
 * @param source_zero Output quantizer zero point in the INT16 range.
 * @param target_zero Next-input quantizer zero point in the INT16 range.
 * @return Saturated INT16 code with ties-away rounding, or zero for invalid parameters.
 */
int16_t deepfir_state_feedback(int16_t code, int64_t multiplier, unsigned shift,
                               int32_t source_zero, int32_t target_zero);

/** Foreground after successful invoke only; shared by audio and compiled replay. */
void deepfir_model_adapter_commit_state(deepfir_model_adapter_t *adapter);

/**
 * @brief Verify the active four-lane Q31 carry kernel on every INT16 code.
 * @param adapter Initialized, immutable qparams; adapter state is never modified.
 * @return True only when MVE is compiled and both zero-centered shift30/31 lanes match.
 * @note Startup-only before audio; scratch is local and no memory is allocated.
 */
bool deepfir_model_adapter_prepare_mve(const deepfir_model_adapter_t *adapter);

#ifdef __cplusplus
}
#endif
