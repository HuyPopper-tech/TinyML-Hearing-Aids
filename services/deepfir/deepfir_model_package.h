/**
 * @file deepfir_model_package.h
 * @brief Project-owned DeepFIR model bytes and quantization contract bridge.
 * @author Bui Minh Huy
 * @date 2026-09-23
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Borrow the immutable compiled model image.
 * @return Static read-only bytes retained for the interpreter lifecycle.
 */
const unsigned char *deepfir_model_package_data(void);
/**
 * @brief Return the compiled image length.
 * @return Model size in bytes.
 */
unsigned int deepfir_model_package_size(void);
/**
 * @brief Borrow input scales in feature/h/c semantic order.
 * @return Static read-only array of three scales.
 */
const float *deepfir_model_input_scales(void);
/**
 * @brief Borrow input zero points in feature/h/c semantic order.
 * @return Static read-only array of three zero points.
 */
const int32_t *deepfir_model_input_zero_points(void);
/**
 * @brief Borrow output scales in gain/phase/h_next/c_next semantic order.
 * @return Static read-only array of four scales.
 */
const float *deepfir_model_output_scales(void);
/**
 * @brief Borrow output zero points in gain/phase/h_next/c_next semantic order.
 * @return Static read-only array of four zero points.
 */
const int32_t *deepfir_model_output_zero_points(void);

#ifdef __cplusplus
}
#endif
