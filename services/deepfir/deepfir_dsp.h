/**
 * @file deepfir_dsp.h
 * @brief R2 DSP interface.
 * @author Bui Minh Huy
 * @date 2026-10-23
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DEEPFIR_DSP_TAPS 128u
#define DEEPFIR_DSP_HOP 16u

typedef int (*deepfir_fft_callback)(float *buffer, uint32_t length, uint8_t inverse, void *context);
typedef struct {
    // Foreground audio owner; reset with model context. Oldest sample is at history_position.
    float history[256], window[256];
    uint32_t history_position; // Next 16-sample write; always a multiple of 16 in [0, 240].
    double scale;
    int32_t zero_point;
    deepfir_fft_callback fft;
    void *fft_context;
} deepfir_feature_state_t;
/**
 * @brief Convert normalized audio with ties-away rounding and signed PCM16 saturation.
 * @param value Normalized audio; non-finite values map to zero.
 * @return Signed PCM16 code.
 */
int16_t deepfir_pcm16_from_float(float value);
/**
 * @brief Render 16 normalized samples with the scalar PCM16 rounding contract.
 * @param input Borrowed float hop; must not overlap output.
 * @param output Caller-owned 16-code destination, unchanged on failure.
 * @return 0 on success, -1 for NULL/overlap, -2 for any non-finite input.
 * @note Foreground only; no retained state or partial publication.
 */
int deepfir_pcm16_render_hop(const float *input, int16_t *output);
/**
 * @brief Zero causal analysis history and prepare the symmetric Hamming window.
 * @param state Foreground-owned context; ignored when NULL.
 * @pre Audio is stopped; fft/qparams are configured separately.
 */
void deepfir_feature_reset(deepfir_feature_state_t *state);
/**
 * @brief Extract magnitude^0.3, real and imaginary compressed spectral features.
 * @param pcm Input hop of 16 mono PCM16 samples, normalized internally by 32768.
 * @param count Must equal 16.
 * @param feature Caller-owned 387-code destination in bin/channel order.
 * @param context Caller-owned deepfir_feature_state_t.
 * @return Zero on success; -1 for invalid configuration, -2 for FFT/non-finite failure.
 * @note History may advance on failure; the stream owner must perform a full reset.
 */
int deepfir_feature_pcm16(const int16_t *pcm, uint32_t count, int16_t *feature, void *context);
/**
 * @brief Extract features from an already normalized hop without a second PCM conversion.
 * @param pcm Separate caller-owned 16-float normalized hop, including conditioned PCM24.
 * @param count Must equal 16.
 * @param feature Caller-owned 387-code destination in bin/channel order.
 * @param context Sole foreground-owned deepfir_feature_state_t.
 * @return Zero on success; -1 for invalid configuration, -2 for FFT/non-finite failure.
 * @note Input is borrowed for this call only; a failed hop requires full stream reset.
 */
int deepfir_feature_float(const float *pcm, uint32_t count, int16_t *feature, void *context);
/**
 * @brief Report whether this binary contains the vector frontend.
 * @return True for the compiled MVE floating-point path.
 */
bool deepfir_feature_mve_ready(void);
/** Before DMA: compare all three vector feature channels against scalar math, <= 1 LSB. */
bool deepfir_feature_prepare_mve(deepfir_feature_state_t *state);

typedef struct {
    float history[DEEPFIR_DSP_TAPS];
    float previous_taps[DEEPFIR_DSP_TAPS];
    float current_taps[DEEPFIR_DSP_TAPS];
    float crossfade_blend[DEEPFIR_DSP_HOP];
    uint32_t sample_count;
} deepfir_dsp_state_t;

/**
 * @brief Reset FIR history and the midpoint-Hann fade before a new stream.
 * @param state Foreground-owned context; ignored when NULL.
 * @param initial_taps Optional 128-tap filter; NULL selects a delta at index 64.
 */
void deepfir_dsp_reset(deepfir_dsp_state_t *state, const float *initial_taps);
/**
 * @brief Filter shared input history through old/new FIRs and crossfade their outputs.
 * @param state Sole foreground owner of FIR history, taps and fade phase.
 * @param samples Normalized input samples.
 * @param sample_count Product calls use 16; scalar test streams may use other sizes.
 * @param next_taps 128 taps, or absolute-hop tap rows for scalar calls longer than 16.
 * @param output Separate caller-owned float destination for sample_count samples.
 * @return Zero on success; -1 for invalid pointers or -2 for an empty scalar call.
 */
int deepfir_dsp_process(deepfir_dsp_state_t *state, const float *samples, uint32_t sample_count,
                        const float *next_taps, float *output);
/** True when this image contains and has enabled the Helium FIR kernel. */
bool deepfir_dsp_mve_ready(void);
/** Optional foreground check used by Debug before DMA starts. */
bool deepfir_dsp_prepare_mve(void);

#ifdef __cplusplus
}
#endif
