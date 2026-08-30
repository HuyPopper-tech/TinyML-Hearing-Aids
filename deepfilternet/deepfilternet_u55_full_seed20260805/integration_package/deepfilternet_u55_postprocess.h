#pragma once
#include <stdint.h>
void dfn_apply_postprocess(const float *spectrum_257x2, const float *gain_257, const float *coeff_5x144x2, float alpha, float *history_5x144x2, float *output_257x2);
