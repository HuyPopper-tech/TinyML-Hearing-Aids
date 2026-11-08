/**
 * @file deepfir_reconstruction.c
 * @brief Hermitian IDFT128, roll64 and taper.
 * @author Bui Minh Huy
 * @date 2026-11-08
 */
#include <math.h>
#include <string.h>
#include "deepfir_reconstruction.h"
#if defined(DEEPFIR_DSP_CMSIS_FFT)
#include "dsp/controller_functions.h"
#endif
#if M55M1_DEEPFIR_MVE && defined(__ARM_FEATURE_MVE) && (__ARM_FEATURE_MVE & 2)
#include <arm_mve.h>
#define DEEPFIR_RECONSTRUCTION_HAS_MVEF 1
#else
#define DEEPFIR_RECONSTRUCTION_HAS_MVEF 0
#endif

static void phase_sin_cos(float angle, float *sine, float *cosine) {
#if defined(DEEPFIR_DSP_CMSIS_FFT)
    // Model phase is radians; CMSIS sin_cos takes degrees and indexes a finite table.
    if (isfinite(angle) && fabsf(angle) <= 3.2f) {
        arm_sin_cos_f32(angle * 57.29577951308232f, sine, cosine);
        return;
    }
#endif
    // Preserve the portable API's large/non-finite-angle behavior outside model range.
    *sine = sinf(angle);
    *cosine = cosf(angle);
}

int deepfir_reconstruct_taps(const int16_t *gain, const int16_t *phase, const float *scales,
                             const int32_t *zero_points, deepfir_fft_callback fft,
                             void *fft_context, float taps[128]) {
    // Positive bins plus conjugate completion initialize every complex element.
    float spectrum[256];
    float rendered[128];
    if (!gain || !phase || !scales || !zero_points || !fft || !taps || !isfinite(scales[0]) ||
        !isfinite(scales[1]) || scales[0] <= 0 || scales[1] <= 0) {
        return -1;
    }
    for (unsigned k = 0; k <= 64; ++k) {
        const float amplitude = ((int32_t)gain[k] - zero_points[0]) * scales[0];
        const float angle = ((int32_t)phase[k] - zero_points[1]) * scales[1];
        float sine, cosine;
        phase_sin_cos(angle, &sine, &cosine);
        const float re = amplitude * cosine;
        // Real endpoints are retained; only DC/Nyquist imaginary components must vanish.
        const float im = k == 0 || k == 64 ? 0.0f : amplitude * sine;
        spectrum[2 * k] = re;
        spectrum[2 * k + 1] = im;
        // Conjugate completion makes the normalized positive-exponent inverse real.
        if (k > 0 && k < 64) {
            spectrum[2 * (128 - k)] = re;
            spectrum[2 * (128 - k) + 1] = -im;
        }
    }
    if (fft(spectrum, 128, 1, fft_context) != 0) {
        return -2;
    }
#if DEEPFIR_RECONSTRUCTION_HAS_MVEF
    static const float lane_offsets[4] = {0, 1, 2, 3};
    for (unsigned n = 0; n < 128; n += 4) {
        const float32x4x2_t bins = vld2q_f32(spectrum + 2u * n);
        const uint32x4_t re_bits =
            vandq_u32(vreinterpretq_u32_f32(bins.val[0]), vdupq_n_u32(0x7fffffffU));
        const uint32x4_t im_bits =
            vandq_u32(vreinterpretq_u32_f32(bins.val[1]), vdupq_n_u32(0x7fffffffU));
        if (vcmphiq_n_u32(re_bits, 0x7f7fffffU) | vcmphiq_n_u32(im_bits, 0x7f7fffffU)) {
            return -3;
        }
        const unsigned shifted = (n + 64u) & 127u;
        const float32x4_t position = vaddq_n_f32(vld1q_f32(lane_offsets), (float)shifted);
        const float32x4_t distance = vabsq_f32(vsubq_n_f32(position, 64.0f));
        // Integer positions and powers of two keep the contract's taper exactly representable.
        const float32x4_t taper = vaddq_n_f32(
            vmulq_n_f32(vsubq_f32(vdupq_n_f32(1), vmulq_n_f32(distance, 1.0f / 64.0f)), 0.5f),
            0.5f);
        vst1q_f32(rendered + shifted, vmulq_f32(bins.val[0], taper));
    }
#else
    for (unsigned n = 0; n < 128; ++n) {
        // Roll +64 before applying the taper at its destination index.
        const unsigned shifted = (n + 64) & 127;
        const float taper = .5f + .5f * (1.0f - fabsf((float)shifted - 64.0f) / 64.0f);
        if (!isfinite(spectrum[2 * n]) || !isfinite(spectrum[2 * n + 1])) {
            return -3;
        }
        rendered[shifted] = spectrum[2 * n] * taper;
    }
#endif
    // Publish only a complete finite filter; failures leave caller taps untouched.
    memcpy(taps, rendered, sizeof(rendered));
    return 0;
}
