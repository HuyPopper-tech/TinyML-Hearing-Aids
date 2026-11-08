/**
 * @file deepfir_cmsis_fft_adapter.c
 * @brief CMSIS-DSP forward FFT256 and normalized inverse FFT128 callbacks.
 * @author Bui Minh Huy
 * @date 2026-11-08
 */
#if defined(DEEPFIR_DSP_CMSIS_FFT)
#include "deepfir_cmsis_fft_adapter.h"

int deepfir_cmsis_fft_init(deepfir_cmsis_fft_context_t *state) {
    if (!state || arm_cfft_init_256_f32(&state->instance) != ARM_MATH_SUCCESS) {
        return -1;
    }
    return (int)arm_cfft_init_128_f32(&state->inverse128);
}

int deepfir_cmsis_fft_callback(float *buffer, uint32_t length, uint8_t inverse, void *context) {
    deepfir_cmsis_fft_context_t *state = (deepfir_cmsis_fft_context_t *)context;
    if (!state || !buffer ||
        !((length == 256u && inverse == 0u) || (length == 128u && inverse == 1u))) {
        return -1;
    }
    arm_cfft_f32(length == 256u ? &state->instance : &state->inverse128, buffer, inverse, 1u);
    return 0;
}
#endif
