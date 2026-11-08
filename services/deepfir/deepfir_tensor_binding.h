/**
 * @file deepfir_tensor_binding.h
 * @brief Portable tensor binding validation.
 * @author Bui Minh Huy
 * @date 2026-11-08
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    unsigned port;
    int tensor_index;
    const char *name;
    unsigned rank;
    int shape[4];
    size_t bytes;
    float scale;
    int32_t zero_point;
    bool int16_type;
} deepfir_tensor_descriptor_t;
/** Semantic order feature/h/c or gain/phase/h_next/c_next, not runtime port order. */
const deepfir_tensor_descriptor_t *deepfir_tensor_expected(bool input, unsigned semantic);
/**
 * @brief Require the exact runtime port, tensor index, name, INT16 dtype and qparams.
 * @param input True for an input semantic; false for an output semantic.
 * @param semantic Index in the semantic order documented above.
 * @param actual Runtime descriptor; never retained.
 * @return True only when all metadata, dimensions and byte count match.
 */
bool deepfir_tensor_validate(bool input, unsigned semantic,
                             const deepfir_tensor_descriptor_t *actual);
#ifdef __cplusplus
}
#endif
