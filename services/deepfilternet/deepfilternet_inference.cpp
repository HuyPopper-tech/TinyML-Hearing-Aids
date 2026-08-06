/**
 * @file deepfilternet_inference.cpp
 * @brief Static TFLM/Ethos-U adapter for the deployed DeepFilterNet model.
 * @author Bui Minh Huy
 * @date 2026-08-05
 */

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

#include "common/sha256.h"
#include "drivers/ethosu_u55/ethosu_u55.h"
#include "models/deepfilternet/generated/deepfilternet_deploy_contract.h"
#include "models/deepfilternet/generated/deepfilternet_u55_vela_model_data.h"
#include "services/deepfilternet/deepfilternet_inference.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

#ifndef M55M1_DEEPFILTERNET_DIAGNOSTICS
#define M55M1_DEEPFILTERNET_DIAGNOSTICS 0
#endif

#ifndef M55M1_DEEPFILTERNET_FAULT_INVOKE_FAIL
#define M55M1_DEEPFILTERNET_FAULT_INVOKE_FAIL 0
#endif
#ifndef M55M1_DEEPFILTERNET_FAULT_INVOKE_SLOW
#define M55M1_DEEPFILTERNET_FAULT_INVOKE_SLOW 0
#endif

namespace {

constexpr size_t kTensorArenaBytes = 256U * 1024U;
static_assert(sizeof(DFN_MODEL_SHA256) == 65U,
              "Generated DeepFilterNet model hash must remain pinned");

alignas(16) uint8_t g_tensor_arena[kTensorArenaBytes];
alignas(tflite::MicroMutableOpResolver<1>)
    uint8_t g_resolver_storage[sizeof(tflite::MicroMutableOpResolver<1>)];
alignas(tflite::MicroInterpreter)
    uint8_t g_interpreter_storage[sizeof(tflite::MicroInterpreter)];
tflite::MicroMutableOpResolver<1> *g_resolver = nullptr;
tflite::MicroInterpreter *g_interpreter = nullptr;
deepfilternet_inference_init_status_t g_init_status =
    DEEPFILTERNET_INFERENCE_INIT_NOT_RUN;

int8_t g_erb_conv_cache[DFN_INPUT_0_BYTES];
int8_t g_enc_state_0[DFN_INPUT_8_BYTES];
int8_t g_enc_state_1[DFN_INPUT_5_BYTES];
int8_t g_enc_state_2[DFN_INPUT_1_BYTES];
int8_t g_df_state_0[DFN_INPUT_6_BYTES];
int8_t g_df_state_1[DFN_INPUT_7_BYTES];
int8_t g_complex_conv_cache[DFN_INPUT_3_BYTES];

/*
 * Invoke is foreground-only and non-reentrant. These adapter-owned staging
 * buffers keep the transaction off the call stack without changing the
 * commit-on-success behavior or publishing caller outputs on failure.
 */
alignas(16) int8_t g_staged_gain[DEEPFILTERNET_INFERENCE_GAIN_BINS];
alignas(16) int8_t g_staged_df[DEEPFILTERNET_INFERENCE_DF_COEFFICIENT_BYTES];
int8_t g_staged_alpha;
alignas(16) int8_t g_staged_erb_cache[DFN_INPUT_0_BYTES];
alignas(16) int8_t g_staged_complex_cache[DFN_INPUT_3_BYTES];
alignas(16) int8_t g_staged_enc_state_0[DFN_INPUT_8_BYTES];
alignas(16) int8_t g_staged_enc_state_1[DFN_INPUT_5_BYTES];
alignas(16) int8_t g_staged_enc_state_2[DFN_INPUT_1_BYTES];
alignas(16) int8_t g_staged_df_state_0[DFN_INPUT_6_BYTES];
alignas(16) int8_t g_staged_df_state_1[DFN_INPUT_7_BYTES];
#if M55M1_DEEPFILTERNET_DIAGNOSTICS
int8_t g_last_erb_feature[DEEPFILTERNET_INFERENCE_ERB_FEATURE_BYTES];
int8_t g_last_complex_feature[DEEPFILTERNET_INFERENCE_COMPLEX_FEATURE_BYTES];
uint32_t g_invoke_count;
uint32_t g_invoke_error_count;
#endif

struct tensor_spec_t {
    uint32_t ordinal;
    uint32_t index;
    const char *name;
    size_t bytes;
    float scale;
    int32_t zero_point;
    uint32_t rank;
    int32_t shape[4];
};

static const tensor_spec_t kInputSpecs[DFN_INPUT_COUNT] = {
    {DFN_INPUT_0_ORDINAL, DFN_INPUT_0_INDEX, DFN_INPUT_0_NAME, DFN_INPUT_0_BYTES,
     DFN_INPUT_0_SCALE, DFN_INPUT_0_ZERO_POINT, DFN_INPUT_0_RANK,
     {DFN_INPUT_0_SHAPE_0, DFN_INPUT_0_SHAPE_1, 0, 0}},
    {DFN_INPUT_1_ORDINAL, DFN_INPUT_1_INDEX, DFN_INPUT_1_NAME, DFN_INPUT_1_BYTES,
     DFN_INPUT_1_SCALE, DFN_INPUT_1_ZERO_POINT, DFN_INPUT_1_RANK,
     {DFN_INPUT_1_SHAPE_0, DFN_INPUT_1_SHAPE_1, 0, 0}},
    {DFN_INPUT_2_ORDINAL, DFN_INPUT_2_INDEX, DFN_INPUT_2_NAME, DFN_INPUT_2_BYTES,
     DFN_INPUT_2_SCALE, DFN_INPUT_2_ZERO_POINT, DFN_INPUT_2_RANK,
     {DFN_INPUT_2_SHAPE_0, DFN_INPUT_2_SHAPE_1, DFN_INPUT_2_SHAPE_2, 0}},
    {DFN_INPUT_3_ORDINAL, DFN_INPUT_3_INDEX, DFN_INPUT_3_NAME, DFN_INPUT_3_BYTES,
     DFN_INPUT_3_SCALE, DFN_INPUT_3_ZERO_POINT, DFN_INPUT_3_RANK,
     {DFN_INPUT_3_SHAPE_0, DFN_INPUT_3_SHAPE_1, DFN_INPUT_3_SHAPE_2,
      DFN_INPUT_3_SHAPE_3}},
    {DFN_INPUT_4_ORDINAL, DFN_INPUT_4_INDEX, DFN_INPUT_4_NAME, DFN_INPUT_4_BYTES,
     DFN_INPUT_4_SCALE, DFN_INPUT_4_ZERO_POINT, DFN_INPUT_4_RANK,
     {DFN_INPUT_4_SHAPE_0, DFN_INPUT_4_SHAPE_1, 0, 0}},
    {DFN_INPUT_5_ORDINAL, DFN_INPUT_5_INDEX, DFN_INPUT_5_NAME, DFN_INPUT_5_BYTES,
     DFN_INPUT_5_SCALE, DFN_INPUT_5_ZERO_POINT, DFN_INPUT_5_RANK,
     {DFN_INPUT_5_SHAPE_0, DFN_INPUT_5_SHAPE_1, 0, 0}},
    {DFN_INPUT_6_ORDINAL, DFN_INPUT_6_INDEX, DFN_INPUT_6_NAME, DFN_INPUT_6_BYTES,
     DFN_INPUT_6_SCALE, DFN_INPUT_6_ZERO_POINT, DFN_INPUT_6_RANK,
     {DFN_INPUT_6_SHAPE_0, DFN_INPUT_6_SHAPE_1, 0, 0}},
    {DFN_INPUT_7_ORDINAL, DFN_INPUT_7_INDEX, DFN_INPUT_7_NAME, DFN_INPUT_7_BYTES,
     DFN_INPUT_7_SCALE, DFN_INPUT_7_ZERO_POINT, DFN_INPUT_7_RANK,
     {DFN_INPUT_7_SHAPE_0, DFN_INPUT_7_SHAPE_1, 0, 0}},
    {DFN_INPUT_8_ORDINAL, DFN_INPUT_8_INDEX, DFN_INPUT_8_NAME, DFN_INPUT_8_BYTES,
     DFN_INPUT_8_SCALE, DFN_INPUT_8_ZERO_POINT, DFN_INPUT_8_RANK,
     {DFN_INPUT_8_SHAPE_0, DFN_INPUT_8_SHAPE_1, 0, 0}},
};

static const tensor_spec_t kOutputSpecs[DFN_OUTPUT_COUNT] = {
    {DFN_OUTPUT_0_ORDINAL, DFN_OUTPUT_0_INDEX, DFN_OUTPUT_0_NAME,
     DFN_OUTPUT_0_BYTES, DFN_OUTPUT_0_SCALE, DFN_OUTPUT_0_ZERO_POINT,
     DFN_OUTPUT_0_RANK,
     {DFN_OUTPUT_0_SHAPE_0, DFN_OUTPUT_0_SHAPE_1, 0, 0}},
    {DFN_OUTPUT_1_ORDINAL, DFN_OUTPUT_1_INDEX, DFN_OUTPUT_1_NAME,
     DFN_OUTPUT_1_BYTES, DFN_OUTPUT_1_SCALE, DFN_OUTPUT_1_ZERO_POINT,
     DFN_OUTPUT_1_RANK,
     {DFN_OUTPUT_1_SHAPE_0, DFN_OUTPUT_1_SHAPE_1, 0, 0}},
    {DFN_OUTPUT_2_ORDINAL, DFN_OUTPUT_2_INDEX, DFN_OUTPUT_2_NAME,
     DFN_OUTPUT_2_BYTES, DFN_OUTPUT_2_SCALE, DFN_OUTPUT_2_ZERO_POINT,
     DFN_OUTPUT_2_RANK,
     {DFN_OUTPUT_2_SHAPE_0, DFN_OUTPUT_2_SHAPE_1, 0, 0}},
    {DFN_OUTPUT_3_ORDINAL, DFN_OUTPUT_3_INDEX, DFN_OUTPUT_3_NAME,
     DFN_OUTPUT_3_BYTES, DFN_OUTPUT_3_SCALE, DFN_OUTPUT_3_ZERO_POINT,
     DFN_OUTPUT_3_RANK,
     {DFN_OUTPUT_3_SHAPE_0, DFN_OUTPUT_3_SHAPE_1, 0, 0}},
    {DFN_OUTPUT_4_ORDINAL, DFN_OUTPUT_4_INDEX, DFN_OUTPUT_4_NAME,
     DFN_OUTPUT_4_BYTES, DFN_OUTPUT_4_SCALE, DFN_OUTPUT_4_ZERO_POINT,
     DFN_OUTPUT_4_RANK,
     {DFN_OUTPUT_4_SHAPE_0, DFN_OUTPUT_4_SHAPE_1, DFN_OUTPUT_4_SHAPE_2,
      DFN_OUTPUT_4_SHAPE_3}},
    {DFN_OUTPUT_5_ORDINAL, DFN_OUTPUT_5_INDEX, DFN_OUTPUT_5_NAME,
     DFN_OUTPUT_5_BYTES, DFN_OUTPUT_5_SCALE, DFN_OUTPUT_5_ZERO_POINT,
     DFN_OUTPUT_5_RANK,
     {DFN_OUTPUT_5_SHAPE_0, DFN_OUTPUT_5_SHAPE_1, 0, 0}},
    {DFN_OUTPUT_6_ORDINAL, DFN_OUTPUT_6_INDEX, DFN_OUTPUT_6_NAME,
     DFN_OUTPUT_6_BYTES, DFN_OUTPUT_6_SCALE, DFN_OUTPUT_6_ZERO_POINT,
     DFN_OUTPUT_6_RANK,
     {DFN_OUTPUT_6_SHAPE_0, DFN_OUTPUT_6_SHAPE_1, 0, 0}},
    {DFN_OUTPUT_7_ORDINAL, DFN_OUTPUT_7_INDEX, DFN_OUTPUT_7_NAME,
     DFN_OUTPUT_7_BYTES, DFN_OUTPUT_7_SCALE, DFN_OUTPUT_7_ZERO_POINT,
     DFN_OUTPUT_7_RANK,
     {DFN_OUTPUT_7_SHAPE_0, DFN_OUTPUT_7_SHAPE_1, 0, 0}},
    {DFN_OUTPUT_8_ORDINAL, DFN_OUTPUT_8_INDEX, DFN_OUTPUT_8_NAME,
     DFN_OUTPUT_8_BYTES, DFN_OUTPUT_8_SCALE, DFN_OUTPUT_8_ZERO_POINT,
     DFN_OUTPUT_8_RANK,
     {DFN_OUTPUT_8_SHAPE_0, DFN_OUTPUT_8_SHAPE_1, DFN_OUTPUT_8_SHAPE_2,
      DFN_OUTPUT_8_SHAPE_3}},
    {DFN_OUTPUT_9_ORDINAL, DFN_OUTPUT_9_INDEX, DFN_OUTPUT_9_NAME,
     DFN_OUTPUT_9_BYTES, DFN_OUTPUT_9_SCALE, DFN_OUTPUT_9_ZERO_POINT,
     DFN_OUTPUT_9_RANK,
     {DFN_OUTPUT_9_SHAPE_0, DFN_OUTPUT_9_SHAPE_1, 0, 0}},
};

/* Exact post-Vela custom-op tensor layout from the deployed FlatBuffer. */
static const int32_t kExpectedEthosUInputTensorIndices[] = {
    0, 1, 2, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
};
static const int32_t kExpectedEthosUOutputTensorIndices[] = {
    12, 13, 14, 15, 16, 17, 18, 19, 20,
};
static const uint32_t kExpectedTensorBuffers[DFN_TENSOR_COUNT] = {
    1U, 2U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U,
    0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U,
};

bool model_hash_valid() {
    uint8_t digest[SHA256_DIGEST_BYTES];

    sha256_compute(deepfilternet_u55_vela_model,
                   deepfilternet_u55_vela_model_len, digest);
    return std::memcmp(digest, dfn_model_sha256, sizeof(digest)) == 0;
}

bool raw_tensor_valid(const tflite::Tensor *tensor, const tensor_spec_t &spec) {
    const flatbuffers::Vector<int32_t> *shape;
    const tflite::QuantizationParameters *quantization;
    size_t bytes = 1U;

    if ((tensor == nullptr) || (tensor->type() != tflite::TensorType_INT8) ||
        (tensor->name() == nullptr) ||
        (std::strcmp(tensor->name()->c_str(), spec.name) != 0)) {
        return false;
    }
    shape = tensor->shape();
    if ((shape == nullptr) || (shape->size() != spec.rank)) {
        return false;
    }
    for (uint32_t dimension = 0U; dimension < spec.rank; ++dimension) {
        if (shape->Get(dimension) != spec.shape[dimension]) {
            return false;
        }
        bytes *= static_cast<size_t>(shape->Get(dimension));
    }
    if (bytes != spec.bytes) {
        return false;
    }
    quantization = tensor->quantization();
    if ((quantization == nullptr) || (quantization->scale() == nullptr) ||
        (quantization->zero_point() == nullptr) ||
        (quantization->scale()->size() != 1U) ||
        (quantization->zero_point()->size() != 1U) ||
        (quantization->scale()->Get(0) != spec.scale) ||
        (quantization->zero_point()->Get(0) != spec.zero_point)) {
        return false;
    }
    return true;
}

bool model_contract_valid(const tflite::Model *model) {
    const tflite::SubGraph *subgraph;
    const tflite::OperatorCode *operator_code;
    const tflite::Operator *operation;
    const flatbuffers::Vector<int32_t> *operation_inputs;
    const flatbuffers::Vector<int32_t> *operation_outputs;
    const flatbuffers::Vector<int32_t> *inputs;
    const flatbuffers::Vector<int32_t> *outputs;
    const flatbuffers::Vector<flatbuffers::Offset<tflite::Tensor>> *tensors;

    if ((model == nullptr) ||
        (deepfilternet_u55_vela_model_len != DFN_MODEL_BYTES) ||
        (model->version() != TFLITE_SCHEMA_VERSION) ||
        (model->subgraphs() == nullptr) || (model->subgraphs()->size() != 1U) ||
        (DFN_HAS_ETHOS_U_CUSTOM_OP != 1) ||
        (model->operator_codes() == nullptr) ||
        (model->operator_codes()->size() != DFN_OPERATOR_COUNT)) {
        return false;
    }
    subgraph = model->subgraphs()->Get(0);
    if ((subgraph == nullptr) || (subgraph->inputs() == nullptr) ||
        (subgraph->outputs() == nullptr) || (subgraph->operators() == nullptr) ||
        (subgraph->inputs()->size() != DFN_INPUT_COUNT) ||
        (subgraph->outputs()->size() != DFN_OUTPUT_COUNT) ||
        (subgraph->operators()->size() != DFN_OPERATOR_COUNT) ||
        (subgraph->tensors() == nullptr) ||
        (subgraph->tensors()->size() != DFN_TENSOR_COUNT)) {
        return false;
    }
    operator_code = model->operator_codes()->Get(0);
    operation = subgraph->operators()->Get(0);
    if ((operator_code == nullptr) || (operation == nullptr) ||
        (operator_code->builtin_code() != tflite::BuiltinOperator_CUSTOM) ||
        (operator_code->custom_code() == nullptr) ||
        (std::strcmp(operator_code->custom_code()->c_str(), "ethos-u") != 0) ||
        (operation->opcode_index() != 0U)) {
        return false;
    }
    inputs = subgraph->inputs();
    outputs = subgraph->outputs();
    tensors = subgraph->tensors();
    operation_inputs = operation->inputs();
    operation_outputs = operation->outputs();
    if ((operation_inputs == nullptr) || (operation_outputs == nullptr) ||
        (operation_inputs->size() !=
         (sizeof(kExpectedEthosUInputTensorIndices) /
          sizeof(kExpectedEthosUInputTensorIndices[0]))) ||
        (operation_outputs->size() !=
         (sizeof(kExpectedEthosUOutputTensorIndices) /
          sizeof(kExpectedEthosUOutputTensorIndices[0])))) {
        return false;
    }
    for (size_t ordinal = 0U;
         ordinal < sizeof(kExpectedEthosUInputTensorIndices) /
                       sizeof(kExpectedEthosUInputTensorIndices[0]);
         ++ordinal) {
        const int32_t tensor_index = operation_inputs->Get(ordinal);
        const tflite::Tensor *tensor =
            (tensor_index >= 0 &&
             static_cast<uint32_t>(tensor_index) < tensors->size())
                ? tensors->Get(tensor_index)
                : nullptr;
        if ((tensor_index != kExpectedEthosUInputTensorIndices[ordinal]) ||
            (tensor_index < 0) ||
            (static_cast<uint32_t>(tensor_index) >= tensors->size()) ||
            (tensor == nullptr) ||
            (tensor->buffer() !=
             kExpectedTensorBuffers[static_cast<uint32_t>(tensor_index)])) {
            return false;
        }
    }
    for (size_t ordinal = 0U;
         ordinal < sizeof(kExpectedEthosUOutputTensorIndices) /
                       sizeof(kExpectedEthosUOutputTensorIndices[0]);
         ++ordinal) {
        const int32_t tensor_index = operation_outputs->Get(ordinal);
        const tflite::Tensor *tensor =
            (tensor_index >= 0 &&
             static_cast<uint32_t>(tensor_index) < tensors->size())
                ? tensors->Get(tensor_index)
                : nullptr;
        if ((tensor_index != kExpectedEthosUOutputTensorIndices[ordinal]) ||
            (tensor_index < 0) ||
            (static_cast<uint32_t>(tensor_index) >= tensors->size()) ||
            (tensor == nullptr) ||
            (tensor->buffer() !=
             kExpectedTensorBuffers[static_cast<uint32_t>(tensor_index)])) {
            return false;
        }
    }
    for (uint32_t ordinal = 0U; ordinal < DFN_INPUT_COUNT; ++ordinal) {
        const tensor_spec_t &spec = kInputSpecs[ordinal];
        if ((spec.ordinal != ordinal) ||
            (inputs->Get(ordinal) != static_cast<int32_t>(spec.index)) ||
            (spec.index >= tensors->size()) ||
            !raw_tensor_valid(tensors->Get(spec.index), spec)) {
            return false;
        }
    }
    for (uint32_t ordinal = 0U; ordinal < DFN_OUTPUT_COUNT; ++ordinal) {
        const tensor_spec_t &spec = kOutputSpecs[ordinal];
        if ((spec.ordinal != ordinal) ||
            (outputs->Get(ordinal) != static_cast<int32_t>(spec.index)) ||
            (spec.index >= tensors->size()) ||
            !raw_tensor_valid(tensors->Get(spec.index), spec)) {
            return false;
        }
    }
    return true;
}

bool tensor_valid(const TfLiteTensor *tensor, const tensor_spec_t &spec) {
    if ((tensor == nullptr) || (tensor->type != kTfLiteInt8) ||
        (tensor->dims == nullptr) ||
        (tensor->dims->size != static_cast<int>(spec.rank)) ||
        (tensor->bytes != spec.bytes) || (tensor->params.scale != spec.scale) ||
        (tensor->params.zero_point != spec.zero_point) ||
        (tensor->data.int8 == nullptr)) {
        return false;
    }
#ifndef TF_LITE_STATIC_MEMORY
    if ((tensor->name == nullptr) ||
        (std::strcmp(tensor->name, spec.name) != 0)) {
        return false;
    }
#endif
    for (uint32_t dimension = 0U; dimension < spec.rank; ++dimension) {
        if (tensor->dims->data[dimension] != spec.shape[dimension]) {
            return false;
        }
    }
    return true;
}

bool input_tensor_valid(uint32_t ordinal) {
    return (g_interpreter != nullptr) &&
           tensor_valid(g_interpreter->input(kInputSpecs[ordinal].ordinal),
                        kInputSpecs[ordinal]);
}

bool output_tensor_valid(uint32_t ordinal) {
    return (g_interpreter != nullptr) &&
           tensor_valid(g_interpreter->output(kOutputSpecs[ordinal].ordinal),
                        kOutputSpecs[ordinal]);
}

bool tensor_storage_in_arena(const TfLiteTensor *tensor) {
    const uintptr_t arena_begin = reinterpret_cast<uintptr_t>(g_tensor_arena);
    const uintptr_t arena_end = arena_begin + sizeof(g_tensor_arena);
    const uintptr_t address = (tensor == nullptr) ? 0U :
        reinterpret_cast<uintptr_t>(tensor->data.int8);

    return (tensor != nullptr) && (tensor->data.int8 != nullptr) &&
           (address >= arena_begin) && (address <= arena_end) &&
           (tensor->bytes <= arena_end - address);
}

bool logical_io_uses_shared_arena() {
    for (uint32_t ordinal = 0U; ordinal < DFN_INPUT_COUNT; ++ordinal) {
        const TfLiteTensor *tensor = g_interpreter->input(ordinal);
        if (!tensor_storage_in_arena(tensor)) {
            return false;
        }
    }
    for (uint32_t ordinal = 0U; ordinal < DFN_OUTPUT_COUNT; ++ordinal) {
        const TfLiteTensor *tensor = g_interpreter->output(ordinal);
        if (!tensor_storage_in_arena(tensor)) {
            return false;
        }
    }
    return true;
}

bool tensor_contract_valid() {
    for (uint32_t ordinal = 0U; ordinal < DFN_INPUT_COUNT; ++ordinal) {
        if (!input_tensor_valid(ordinal)) {
            return false;
        }
    }
    for (uint32_t ordinal = 0U; ordinal < DFN_OUTPUT_COUNT; ++ordinal) {
        if (!output_tensor_valid(ordinal)) {
            return false;
        }
    }
    return logical_io_uses_shared_arena();
}

int8_t saturate_int8(int32_t value) {
    if (value < -128) {
        return static_cast<int8_t>(-128);
    }
    if (value > 127) {
        return static_cast<int8_t>(127);
    }
    return static_cast<int8_t>(value);
}

int8_t requantize_df_state_0(int8_t output_value) {
    // std::round gives deterministic nearest-integer, ties-away-from-zero.
    const float dequantized =
        (static_cast<float>(output_value) -
         static_cast<float>(DFN_OUTPUT_NEXT_DF_STATE_0_ZERO_POINT)) *
        DFN_OUTPUT_NEXT_DF_STATE_0_SCALE;
    const float input_value =
        dequantized / DFN_INPUT_DF_STATE_0_SCALE +
        static_cast<float>(DFN_INPUT_DF_STATE_0_ZERO_POINT);
    const int32_t rounded = static_cast<int32_t>(std::round(input_value));

    return saturate_int8(rounded);
}

void copy_state_inputs() {
    std::memcpy(g_interpreter->input(DFN_INPUT_ERB_CONV_CACHE_ORDINAL)->data.int8,
                g_erb_conv_cache, DFN_INPUT_0_BYTES);
    std::memcpy(g_interpreter->input(DFN_INPUT_ENC_STATE_2_ORDINAL)->data.int8,
                g_enc_state_2, DFN_INPUT_1_BYTES);
    std::memcpy(g_interpreter->input(DFN_INPUT_COMPLEX_CONV_CACHE_ORDINAL)->data.int8,
                g_complex_conv_cache, DFN_INPUT_3_BYTES);
    std::memcpy(g_interpreter->input(DFN_INPUT_ENC_STATE_1_ORDINAL)->data.int8,
                g_enc_state_1, DFN_INPUT_5_BYTES);
    std::memcpy(g_interpreter->input(DFN_INPUT_DF_STATE_0_ORDINAL)->data.int8,
                g_df_state_0, DFN_INPUT_6_BYTES);
    std::memcpy(g_interpreter->input(DFN_INPUT_DF_STATE_1_ORDINAL)->data.int8,
                g_df_state_1, DFN_INPUT_7_BYTES);
    std::memcpy(g_interpreter->input(DFN_INPUT_ENC_STATE_0_ORDINAL)->data.int8,
                g_enc_state_0, DFN_INPUT_8_BYTES);
}

void reset_state_storage() {
    std::memset(g_erb_conv_cache, DFN_INPUT_ERB_CONV_CACHE_ZERO_POINT,
                sizeof(g_erb_conv_cache));
    std::memset(g_complex_conv_cache, DFN_INPUT_COMPLEX_CONV_CACHE_ZERO_POINT,
                sizeof(g_complex_conv_cache));
    std::memset(g_enc_state_0, DFN_INPUT_ENC_STATE_0_ZERO_POINT,
                sizeof(g_enc_state_0));
    std::memset(g_enc_state_1, DFN_INPUT_ENC_STATE_1_ZERO_POINT,
                sizeof(g_enc_state_1));
    std::memset(g_enc_state_2, DFN_INPUT_ENC_STATE_2_ZERO_POINT,
                sizeof(g_enc_state_2));
    std::memset(g_df_state_0, DFN_INPUT_DF_STATE_0_ZERO_POINT,
                sizeof(g_df_state_0));
    std::memset(g_df_state_1, DFN_INPUT_DF_STATE_1_ZERO_POINT,
                sizeof(g_df_state_1));
}

void reset_interpreter_state() {
    if ((g_interpreter != nullptr) &&
        input_tensor_valid(DFN_INPUT_ERB_CONV_CACHE_ORDINAL) &&
        input_tensor_valid(DFN_INPUT_ENC_STATE_2_ORDINAL) &&
        input_tensor_valid(DFN_INPUT_COMPLEX_CONV_CACHE_ORDINAL) &&
        input_tensor_valid(DFN_INPUT_ENC_STATE_1_ORDINAL) &&
        input_tensor_valid(DFN_INPUT_DF_STATE_0_ORDINAL) &&
        input_tensor_valid(DFN_INPUT_DF_STATE_1_ORDINAL) &&
        input_tensor_valid(DFN_INPUT_ENC_STATE_0_ORDINAL)) {
        copy_state_inputs();
    }
}

bool invoke_failure() {
#if M55M1_DEEPFILTERNET_DIAGNOSTICS
    ++g_invoke_error_count;
#endif
    deepfilternet_inference_reset();
    return false;
}

#if M55M1_DEEPFILTERNET_DIAGNOSTICS
void scan_int8(const int8_t *values, size_t count,
               int32_t *minimum, int32_t *maximum, uint32_t *saturated) {
    *minimum = 0;
    *maximum = 0;
    *saturated = 0U;
    if ((values == nullptr) || (count == 0U)) {
        return;
    }
    *minimum = values[0];
    *maximum = values[0];
    for (size_t index = 0U; index < count; ++index) {
        const int32_t value = values[index];
        if (value < *minimum) *minimum = value;
        if (value > *maximum) *maximum = value;
        if ((value == -128) || (value == 127)) ++(*saturated);
    }
}
#endif

}  // namespace

extern "C" bool deepfilternet_inference_init(void) {
    const tflite::Model *model;
    TfLiteStatus allocate_status;
    flatbuffers::Verifier verifier(
        reinterpret_cast<const uint8_t *>(deepfilternet_u55_vela_model),
        deepfilternet_u55_vela_model_len);

    if (g_interpreter != nullptr) {
        g_init_status = DEEPFILTERNET_INFERENCE_INIT_READY;
        return true;
    }
    if (!tflite::VerifyModelBuffer(verifier) || !model_hash_valid()) {
        g_init_status = DEEPFILTERNET_INFERENCE_INIT_MODEL_VERIFY_FAILED;
        return false;
    }
    model = tflite::GetModel(deepfilternet_u55_vela_model);
    if (!model_contract_valid(model)) {
        g_init_status = DEEPFILTERNET_INFERENCE_INIT_MODEL_CONTRACT_FAILED;
        return false;
    }
    if (!ethosu_u55_init()) {
        g_init_status = DEEPFILTERNET_INFERENCE_INIT_ETHOSU_FAILED;
        return false;
    }
    g_resolver = new (g_resolver_storage) tflite::MicroMutableOpResolver<1>();
    if ((g_resolver == nullptr) || (g_resolver->AddEthosU() != kTfLiteOk)) {
        g_resolver = nullptr;
        g_init_status = DEEPFILTERNET_INFERENCE_INIT_RESOLVER_FAILED;
        return false;
    }
    g_interpreter = new (g_interpreter_storage)
        tflite::MicroInterpreter(model, *g_resolver, g_tensor_arena,
                                 sizeof(g_tensor_arena));
    if (g_interpreter == nullptr) {
        g_init_status = DEEPFILTERNET_INFERENCE_INIT_ARENA_FAILED;
        return false;
    }
    allocate_status = g_interpreter->AllocateTensors();
    if (allocate_status != kTfLiteOk) {
        g_interpreter = nullptr;
        g_init_status = DEEPFILTERNET_INFERENCE_INIT_ARENA_FAILED;
        return false;
    }
    if (!tensor_contract_valid()) {
        g_interpreter = nullptr;
        g_init_status = DEEPFILTERNET_INFERENCE_INIT_TENSOR_CONTRACT_FAILED;
        return false;
    }
    deepfilternet_inference_reset();
    g_init_status = DEEPFILTERNET_INFERENCE_INIT_READY;
    return true;
}

extern "C" int deepfilternet_inference_last_init_status(void) {
    return static_cast<int>(g_init_status);
}

extern "C" bool deepfilternet_inference_invoke(
    const int8_t erb_feature[DEEPFILTERNET_INFERENCE_ERB_FEATURE_BYTES],
    const int8_t complex_feature[DEEPFILTERNET_INFERENCE_COMPLEX_FEATURE_BYTES],
    int8_t gain_bins[DEEPFILTERNET_INFERENCE_GAIN_BINS],
    int8_t df_coefficients[DEEPFILTERNET_INFERENCE_DF_COEFFICIENT_BYTES],
    int8_t *alpha) {
    if ((g_interpreter == nullptr) || (erb_feature == nullptr) ||
        (complex_feature == nullptr) || (gain_bins == nullptr) ||
        (df_coefficients == nullptr) || (alpha == nullptr) ||
        !tensor_contract_valid()) {
        return invoke_failure();
    }
    copy_state_inputs();
    std::memcpy(g_interpreter->input(DFN_INPUT_ERB_FEATURE_ORDINAL)->data.int8,
                erb_feature, DFN_INPUT_4_BYTES);
    std::memcpy(g_interpreter->input(DFN_INPUT_COMPLEX_FEATURE_ORDINAL)->data.int8,
                complex_feature, DFN_INPUT_2_BYTES);
#if M55M1_DEEPFILTERNET_DIAGNOSTICS
    std::memcpy(g_last_erb_feature, erb_feature, sizeof(g_last_erb_feature));
    std::memcpy(g_last_complex_feature, complex_feature,
                sizeof(g_last_complex_feature));
#endif
#if M55M1_DEEPFILTERNET_FAULT_INVOKE_FAIL
    return invoke_failure();
#endif
#if M55M1_DEEPFILTERNET_FAULT_INVOKE_SLOW
    ethosu_u55_fault_delay_ms(12U);
#endif
    if (g_interpreter->Invoke() != kTfLiteOk) {
        return invoke_failure();
    }
    for (uint32_t ordinal = 0U; ordinal < DFN_OUTPUT_COUNT; ++ordinal) {
        if (!output_tensor_valid(ordinal)) {
            return invoke_failure();
        }
    }

    std::memcpy(g_staged_gain,
                g_interpreter->output(DFN_OUTPUT_GAIN_BINS_ORDINAL)->data.int8,
                sizeof(g_staged_gain));
    std::memcpy(g_staged_df,
                g_interpreter->output(DFN_OUTPUT_DF_COEFFICIENTS_ORDINAL)->data.int8,
                sizeof(g_staged_df));
    g_staged_alpha =
        g_interpreter->output(DFN_OUTPUT_ALPHA_ORDINAL)->data.int8[0];
    std::memcpy(g_staged_enc_state_0,
                g_interpreter->output(DFN_OUTPUT_NEXT_ENC_STATE_0_ORDINAL)
                    ->data.int8,
                sizeof(g_staged_enc_state_0));
    std::memcpy(g_staged_enc_state_1,
                g_interpreter->output(DFN_OUTPUT_NEXT_ENC_STATE_1_ORDINAL)
                    ->data.int8,
                sizeof(g_staged_enc_state_1));
    std::memcpy(g_staged_enc_state_2,
                g_interpreter->output(DFN_OUTPUT_NEXT_ENC_STATE_2_ORDINAL)
                    ->data.int8,
                sizeof(g_staged_enc_state_2));
    std::memcpy(g_staged_df_state_1,
                g_interpreter->output(DFN_OUTPUT_NEXT_DF_STATE_1_ORDINAL)
                    ->data.int8,
                sizeof(g_staged_df_state_1));
    for (size_t index = 0U; index < sizeof(g_staged_df_state_0); ++index) {
        g_staged_df_state_0[index] = requantize_df_state_0(
            g_interpreter->output(DFN_OUTPUT_NEXT_DF_STATE_0_ORDINAL)
                ->data.int8[index]);
    }
    std::memcpy(g_staged_complex_cache,
                g_interpreter->output(DFN_OUTPUT_NEXT_COMPLEX_CONV_CACHE_ORDINAL)
                    ->data.int8,
                sizeof(g_staged_complex_cache));
    /* The Vela output aliases the current ERB feature tensor by contract. */
    std::memcpy(g_staged_erb_cache, erb_feature, sizeof(g_staged_erb_cache));

    std::memcpy(gain_bins, g_staged_gain, sizeof(g_staged_gain));
    std::memcpy(df_coefficients, g_staged_df, sizeof(g_staged_df));
    *alpha = g_staged_alpha;
    std::memcpy(g_enc_state_0, g_staged_enc_state_0, sizeof(g_enc_state_0));
    std::memcpy(g_enc_state_1, g_staged_enc_state_1, sizeof(g_enc_state_1));
    std::memcpy(g_enc_state_2, g_staged_enc_state_2, sizeof(g_enc_state_2));
    std::memcpy(g_df_state_0, g_staged_df_state_0, sizeof(g_df_state_0));
    std::memcpy(g_df_state_1, g_staged_df_state_1, sizeof(g_df_state_1));
    std::memcpy(g_erb_conv_cache, g_staged_erb_cache, sizeof(g_erb_conv_cache));
    std::memcpy(g_complex_conv_cache, g_staged_complex_cache,
                sizeof(g_complex_conv_cache));
#if M55M1_DEEPFILTERNET_DIAGNOSTICS
    ++g_invoke_count;
#endif
    return true;
}

extern "C" void deepfilternet_inference_reset(void) {
    reset_state_storage();
    reset_interpreter_state();
}

extern "C" void deepfilternet_inference_get_diagnostics(
    deepfilternet_inference_diagnostics_t *diagnostics) {
#if M55M1_DEEPFILTERNET_DIAGNOSTICS
    const int8_t *states[DEEPFILTERNET_INFERENCE_STATE_COUNT] = {
        g_erb_conv_cache, g_enc_state_0, g_enc_state_1, g_enc_state_2,
        g_df_state_0, g_df_state_1, g_complex_conv_cache};
    const size_t state_sizes[DEEPFILTERNET_INFERENCE_STATE_COUNT] = {
        sizeof(g_erb_conv_cache), sizeof(g_enc_state_0), sizeof(g_enc_state_1),
        sizeof(g_enc_state_2), sizeof(g_df_state_0), sizeof(g_df_state_1),
        sizeof(g_complex_conv_cache)};
#endif

    if (diagnostics == nullptr) {
        return;
    }
    std::memset(diagnostics, 0, sizeof(*diagnostics));
#if M55M1_DEEPFILTERNET_DIAGNOSTICS
    diagnostics->invoke_count = g_invoke_count;
    diagnostics->invoke_error_count = g_invoke_error_count;
    diagnostics->arena_capacity_bytes = sizeof(g_tensor_arena);
    diagnostics->arena_used_bytes =
        (g_interpreter == nullptr) ? 0U :
        static_cast<uint32_t>(g_interpreter->arena_used_bytes());
    scan_int8(g_last_erb_feature, sizeof(g_last_erb_feature),
              &diagnostics->feature_min, &diagnostics->feature_max,
              &diagnostics->feature_sat_count);
    scan_int8(g_last_complex_feature, sizeof(g_last_complex_feature),
              &diagnostics->complex_min, &diagnostics->complex_max,
              &diagnostics->complex_sat_count);
    scan_int8(g_staged_gain, sizeof(g_staged_gain), &diagnostics->gain_min,
              &diagnostics->gain_max, &diagnostics->gain_sat_count);
    scan_int8(g_staged_df, sizeof(g_staged_df), &diagnostics->df_min,
              &diagnostics->df_max, &diagnostics->df_sat_count);
    diagnostics->alpha_min = g_staged_alpha;
    diagnostics->alpha_max = g_staged_alpha;
    diagnostics->alpha_sat_count =
        ((g_staged_alpha == -128) || (g_staged_alpha == 127)) ? 1U : 0U;
    for (uint32_t index = 0U; index < DEEPFILTERNET_INFERENCE_STATE_COUNT;
         ++index) {
        scan_int8(states[index], state_sizes[index],
                  &diagnostics->state_min[index],
                  &diagnostics->state_max[index],
                  &diagnostics->state_sat_count[index]);
    }
#endif
}
