/**
 * @file deepfir_reconstruction.h
 * @brief Gain/phase FIR reconstruction.
 * @author Bui Minh Huy
 * @date 2026-11-08
 */
#pragma once
#include "deepfir_dsp.h"
#ifdef __cplusplus
extern "C" {
#endif
/**
 * @brief Reconstruct the current FIR128 from gain65 and phase65 controller codes.
 * @param gain Caller-owned 65 signed INT16 gain codes.
 * @param phase Caller-owned 65 signed INT16 phase codes; dequantized angles are radians.
 * @param scales Semantic output scales; entries 0/1 are gain/phase.
 * @param zero_points Semantic output zero points; entries 0/1 are gain/phase.
 * @param fft Normalized positive-exponent inverse128 callback.
 * @param fft_context Caller-owned callback state retained for this call.
 * @param taps Separate caller-owned 128-float destination, untouched on failure.
 * @return Zero on success; -1 for invalid parameters, -2 for FFT failure, -3 for non-finite taps.
 * @note Foreground-only; Hermitian completion, roll +64 and taper follow DSP_CONTRACT.md.
 */
int deepfir_reconstruct_taps(const int16_t *gain, const int16_t *phase, const float *scales,
                             const int32_t *zero_points, deepfir_fft_callback fft,
                             void *fft_context, float taps[128]);
#ifdef __cplusplus
}
#endif
