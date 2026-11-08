/**
 * @file deepfir_cmsis_fft_adapter.h
 * @brief Caller-owned CMSIS-DSP FFT contexts for the R2 streaming service.
 * @author Bui Minh Huy
 * @date 2026-11-08
 */
#pragma once
#include "deepfir_dsp.h"
#include "dsp/transform_functions.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    arm_cfft_instance_f32 instance;
    arm_cfft_instance_f32 inverse128;
} deepfir_cmsis_fft_context_t;
/**
 * @brief Prepare FFT256 and inverse128 descriptors before audio starts.
 * @param state Caller-owned context retained for the complete streaming lifecycle.
 * @return Zero on success; nonzero for invalid context or CMSIS init failure.
 */
int deepfir_cmsis_fft_init(deepfir_cmsis_fft_context_t *state);
/**
 * @brief Transform interleaved complex floats in natural bin order.
 * @param buffer Caller-owned 2*length floats, updated in place.
 * @param length 256 for forward or 128 for inverse.
 * @param inverse Zero for unnormalized forward, one for normalized positive-exponent inverse.
 * @param context Initialized deepfir_cmsis_fft_context_t.
 * @return Zero on success; -1 for invalid parameters.
 */
int deepfir_cmsis_fft_callback(float *buffer, uint32_t length, uint8_t inverse, void *context);

#ifdef __cplusplus
}
#endif
