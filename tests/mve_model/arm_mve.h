/**
 * @file arm_mve.h
 * @brief Native lane-semantics model for testing actual MVE kernel C branches.
 * @author Bui Minh Huy
 * @date 2026-11-08
 * @note Test-only; this neither executes MVE nor models target timing/CMSIS accuracy.
 */
#pragma once
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    float lane[4];
} float32x4_t;
typedef struct {
    int32_t lane[4];
} int32x4_t;
typedef struct {
    uint32_t lane[4];
} uint32x4_t;
typedef struct {
    float32x4_t val[2];
} float32x4x2_t;
typedef uint16_t mve_pred16_t;

#define MVE_MODEL_UNARY(name, result_type, argument_type, expression)                              \
    static inline result_type name(argument_type a) {                                              \
        result_type r;                                                                             \
        for (unsigned i = 0; i < 4; ++i) {                                                         \
            r.lane[i] = (expression);                                                              \
        }                                                                                          \
        return r;                                                                                  \
    }
#define MVE_MODEL_BINARY(name, result_type, first_type, second_type, expression)                   \
    static inline result_type name(first_type a, second_type b) {                                  \
        result_type r;                                                                             \
        for (unsigned i = 0; i < 4; ++i) {                                                         \
            r.lane[i] = (expression);                                                              \
        }                                                                                          \
        return r;                                                                                  \
    }
#define MVE_MODEL_COMPARE(name, first_type, second_type, expression)                               \
    static inline mve_pred16_t name(first_type a, second_type b) {                                 \
        mve_pred16_t r = 0;                                                                        \
        for (unsigned i = 0; i < 4; ++i) {                                                         \
            if (expression) {                                                                      \
                r |= (mve_pred16_t)(15u << (4 * i));                                               \
            }                                                                                      \
        }                                                                                          \
        return r;                                                                                  \
    }

MVE_MODEL_UNARY(vdupq_n_f32, float32x4_t, float, a)
MVE_MODEL_UNARY(vdupq_n_s32, int32x4_t, int32_t, a)
MVE_MODEL_UNARY(vdupq_n_u32, uint32x4_t, uint32_t, a)
MVE_MODEL_UNARY(vld1q_f32, float32x4_t, const float *, a[i])
MVE_MODEL_UNARY(vld1q_s32, int32x4_t, const int32_t *, a[i])
MVE_MODEL_UNARY(vldrhq_s32, int32x4_t, const int16_t *, a[i])
MVE_MODEL_UNARY(vabsq_f32, float32x4_t, float32x4_t, fabsf(a.lane[i]))
MVE_MODEL_UNARY(vabsq_s32, int32x4_t, int32x4_t, a.lane[i] < 0 ? -a.lane[i] : a.lane[i])
MVE_MODEL_UNARY(vnegq_s32, int32x4_t, int32x4_t, -a.lane[i])
MVE_MODEL_UNARY(vcvtq_s32_f32, int32x4_t, float32x4_t, (int32_t)a.lane[i])
MVE_MODEL_UNARY(vcvtq_f32_s32, float32x4_t, int32x4_t, (float)a.lane[i])
MVE_MODEL_BINARY(vmulq_n_f32, float32x4_t, float32x4_t, float, a.lane[i] * b)
MVE_MODEL_BINARY(vcvtq_n_f32_s32, float32x4_t, int32x4_t, int,
                 (float)a.lane[i] / (float)(UINT32_C(1) << b))
MVE_MODEL_BINARY(vmulq_f32, float32x4_t, float32x4_t, float32x4_t, a.lane[i] * b.lane[i])
MVE_MODEL_BINARY(vaddq_n_f32, float32x4_t, float32x4_t, float, a.lane[i] + b)
MVE_MODEL_BINARY(vaddq_f32, float32x4_t, float32x4_t, float32x4_t, a.lane[i] + b.lane[i])
MVE_MODEL_BINARY(vsubq_n_f32, float32x4_t, float32x4_t, float, a.lane[i] - b)
MVE_MODEL_BINARY(vsubq_f32, float32x4_t, float32x4_t, float32x4_t, a.lane[i] - b.lane[i])
MVE_MODEL_BINARY(vandq_u32, uint32x4_t, uint32x4_t, uint32x4_t, a.lane[i] & b.lane[i])
MVE_MODEL_BINARY(vshlq_n_s32, int32x4_t, int32x4_t, int, a.lane[i] << b)
MVE_MODEL_BINARY(vmaxq_s32, int32x4_t, int32x4_t, int32x4_t,
                 a.lane[i] > b.lane[i] ? a.lane[i] : b.lane[i])
MVE_MODEL_BINARY(vminq_s32, int32x4_t, int32x4_t, int32x4_t,
                 a.lane[i] < b.lane[i] ? a.lane[i] : b.lane[i])
MVE_MODEL_COMPARE(vcmpeqq_n_u32, uint32x4_t, uint32_t, a.lane[i] == b)
MVE_MODEL_COMPARE(vcmpcsq_n_u32, uint32x4_t, uint32_t, a.lane[i] >= b)
MVE_MODEL_COMPARE(vcmphiq_n_u32, uint32x4_t, uint32_t, a.lane[i] > b)
MVE_MODEL_COMPARE(vcmpleq_n_f32, float32x4_t, float, a.lane[i] <= b)
MVE_MODEL_COMPARE(vcmpgtq_n_f32, float32x4_t, float, a.lane[i] > b)
MVE_MODEL_COMPARE(vcmpltq_n_f32, float32x4_t, float, a.lane[i] < b)
MVE_MODEL_COMPARE(vcmpltq_n_s32, int32x4_t, int32_t, a.lane[i] < b)

static inline uint32x4_t vreinterpretq_u32_f32(float32x4_t a) {
    uint32x4_t r;
    memcpy(&r, &a, sizeof(r));
    return r;
}
static inline int32x4_t vqrdmulhq_s32(int32x4_t a, int32x4_t b) {
    int32x4_t r;
    for (unsigned i = 0; i < 4; ++i) {
        int64_t v = ((int64_t)a.lane[i] * b.lane[i] + (INT64_C(1) << 30)) >> 31;
        r.lane[i] = v > INT32_MAX ? INT32_MAX : v < INT32_MIN ? INT32_MIN : (int32_t)v;
    }
    return r;
}
#define MVE_MODEL_SELECT(name, type)                                                               \
    static inline type name(type a, type b, mve_pred16_t predicate) {                              \
        type r;                                                                                    \
        for (unsigned i = 0; i < 4; ++i) {                                                         \
            r.lane[i] = predicate & (1u << (4 * i)) ? a.lane[i] : b.lane[i];                       \
        }                                                                                          \
        return r;                                                                                  \
    }
MVE_MODEL_SELECT(vpselq_f32, float32x4_t)
MVE_MODEL_SELECT(vpselq_s32, int32x4_t)
static inline float32x4_t vfmaq_n_f32(float32x4_t a, float32x4_t b, float c) {
    for (unsigned i = 0; i < 4; ++i) {
        a.lane[i] = fmaf(b.lane[i], c, a.lane[i]);
    }
    return a;
}
static inline void vstrhq_s32(int16_t *p, int32x4_t a) {
    for (unsigned i = 0; i < 4; ++i) {
        p[i] = (int16_t)a.lane[i];
    }
}
static inline void vst1q_f32(float *p, float32x4_t a) {
    memcpy(p, &a, sizeof(a));
}
static inline float32x4x2_t vld2q_f32(const float *p) {
    float32x4x2_t r;
    for (unsigned i = 0; i < 4; ++i) {
        r.val[0].lane[i] = p[2 * i];
        r.val[1].lane[i] = p[2 * i + 1];
    }
    return r;
}
static inline void vst2q_f32(float *p, float32x4x2_t a) {
    for (unsigned i = 0; i < 4; ++i) {
        p[2 * i] = a.val[0].lane[i];
        p[2 * i + 1] = a.val[1].lane[i];
    }
}
