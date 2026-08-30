#include "deepfilternet_u55_postprocess.h"
#include "dsp_constants.h"
#include <string.h>

/* Scalar golden reference. Replace only this inner loop with Helium after golden parity passes. */
void dfn_apply_postprocess(const float *x, const float *gain, const float *coeff, float alpha, float *history, float *y) {
    for (int k = 0; k < DFN_FFT_BINS; ++k) { y[2*k] = x[2*k] * gain[k]; y[2*k+1] = x[2*k+1] * gain[k]; }
    for (int tap = DFN_DF_TAPS - 1; tap > 0; --tap) memcpy(&history[2*tap*DFN_DF_BINS], &history[2*(tap-1)*DFN_DF_BINS], 2*DFN_DF_BINS*sizeof(float));
    memcpy(history, y, 2*DFN_DF_BINS*sizeof(float));
    for (int k = 0; k < DFN_DF_BINS; ++k) {
        float fr = 0.0f, fi = 0.0f;
        for (int tap = 0; tap < DFN_DF_TAPS; ++tap) { const float *c = &coeff[2*(tap*DFN_DF_BINS+k)], *h = &history[2*(tap*DFN_DF_BINS+k)]; fr += c[0]*h[0] - c[1]*h[1]; fi += c[0]*h[1] + c[1]*h[0]; }
        y[2*k] = (1.0f-alpha)*y[2*k] + alpha*fr; y[2*k+1] = (1.0f-alpha)*y[2*k+1] + alpha*fi;
    }
}
