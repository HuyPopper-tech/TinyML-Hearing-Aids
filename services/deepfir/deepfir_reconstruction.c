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
    for (unsigned n = 0; n < 128; ++n) {
        // Roll +64 before applying the taper at its destination index.
        const unsigned shifted = (n + 64) & 127;
        const float taper = .5f + .5f * (1.0f - fabsf((float)shifted - 64.0f) / 64.0f);
        if (!isfinite(spectrum[2 * n]) || !isfinite(spectrum[2 * n + 1])) {
            return -3;
        }
        rendered[shifted] = spectrum[2 * n] * taper;
    }
    // Publish only a complete finite filter; failures leave caller taps untouched.
    memcpy(taps, rendered, sizeof(rendered));
    return 0;
}
