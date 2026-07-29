/**
 * @file erb_gru_inference.cpp
 * @brief Verified static TFLM interpreter for one ERB-GRU Vela model.
 * @author Bui Minh Huy
 * @date 2026-07-29
 */

#include <cstddef>
#include <cstring>
#include <new>

#include "common/sha256.h"
#include "drivers/ethosu_u55/ethosu_u55.h"
#include "models/erb_gru/generated/erb_gru_assets.h"
#include "models/erb_gru/generated/erb_gru_model_data.h"
#include "services/erb_gru/erb_gru_inference.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

namespace {

constexpr size_t kTensorArenaBytes = 128U * 1024U;
alignas(16) uint8_t g_tensor_arena[kTensorArenaBytes];
tflite::MicroMutableOpResolver<1> g_resolver;
alignas(tflite::MicroInterpreter) uint8_t g_interpreter_storage[sizeof(tflite::MicroInterpreter)];
tflite::MicroInterpreter *g_interpreter;
uint32_t g_last_invoke_cycles;
uint32_t g_max_invoke_cycles;
uint32_t g_invoke_count;
uint32_t g_invoke_error_count;
uint32_t g_deadline_miss_count;
size_t g_arena_used_bytes;

bool model_hash_valid()
{
    uint8_t digest[SHA256_DIGEST_BYTES];

    sha256_compute(erb_gru_vela_model, erb_gru_vela_model_len, digest);
    return std::memcmp(digest, erb_gru_model_sha256, sizeof(digest)) == 0;
}

bool shape_valid(const TfLiteTensor *tensor, int rank, int length)
{
    if ((tensor == nullptr) || (tensor->type != kTfLiteInt8) || (tensor->dims == nullptr) ||
        (tensor->dims->size != rank) || (tensor->dims->data[0] != ERB_GRU_BATCH_SIZE) ||
        (tensor->dims->data[rank - 1] != length))
    {
        return false;
    }
    for (int dimension = 1; dimension < (rank - 1); ++dimension)
    {
        if (tensor->dims->data[dimension] != 1)
        {
            return false;
        }
    }
    return true;
}

bool tensor_valid(const TfLiteTensor *tensor, int rank, int length, float scale, int zero_point)
{
    return shape_valid(tensor, rank, length) && (tensor->bytes == static_cast<size_t>(length)) &&
           (tensor->params.scale == scale) && (tensor->params.zero_point == zero_point);
}

bool model_contract_valid(const tflite::Model *model)
{
    const tflite::SubGraph *subgraph;
    const tflite::OperatorCode *operator_code;

    if ((model == nullptr) || (erb_gru_vela_model_len != ERB_GRU_MODEL_BYTES) ||
        (model->subgraphs() == nullptr) || (model->subgraphs()->size() != 1U) ||
        (model->operator_codes() == nullptr) || (model->operator_codes()->size() != 1U))
    {
        return false;
    }
    subgraph = model->subgraphs()->Get(0);
    operator_code = model->operator_codes()->Get(0);
    if ((subgraph == nullptr) || (subgraph->inputs() == nullptr) || (subgraph->outputs() == nullptr) ||
        (subgraph->operators() == nullptr) || (subgraph->inputs()->size() != 3U) ||
        (subgraph->outputs()->size() != 3U) || (subgraph->operators()->size() != 1U) ||
        (operator_code == nullptr) || (operator_code->builtin_code() != tflite::BuiltinOperator_CUSTOM) ||
        (operator_code->custom_code() == nullptr) ||
        (std::strcmp(operator_code->custom_code()->c_str(), "ethos-u") != 0))
    {
        return false;
    }
    return (subgraph->inputs()->Get(ERB_GRU_INPUT_STATE1_ORDINAL) == ERB_GRU_INPUT_STATE1_INDEX) &&
           (subgraph->inputs()->Get(ERB_GRU_INPUT_STATE2_ORDINAL) == ERB_GRU_INPUT_STATE2_INDEX) &&
           (subgraph->inputs()->Get(ERB_GRU_INPUT_FEATURE_ORDINAL) == ERB_GRU_INPUT_FEATURE_INDEX) &&
           (subgraph->outputs()->Get(ERB_GRU_OUTPUT_STATE2_ORDINAL) == ERB_GRU_OUTPUT_STATE2_INDEX) &&
           (subgraph->outputs()->Get(ERB_GRU_OUTPUT_STATE1_ORDINAL) == ERB_GRU_OUTPUT_STATE1_INDEX) &&
           (subgraph->outputs()->Get(ERB_GRU_OUTPUT_GAIN_ORDINAL) == ERB_GRU_OUTPUT_GAIN_INDEX);
}

bool tensors_valid()
{
    return tensor_valid(g_interpreter->input(ERB_GRU_INPUT_STATE1_ORDINAL), ERB_GRU_STATE_RANK,
                        ERB_GRU_STATE_SIZE, ERB_GRU_STATE1_SCALE, ERB_GRU_STATE1_ZERO_POINT) &&
           tensor_valid(g_interpreter->input(ERB_GRU_INPUT_STATE2_ORDINAL), ERB_GRU_STATE_RANK,
                        ERB_GRU_STATE_SIZE, ERB_GRU_STATE2_SCALE, ERB_GRU_STATE2_ZERO_POINT) &&
           tensor_valid(g_interpreter->input(ERB_GRU_INPUT_FEATURE_ORDINAL), ERB_GRU_FEATURE_RANK,
                        ERB_GRU_FEATURE_SIZE, ERB_GRU_FEATURE_SCALE, ERB_GRU_FEATURE_ZERO_POINT) &&
           tensor_valid(g_interpreter->output(ERB_GRU_OUTPUT_STATE2_ORDINAL), ERB_GRU_STATE_RANK,
                        ERB_GRU_STATE_SIZE, ERB_GRU_STATE2_SCALE, ERB_GRU_STATE2_ZERO_POINT) &&
           tensor_valid(g_interpreter->output(ERB_GRU_OUTPUT_STATE1_ORDINAL), ERB_GRU_STATE_RANK,
                        ERB_GRU_STATE_SIZE, ERB_GRU_STATE1_SCALE, ERB_GRU_STATE1_ZERO_POINT) &&
           tensor_valid(g_interpreter->output(ERB_GRU_OUTPUT_GAIN_ORDINAL), ERB_GRU_FEATURE_RANK,
                        ERB_GRU_FEATURE_SIZE, ERB_GRU_GAIN_SCALE, ERB_GRU_GAIN_ZERO_POINT);
}

} // namespace

extern "C" bool erb_gru_inference_init(void)
{
    const tflite::Model *model;

    if (g_interpreter != nullptr)
    {
        return true;
    }
    if (!ethosu_u55_init())
    {
        return false;
    }

    model = tflite::GetModel(erb_gru_vela_model);
    if (!model_hash_valid() || !model_contract_valid(model) || (model->version() != TFLITE_SCHEMA_VERSION) ||
        (g_resolver.AddEthosU() != kTfLiteOk))
    {
        return false;
    }

    g_interpreter = new (g_interpreter_storage)
        tflite::MicroInterpreter(model, g_resolver, g_tensor_arena, sizeof(g_tensor_arena));
    if ((g_interpreter->AllocateTensors() != kTfLiteOk) || !tensors_valid())
    {
        g_interpreter = nullptr;
        return false;
    }
    g_arena_used_bytes = g_interpreter->arena_used_bytes();
    return true;
}

extern "C" bool erb_gru_inference_invoke(
    const int8_t feature[ERB_GRU_INFERENCE_BANDS],
    const int8_t state1[ERB_GRU_INFERENCE_STATE_SIZE],
    const int8_t state2[ERB_GRU_INFERENCE_STATE_SIZE],
    int8_t gain[ERB_GRU_INFERENCE_BANDS],
    int8_t next_state1[ERB_GRU_INFERENCE_STATE_SIZE],
    int8_t next_state2[ERB_GRU_INFERENCE_STATE_SIZE])
{
    uint32_t start_cycle;

    if ((g_interpreter == nullptr) || (feature == nullptr) || (state1 == nullptr) ||
        (state2 == nullptr) || (gain == nullptr) || (next_state1 == nullptr) ||
        (next_state2 == nullptr))
    {
        return false;
    }

    std::memcpy(g_interpreter->input(ERB_GRU_INPUT_STATE1_ORDINAL)->data.int8, state1,
                ERB_GRU_INFERENCE_STATE_SIZE);
    std::memcpy(g_interpreter->input(ERB_GRU_INPUT_STATE2_ORDINAL)->data.int8, state2,
                ERB_GRU_INFERENCE_STATE_SIZE);
    std::memcpy(g_interpreter->input(ERB_GRU_INPUT_FEATURE_ORDINAL)->data.int8, feature,
                ERB_GRU_INFERENCE_BANDS);

    start_cycle = ethosu_u55_cycles_now();
#if M55M1_ERB_GRU_FAULT_INVOKE_FAIL
    ++g_invoke_error_count;
    return false;
#endif
    if (g_interpreter->Invoke() != kTfLiteOk)
    {
        ++g_invoke_error_count;
        return false;
    }
#if M55M1_ERB_GRU_FAULT_INVOKE_SLOW
    ethosu_u55_fault_delay_ms(12U);
#endif
    g_last_invoke_cycles = ethosu_u55_cycles_now() - start_cycle;
    if (g_last_invoke_cycles > g_max_invoke_cycles)
    {
        g_max_invoke_cycles = g_last_invoke_cycles;
    }
    ++g_invoke_count;
    if (ethosu_u55_deadline_exceeded(start_cycle, 10U))
    {
        ++g_deadline_miss_count;
        ++g_invoke_error_count;
        return false;
    }

    std::memcpy(next_state2,
                g_interpreter->output(ERB_GRU_OUTPUT_STATE2_ORDINAL)->data.int8,
                ERB_GRU_INFERENCE_STATE_SIZE);
    std::memcpy(next_state1,
                g_interpreter->output(ERB_GRU_OUTPUT_STATE1_ORDINAL)->data.int8,
                ERB_GRU_INFERENCE_STATE_SIZE);
    std::memcpy(gain, g_interpreter->output(ERB_GRU_OUTPUT_GAIN_ORDINAL)->data.int8,
                ERB_GRU_INFERENCE_BANDS);
    return true;
}

extern "C" void erb_gru_inference_get_performance(
    erb_gru_inference_performance_t *performance)
{
    if (performance != nullptr)
    {
        *performance = {
            g_last_invoke_cycles,
            g_max_invoke_cycles,
            g_invoke_count,
            g_invoke_error_count,
            g_deadline_miss_count,
            g_arena_used_bytes,
        };
    }
}
