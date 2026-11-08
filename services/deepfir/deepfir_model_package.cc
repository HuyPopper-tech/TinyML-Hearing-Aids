/**
 * @file deepfir_model_package.cc
 * @brief Project-owned bridge for the checked-in DeepFIR model package.
 * @author Bui Minh Huy
 * @date 2026-09-23
 */

#include "services/deepfir/deepfir_model_package.h"

#include "deepfir_model_data.h"
#include "deepfir_model_qparams.h"

extern "C" const unsigned char *deepfir_model_package_data(void) {
    return g_deepfir_model;
}

extern "C" unsigned int deepfir_model_package_size(void) {
    return g_deepfir_model_len;
}

extern "C" const float *deepfir_model_input_scales(void) {
    return deepfir_input_scale;
}

extern "C" const int32_t *deepfir_model_input_zero_points(void) {
    return deepfir_input_zero;
}

extern "C" const float *deepfir_model_output_scales(void) {
    return deepfir_output_scale;
}

extern "C" const int32_t *deepfir_model_output_zero_points(void) {
    return deepfir_output_zero;
}
