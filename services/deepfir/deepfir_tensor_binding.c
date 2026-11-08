/**
 * @file deepfir_tensor_binding.c
 * @brief R2 generated-port contract and rejection checks.
 * @author Bui Minh Huy
 * @date 2026-11-08
 */
#include <math.h>
#include <string.h>
#include "deepfir_tensor_binding.h"

static const deepfir_tensor_descriptor_t DEEPFIR_INPUT_DESCRIPTORS[] = {
    {0,
     9,
     "serving_default_feature_context:0",
     4,
     {1, 2, 129, 3},
     1548,
     9.672559826867655e-05f,
     0,
     true},
    {2, 10, "serving_default_h:0", 2, {1, 200, 0, 0}, 400, 3.0516976039507426e-05f, 0, true},
    {1, 8, "serving_default_c:0", 2, {1, 200, 0, 0}, 400, 2.9065247872495092e-05f, 0, true},
};
static const deepfir_tensor_descriptor_t DEEPFIR_OUTPUT_DESCRIPTORS[] = {
    {3, 1, "PartitionedCall:1", 2, {1, 65, 0, 0}, 130, 5.657631845679134e-05f, 0, true},
    {2, 3, "PartitionedCall:3", 2, {1, 65, 0, 0}, 130, 7.72049679653719e-05f, 0, true},
    {0, 2, "PartitionedCall:2", 2, {1, 200, 0, 0}, 400, 3.051537714782171e-05f, 0, true},
    {1, 0, "PartitionedCall:0", 2, {1, 200, 0, 0}, 400, 2.9116219593561254e-05f, 0, true},
};

const deepfir_tensor_descriptor_t *deepfir_tensor_expected(bool input, unsigned semantic) {
    if (semantic >= (input ? 3u : 4u)) {
        return NULL;
    }
    return input ? &DEEPFIR_INPUT_DESCRIPTORS[semantic] : &DEEPFIR_OUTPUT_DESCRIPTORS[semantic];
}

bool deepfir_tensor_validate(bool input, unsigned semantic,
                             const deepfir_tensor_descriptor_t *actual) {
    const deepfir_tensor_descriptor_t *expected = deepfir_tensor_expected(input, semantic);
    if (!expected || !actual || !actual->int16_type || !actual->name ||
        actual->port != expected->port || actual->tensor_index != expected->tensor_index ||
        strcmp(actual->name, expected->name) != 0 || actual->rank != expected->rank ||
        actual->bytes != expected->bytes || !isfinite(actual->scale) ||
        actual->scale != expected->scale || actual->zero_point != expected->zero_point) {
        return false;
    }
    for (unsigned i = 0; i < expected->rank; ++i) {
        if (actual->shape[i] != expected->shape[i]) {
            return false;
        }
    }
    return true;
}
