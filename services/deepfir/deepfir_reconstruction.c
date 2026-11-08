/**
 * @file deepfir_reconstruction.c
 * @brief Hermitian IDFT128, roll64 and taper.
 * @author Bui Minh Huy
 * @date 2026-11-08
 */
#include <math.h>
#include <string.h>
#include "deepfir_reconstruction.h"

int deepfir_reconstruct_taps(const int16_t *gain, const int16_t *phase, const float *scales,
                             const int32_t *zero_points, deepfir_fft_callback fft,
                             void *fft_context, float taps[128]) {
    float spectrum[256] = {0};
    float rendered[128];
    if (!gain || !phase || !scales || !zero_points || !fft || !taps || !isfinite(scales[0]) ||
        !isfinite(scales[1]) || scales[0] <= 0 || scales[1] <= 0) {
        return -1;
    }
    for (unsigned k = 0; k <= 64; ++k) {
        const float amplitude = ((int32_t)gain[k] - zero_points[0]) * scales[0];
        const float angle = ((int32_t)phase[k] - zero_points[1]) * scales[1];
        const float re = amplitude * cosf(angle);
        // Real endpoints are retained; only DC/Nyquist imaginary components must vanish.
        const float im = k == 0 || k == 64 ? 0.0f : amplitude * sinf(angle);
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
