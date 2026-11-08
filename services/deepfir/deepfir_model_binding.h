/**
 * @file deepfir_model_binding.h
 * @brief R2 immutable binding glue.
 * @author Bui Minh Huy
 * @date 2026-10-23
 */
#pragma once
#include "services/deepfir/deepfir_model_adapter.h"
#include "models/deepfir/deepfir_model_qparams.h"
#ifdef __cplusplus
extern "C" {
#endif
static inline int deepfir_model_init_qparams(deepfir_model_adapter_t *adapter) {
    int status = deepfir_model_adapter_set_qparams(adapter, deepfir_input_scale, deepfir_input_zero,
                                                   deepfir_output_scale, deepfir_output_zero);
    if (status == 0) {
        for (unsigned i = 0; i < 2; ++i) {
            adapter->feedback_multiplier[i] = DF_FEEDBACK_MULTIPLIER[i];
            adapter->feedback_shift[i] = DF_FEEDBACK_SHIFT[i];
            adapter->feedback_direct[i] = DF_DIRECT_COPY[i];
        }
    }
    return status;
}
#ifdef __cplusplus
}
#endif
