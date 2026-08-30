/**
 * @file neuralaid_inference.cpp
 * @brief NeuralAid U55 TFLM bridge.
 * @author Bui Minh Huy
 * @date 2026-08-23
 */

#include <cmath>
#include <cstring>
#include <new>

#include "common/sha256.h"
#include "drivers/audio_transport/audio_transport.h"
#include "drivers/ethosu_u55/ethosu_u55.h"
#include "models/neuralaid/generated/neuralaid_deploy_contract.h"
#include "models/neuralaid/generated/neuralaid_u55_vela_model_data.h"
#include "services/neuralaid/neuralaid_inference.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

namespace {

constexpr size_t kArenaBytes = 128U * 1024U;
/* Contract order: conv_history, dual_path_history, lstm_h_0..lstm_h_5,
 * lstm_c_0..lstm_c_5. Raw-copy applies to conv_history, dual_path_history,
 * lstm_h_3 and lstm_c_0; all other pairs use requantize_state. */
constexpr uint16_t kStateSizes[14] = {
    516, 8256, 1056, 1056, 1056, 1056, 1056,
    1056, 1056, 1056, 1056, 1056, 1056, 1056,
};
constexpr float kInputScale[15] = {
    0.0135895619f, 0.0135895619f, 0.0090530301f, 0.0009034820f,
    0.0021036710f, 0.0035252380f, 0.0016852400f, 0.0022394190f,
    0.0032754420f, 0.0018293993f, 0.0044894363f, 0.0062219724f,
    0.0035123362f, 0.0037404692f, 0.0066008512f,
};
constexpr int kInputZero[15] = {
    -15, -15, 11, -3, 40, -18, -16, 11, 1, 0, 30, -14, 2, -5, -28,
};
constexpr float kTfInputScale = 0.01358956191688776f;
constexpr int kTfInputZeroPoint = -15;
constexpr float kTfOutputScale = 0.012382527813315392f;
constexpr int kTfOutputZeroPoint = -37;
constexpr float kOutputScale[15] = {
    kTfOutputScale, 0.01358956191688776f, 0.009053030051290989f,
    0.001829399261623621f, 0.004489436745643616f, 0.006221972871571779f,
    0.0035123364068567753f, 0.0037404694594442844f, 0.006600852124392986f,
    0.0009034821996465325f, 0.0021036711987107992f, 0.00352523778565228f,
    0.001685239956714213f, 0.0022394193802028894f, 0.0032754414714872837f,
};
constexpr int kOutputZero[15] = {
    kTfOutputZeroPoint, -15, 11, 0, 30, -14, 2, -5, -28, -3, 40, -18, -16, 11, 1,
};
constexpr uint8_t kOutputForInput[14] = {
    1, 2, 9, 10, 11, 12, 13, 14, 3, 4, 5, 6, 7, 8,
};
alignas(16) uint8_t g_arena[kArenaBytes];
alignas(tflite::MicroMutableOpResolver<1>) uint8_t
    g_resolver_storage[sizeof(tflite::MicroMutableOpResolver<1>)];
alignas(tflite::MicroInterpreter) uint8_t
    g_interpreter_storage[sizeof(tflite::MicroInterpreter)];
tflite::MicroInterpreter *g_interpreter;
int g_status;
uint32_t g_cpu_operator_count;
uint32_t g_ethosu_operator_count;
int8_t g_requant_lut[14][256];
neuralaid_inference_diagnostics_t g_diagnostics;

int8_t saturate_int8(int32_t value) {
    return (value < -128) ? -128 :
           (value > 127) ? 127 : static_cast<int8_t>(value);
}
int8_t requantize_state(
    int8_t value,
    const TfLiteTensor *source,
    uint8_t input_ordinal) {
    const float real = (static_cast<float>(value) - source->params.zero_point) *
        source->params.scale;
    return saturate_int8(static_cast<int32_t>(std::round(
        real / kInputScale[input_ordinal] + kInputZero[input_ordinal])));
}

bool raw_copy_state(uint8_t state_ordinal) {
    return (state_ordinal == 0U) || (state_ordinal == 1U) ||
           (state_ordinal == 5U) || (state_ordinal == 8U);
}

void build_requant_lut(void) {
    for (uint8_t i = 0U; i < 14U; ++i) {
        const TfLiteTensor *source = g_interpreter->output(kOutputForInput[i]);

        if (raw_copy_state(i)) {
            continue;
        }
        for (int32_t value = -128; value <= 127; ++value) {
            g_requant_lut[i][value + 128] = requantize_state(
                static_cast<int8_t>(value), source, static_cast<uint8_t>(i + 1U));
        }
    }
}

uint32_t elapsed_us(uint32_t start_cycle) {
    return audio_transport_cycles_to_us(audio_transport_cycles_elapsed(
        start_cycle, audio_transport_cycles_now()));
}
bool valid_tensor(const TfLiteTensor *tensor, size_t bytes) {
    return (tensor != nullptr) && (tensor->type == kTfLiteInt8) &&
           (tensor->data.int8 != nullptr) && (tensor->bytes == bytes);
}

bool same_quantization(float left, float right) {
    return std::fabs(left - right) <= 1.0e-8f;
}

bool verify_tensor_contract(void) {
    for (uint8_t i = 0U; i < NEURALAID_MODEL_INPUT_COUNT; ++i) {
        const size_t bytes = (i == 0U) ? NEURALAID_TF_VALUES : kStateSizes[i - 1U];
        const TfLiteTensor *input = g_interpreter->input(i);
        const TfLiteTensor *output = g_interpreter->output(i);
        const size_t output_bytes = (i == 0U) ? NEURALAID_TF_VALUES :
            ((i == 1U) ? 516U : ((i == 2U) ? 8256U : 1056U));

        if (!valid_tensor(input, bytes) || !valid_tensor(output, output_bytes) ||
            !same_quantization(input->params.scale, kInputScale[i]) ||
            (input->params.zero_point != kInputZero[i]) ||
            !same_quantization(output->params.scale, kOutputScale[i]) ||
            (output->params.zero_point != kOutputZero[i])) {
            return false;
        }
    }
    return same_quantization(g_interpreter->input(0)->params.scale, kTfInputScale) &&
        (g_interpreter->input(0)->params.zero_point == kTfInputZeroPoint) &&
        same_quantization(g_interpreter->output(0)->params.scale, kTfOutputScale) &&
        (g_interpreter->output(0)->params.zero_point == kTfOutputZeroPoint);
}

bool verify_ethosu_only_model(const tflite::Model *model) {
    const tflite::SubGraph *subgraph = model->subgraphs()->Get(0);

    g_cpu_operator_count = 0U;
    g_ethosu_operator_count = 0U;
    for (uint32_t i = 0U; i < subgraph->operators()->size(); ++i) {
        const tflite::Operator *op = subgraph->operators()->Get(i);
        const tflite::OperatorCode *code =
            model->operator_codes()->Get(op->opcode_index());
        const flatbuffers::String *custom_code = code->custom_code();

        if ((code->builtin_code() == tflite::BuiltinOperator_CUSTOM) &&
            (custom_code != nullptr) &&
            (std::strcmp(custom_code->c_str(), "ethos-u") == 0)) {
            ++g_ethosu_operator_count;
        } else {
            ++g_cpu_operator_count;
        }
    }
    return (g_ethosu_operator_count != 0U) && (g_cpu_operator_count == 0U);
}

}  // namespace

extern "C" bool neuralaid_inference_init(void) {
    uint8_t digest[SHA256_DIGEST_BYTES];
    flatbuffers::Verifier verifier(g_neuralaid_model, g_neuralaid_model_len);
    if (g_interpreter) return true;
    sha256_compute(g_neuralaid_model, g_neuralaid_model_len, digest);
    if (!tflite::VerifyModelBuffer(verifier) || std::memcmp(digest, neuralaid_model_sha256, sizeof(digest)) != 0) { g_status = -101; return false; }
    const tflite::Model *model = tflite::GetModel(g_neuralaid_model);
    if (!model || !model->subgraphs() || model->subgraphs()->size() != 1U || !model->subgraphs()->Get(0)->inputs() || model->subgraphs()->Get(0)->inputs()->size() != NEURALAID_MODEL_INPUT_COUNT || !model->subgraphs()->Get(0)->outputs() || model->subgraphs()->Get(0)->outputs()->size() != NEURALAID_MODEL_OUTPUT_COUNT || !verify_ethosu_only_model(model)) { g_status = -102; return false; }
    if (!ethosu_u55_init()) { g_status = -103; return false; }
    auto *resolver = new (g_resolver_storage) tflite::MicroMutableOpResolver<1>();
    if (resolver->AddEthosU() != kTfLiteOk) { g_status = -104; return false; }
    g_interpreter = new (g_interpreter_storage) tflite::MicroInterpreter(model, *resolver, g_arena, sizeof(g_arena));
    if (g_interpreter->AllocateTensors() != kTfLiteOk) { g_interpreter = nullptr; g_status = -105; return false; }
    if (!verify_tensor_contract()) { g_interpreter = nullptr; g_status = -106; return false; }
    build_requant_lut();
    g_status = 1; return true;
}
extern "C" int neuralaid_inference_last_init_status(void) { return g_status; }
extern "C" void neuralaid_inference_get_model_diagnostics(
    neuralaid_model_diagnostics_t *diagnostics) {
    if (diagnostics != nullptr) {
        diagnostics->cpu_operator_count = g_cpu_operator_count;
        diagnostics->ethosu_operator_count = g_ethosu_operator_count;
    }
}
extern "C" void neuralaid_inference_get_diagnostics(
    neuralaid_inference_diagnostics_t *diagnostics) {
    if (diagnostics != nullptr) {
        *diagnostics = g_diagnostics;
    }
}
extern "C" void neuralaid_inference_reset(neuralaid_stream_state_t *state) { if (state) for (uint16_t i=0,offset=0;i<14;++i) { std::memset(state->bytes+offset, kInputZero[i+1], kStateSizes[i]); offset+=kStateSizes[i]; } }
extern "C" bool neuralaid_inference_invoke(const int8_t tf_frame[NEURALAID_TF_VALUES], neuralaid_stream_state_t *state, int8_t enhanced_tf[NEURALAID_TF_VALUES]) {
    uint32_t stage_start;
    if (!g_interpreter || !tf_frame || !state || !enhanced_tf) return false;
    stage_start = audio_transport_cycles_now();
    uint16_t offset=0; for(uint8_t i=0;i<14;++i){ std::memcpy(g_interpreter->input(i+1)->data.int8,state->bytes+offset,kStateSizes[i]); offset+=kStateSizes[i]; }
    std::memcpy(g_interpreter->input(0)->data.int8,tf_frame,NEURALAID_TF_VALUES);
    g_diagnostics.input_state_copy_us = elapsed_us(stage_start);
    stage_start = audio_transport_cycles_now();
    if(g_interpreter->Invoke()!=kTfLiteOk) {
        g_diagnostics.invoke_only_us = elapsed_us(stage_start);
        return false;
    }
    g_diagnostics.invoke_only_us = elapsed_us(stage_start);
    stage_start = audio_transport_cycles_now();
    std::memcpy(enhanced_tf,g_interpreter->output(0)->data.int8,NEURALAID_TF_VALUES);
    offset=0; for(uint8_t i=0;i<14;++i){ const TfLiteTensor *out=g_interpreter->output(kOutputForInput[i]); for(uint16_t j=0;j<kStateSizes[i];++j) state->bytes[offset+j]=raw_copy_state(i)?out->data.int8[j]:g_requant_lut[i][static_cast<uint8_t>(out->data.int8[j]+128)]; offset+=kStateSizes[i]; }
    g_diagnostics.output_state_us = elapsed_us(stage_start);
    return true;
}
