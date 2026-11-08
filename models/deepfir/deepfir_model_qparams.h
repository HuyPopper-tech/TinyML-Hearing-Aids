/* R2 immutable binding; author: Codex; date: 2026-11-08. */
#pragma once
#include "deepfir_r2_contract.h"
#ifdef __cplusplus
extern "C" {
#endif
static const char deepfir_compiled_sha256[] = "69c1c3d2f2491df24a66c2190f266091bf8c192fcbce6724ee430d5cf28b1c27";
static const float deepfir_input_scale[3] = {9.672559826867655e-05f,3.0516976039507426e-05f,2.9065247872495092e-05f};
static const int32_t deepfir_input_zero[3] = {0,0,0};
static const float deepfir_output_scale[4] = {5.657631845679134e-05f,7.72049679653719e-05f,3.051537714782171e-05f,2.9116219593561254e-05f};
static const int32_t deepfir_output_zero[4] = {0,0,0,0};
#define DF_INPUT_PORT DF_R2_INPUT_PORT
#define DF_OUTPUT_PORT DF_R2_OUTPUT_PORT
#define DF_INPUT_ZERO DF_R2_INPUT_ZERO
#define DF_OUTPUT_ZERO DF_R2_OUTPUT_ZERO
#define DF_FEEDBACK_MULTIPLIER DF_R2_FEEDBACK_MULTIPLIER
#define DF_FEEDBACK_SHIFT DF_R2_FEEDBACK_SHIFT
#define DF_DIRECT_COPY DF_R2_DIRECT_COPY
#ifdef __cplusplus
}
#endif
