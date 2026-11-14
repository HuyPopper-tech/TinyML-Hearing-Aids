/**
 * @file audio_gain_harness.c
 * @brief Configurable preamp and streaming dynamics against independent double math.
 * @author Bui Minh Huy
 * @date 2026-11-15
 */
#include <assert.h>
#include <math.h>
#include <stdint.h>

#include "services/deepfir/deepfir_input_conditioner.h"

int main(void) {
    const double preamp = pow(10.0, M55M1_SOFTWARE_GAIN_DB / 20.0);
    const double ceiling = pow(10.0, -1.0 / 20.0);
    deepfir_input_conditioner_t state;
    int32_t input[16];
    float output[16];
    deepfir_input_conditioner_reset(&state);
    for (unsigned i = 0; i < 16; ++i) {
        input[i] = (i & 1) ? 8 : -8;
    }
    assert(!deepfir_input_conditioner_process(&state, input, 16, output));
    for (unsigned i = 0; i < 16; ++i) {
        assert(fabs(output[i] / (input[i] / 8388608.0) - preamp) <= preamp * 1e-7);
    }
    assert(state.gain == 1.0f);
    deepfir_input_conditioner_reset(&state);
    double detector = 0.0, compressor = 1.0, limiter = 1.0;
    uint32_t random = 0x215;
    for (unsigned hop = 0; hop < 2048; ++hop) {
        double peak = 0.0;
        for (unsigned i = 0; i < 16; ++i) {
            random = random * 1664525U + 1013904223U;
            input[i] = hop < 64 ? ((i & 1) ? 8388607 : -8388608)
                               : ((int32_t)(random >> 8) - 8388608) / (1 << (hop % 16));
            peak = fmax(peak, fabs(input[i] / 8388608.0 * preamp));
        }
        detector = fmax(peak, detector * exp(-log(9.0) * 16 / (16000 * .020)));
        double target = 1.0;
        if (detector > pow(10.0, -15.0 / 20.0)) {
            const double over = 20.0 * log10(detector) + 12.0;
            const double reduction = over < 3.0 ? -(over + 3.0) * (over + 3.0) / 18.0
                                                : -(2.0 / 3.0) * over;
            target = pow(10.0, reduction / 20.0);
        }
        assert(!deepfir_input_conditioner_process(&state, input, 16, output));
        for (unsigned i = 0; i < 16; ++i) {
            const double duration = target < compressor ? .003 : .120;
            compressor = target + (compressor - target) * exp(-log(9.0) / (16000 * duration));
            limiter = 1.0 + (limiter - 1.0) * exp(-log(9.0) / (16000 * .020));
            const double compressed = input[i] / 8388608.0 * preamp * compressor;
            if (fabs(compressed) * limiter > ceiling) {
                limiter = ceiling / fabs(compressed);
            }
            assert(isfinite(output[i]));
            assert(fabs(output[i]) <= ceiling + 2e-7);
            assert(fabs(output[i] - compressed * limiter) <= 3e-5);
        }
    }
    assert(state.processed_samples == 2048 * 16);
    return 0;
}
