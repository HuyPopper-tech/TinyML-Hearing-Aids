/**
 * @file arm_math.h
 * @brief Link stubs for native kernel tests; never used as CMSIS accuracy evidence.
 * @author Bui Minh Huy
 * @date 2026-11-08
 */
#pragma once
#include <math.h>
#include <stdint.h>
typedef int16_t q15_t;
static inline void arm_q15_to_float(const q15_t *p, float *r, uint32_t n) {
    for (uint32_t i = 0; i < n; ++i) {
        r[i] = p[i] / 32768.0f;
    }
}
static inline void arm_vlog_f32(const float *p, float *r, uint32_t n) {
    for (uint32_t i = 0; i < n; ++i) {
        r[i] = logf(p[i]);
    }
}
static inline void arm_vexp_f32(const float *p, float *r, uint32_t n) {
    for (uint32_t i = 0; i < n; ++i) {
        r[i] = expf(p[i]);
    }
}
static inline void arm_scale_f32(const float *p, float scale, float *r, uint32_t n) {
    for (uint32_t i = 0; i < n; ++i) {
        r[i] = p[i] * scale;
    }
}
