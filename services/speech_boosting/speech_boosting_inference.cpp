/**
 * @file speech_boosting_inference.cpp
 * @brief Static contract-checked TensorFlow Lite Micro and Ethos-U55 bridge.
 * @author Bui Minh Huy
 * @date 2026-08-27
 */

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

#include "common/sha256.h"
#include "drivers/audio_transport/audio_transport.h"
#include "drivers/ethosu_u55/ethosu_u55.h"
#include "speech_boosting_deploy_contract.h"
#include "speech_boosting_generated_contract.h"
#include "speech_boosting_model_contract.h"
#include "speech_boosting_u55_vela_model_data.h"
#include "services/speech_boosting/speech_boosting_inference.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

namespace {

enum speech_boosting_inference_init_status_t {
    k_init_not_run = 0,
    k_init_ready = 1,
    k_init_model_verify_failed = -1,
    k_init_model_contract_failed = -2,
    k_init_ethosu_failed = -3,
    k_init_resolver_failed = -4,
    k_init_arena_failed = -5,
    k_init_tensor_contract_failed = -6,
};

constexpr int32_t k_audio_input_shape[4] = {1, 1, 64, 2};
constexpr int32_t k_audio_output_shape[4] = {1, 1, 64, 1};

#ifndef M55M1_SPEECH_BOOSTING_MODEL_IN_SRAM
#define M55M1_SPEECH_BOOSTING_MODEL_IN_SRAM 0
#endif

#if M55M1_SPEECH_BOOSTING_MODEL_IN_SRAM
#define SPEECH_BOOSTING_TENSOR_ARENA_SECTION \
    __attribute__((section(".bss.U55_TENSOR_ARENA")))
#else
#define SPEECH_BOOSTING_TENSOR_ARENA_SECTION
#endif

#define SPEECH_BOOSTING_DTCM_BSS __attribute__((section(".bss.DTCM.ZeroInit")))

alignas(16) SPEECH_BOOSTING_TENSOR_ARENA_SECTION
    uint8_t g_tensor_arena[SPEECH_BOOSTING_TENSOR_ARENA_BYTES];
alignas(tflite::MicroMutableOpResolver<1>)
    uint8_t g_resolver_storage[sizeof(tflite::MicroMutableOpResolver<1>)]
        SPEECH_BOOSTING_DTCM_BSS;
alignas(tflite::MicroInterpreter)
    uint8_t g_interpreter_storage[sizeof(tflite::MicroInterpreter)]
        SPEECH_BOOSTING_DTCM_BSS;

tflite::MicroMutableOpResolver<1> *g_resolver = nullptr;
tflite::MicroInterpreter *g_interpreter = nullptr;
speech_boosting_inference_init_status_t g_init_status = k_init_not_run;
speech_boosting_inference_diagnostics_t g_diagnostics;

uint32_t elapsed_microseconds(uint32_t start_cycle) {
    return audio_transport_cycles_to_us(
        audio_transport_cycles_elapsed(start_cycle, audio_transport_cycles_now()));
}

void prepare_model_for_ethosu(void) {
#if M55M1_SPEECH_BOOSTING_MODEL_IN_SRAM
    ethosu_flush_dcache(
        reinterpret_cast<uint32_t *>(g_speech_boosting_u55_vela_model),
        g_speech_boosting_u55_vela_model_len);
#endif
}

bool model_hash_valid() {
    uint8_t digest[SHA256_DIGEST_BYTES];

    sha256_compute(
        g_speech_boosting_u55_vela_model,
        g_speech_boosting_u55_vela_model_len,
        digest);
    return std::memcmp(
               digest,
               SPEECH_BOOSTING_VELA_MODEL_SHA256,
               sizeof(digest)) == 0;
}

bool model_contract_valid(const tflite::Model *model) {
    const tflite::SubGraph *subgraph;
    const tflite::OperatorCode *operator_code;

    if ((model == nullptr) ||
        (g_speech_boosting_u55_vela_model_len !=
         SPEECH_BOOSTING_VELA_MODEL_BYTES) ||
        (model->version() != TFLITE_SCHEMA_VERSION) ||
        (model->subgraphs() == nullptr) || (model->subgraphs()->size() != 1U) ||
        (model->operator_codes() == nullptr) ||
        (model->operator_codes()->size() != 1U)) {
        return false;
    }

    subgraph = model->subgraphs()->Get(0);
    operator_code = model->operator_codes()->Get(0);
    if ((subgraph == nullptr) || (operator_code == nullptr) ||
        (subgraph->inputs() == nullptr) || (subgraph->outputs() == nullptr) ||
        (subgraph->operators() == nullptr) ||
        (subgraph->inputs()->size() != SPEECH_BOOSTING_INPUT_TENSOR_COUNT) ||
        (subgraph->outputs()->size() != SPEECH_BOOSTING_OUTPUT_TENSOR_COUNT) ||
        (subgraph->operators()->size() == 0U) ||
        (operator_code->builtin_code() != tflite::BuiltinOperator_CUSTOM) ||
        (operator_code->custom_code() == nullptr) ||
        (std::strcmp(operator_code->custom_code()->c_str(), "ethos-u") != 0)) {
        return false;
    }

    for (uint32_t index = 0U; index < subgraph->operators()->size(); ++index) {
        const tflite::Operator *operation = subgraph->operators()->Get(index);

        if ((operation == nullptr) || (operation->opcode_index() != 0U)) {
            return false;
        }
    }
    return true;
}

bool tensor_valid(
    const TfLiteTensor *tensor,
    uint32_t bytes,
    float scale,
    int32_t zero_point,
    const int32_t shape[4],
    uint32_t rank) {
    if ((tensor == nullptr) || (tensor->type != kTfLiteInt8) ||
        (tensor->data.int8 == nullptr) || (tensor->dims == nullptr) ||
        (tensor->dims->size != static_cast<int>(rank)) || (tensor->bytes != bytes) ||
        (tensor->params.scale != scale) || (tensor->params.zero_point != zero_point)) {
        return false;
    }
    for (uint32_t dimension = 0U; dimension < rank; ++dimension) {
        if (tensor->dims->data[dimension] != shape[dimension]) {
            return false;
        }
    }
    return true;
}

bool state_tensor_valid(const TfLiteTensor *tensor, uint32_t state_index, bool input) {
    return tensor_valid(
        tensor,
        SPEECH_BOOSTING_STATE_BYTES_PER_TENSOR[state_index],
        input ? SPEECH_BOOSTING_STATE_INPUT_SCALE[state_index]
              : SPEECH_BOOSTING_STATE_OUTPUT_SCALE[state_index],
        input ? SPEECH_BOOSTING_STATE_INPUT_QUANTIZATION_ZERO_POINT[state_index]
              : SPEECH_BOOSTING_STATE_OUTPUT_ZERO_POINT[state_index],
        SPEECH_BOOSTING_STATE_TENSOR_SHAPES[state_index],
        SPEECH_BOOSTING_STATE_TENSOR_RANK[state_index]);
}

bool ordinal_mapping_valid(void) {
    bool seen_inputs[SPEECH_BOOSTING_INPUT_TENSOR_COUNT] = {false};
    bool seen_outputs[SPEECH_BOOSTING_OUTPUT_TENSOR_COUNT] = {false};

    seen_inputs[SPEECH_BOOSTING_AUDIO_INPUT_INDEX] = true;
    seen_outputs[SPEECH_BOOSTING_AUDIO_OUTPUT_INDEX] = true;
    for (uint32_t index = 0U; index < SPEECH_BOOSTING_STATE_TENSOR_COUNT; ++index) {
        const uint32_t input_ordinal = SPEECH_BOOSTING_STATE_INPUT_INDEX[index];
        const uint32_t output_ordinal = SPEECH_BOOSTING_STATE_OUTPUT_INDEX[index];

        if ((input_ordinal >= SPEECH_BOOSTING_INPUT_TENSOR_COUNT) ||
            (output_ordinal >= SPEECH_BOOSTING_OUTPUT_TENSOR_COUNT) ||
            seen_inputs[input_ordinal] || seen_outputs[output_ordinal]) {
            return false;
        }
        seen_inputs[input_ordinal] = true;
        seen_outputs[output_ordinal] = true;
    }
    for (uint32_t ordinal = 0U; ordinal < SPEECH_BOOSTING_INPUT_TENSOR_COUNT; ++ordinal) {
        if (!seen_inputs[ordinal] || !seen_outputs[ordinal]) {
            return false;
        }
    }
    return true;
}

bool tensor_contract_valid() {
    if ((g_interpreter == nullptr) ||
        (g_interpreter->inputs_size() != SPEECH_BOOSTING_INPUT_TENSOR_COUNT) ||
        (g_interpreter->outputs_size() != SPEECH_BOOSTING_OUTPUT_TENSOR_COUNT) ||
        !ordinal_mapping_valid() ||
        !tensor_valid(
            g_interpreter->input(SPEECH_BOOSTING_AUDIO_INPUT_INDEX),
            SPEECH_BOOSTING_CHUNK_SAMPLES * SPEECH_BOOSTING_INPUT_CHANNELS,
            SPEECH_BOOSTING_AUDIO_INPUT_SCALE,
            SPEECH_BOOSTING_AUDIO_INPUT_ZERO_POINT,
            k_audio_input_shape,
            4U) ||
        !tensor_valid(
            g_interpreter->output(SPEECH_BOOSTING_AUDIO_OUTPUT_INDEX),
            SPEECH_BOOSTING_CHUNK_SAMPLES * SPEECH_BOOSTING_OUTPUT_CHANNELS,
            SPEECH_BOOSTING_AUDIO_OUTPUT_SCALE,
            SPEECH_BOOSTING_AUDIO_OUTPUT_ZERO_POINT,
            k_audio_output_shape,
            4U)) {
        return false;
    }

    for (uint32_t index = 0U; index < SPEECH_BOOSTING_STATE_TENSOR_COUNT; ++index) {
        if (!state_tensor_valid(
                g_interpreter->input(SPEECH_BOOSTING_STATE_INPUT_INDEX[index]),
                index,
                true) ||
            !state_tensor_valid(
                g_interpreter->output(SPEECH_BOOSTING_STATE_OUTPUT_INDEX[index]),
                index,
                false)) {
            return false;
        }
    }
    return true;
}

int8_t requantize_state(int8_t output_value, uint32_t state_index) {
    const float real_value =
        (static_cast<float>(output_value) -
         static_cast<float>(SPEECH_BOOSTING_STATE_OUTPUT_ZERO_POINT[state_index])) *
        SPEECH_BOOSTING_STATE_OUTPUT_SCALE[state_index];
    const int32_t input_value = static_cast<int32_t>(std::round(
        real_value / SPEECH_BOOSTING_STATE_INPUT_SCALE[state_index] +
        static_cast<float>(
            SPEECH_BOOSTING_STATE_INPUT_QUANTIZATION_ZERO_POINT[state_index])));

    if (input_value < -128) {
        return -128;
    }
    if (input_value > 127) {
        return 127;
    }
    return static_cast<int8_t>(input_value);
}

void copy_state_inputs(const speech_boosting_stream_state_t *state) {
    uint32_t offset = 0U;

    for (uint32_t index = 0U; index < SPEECH_BOOSTING_STATE_TENSOR_COUNT; ++index) {
        std::memcpy(
            g_interpreter->input(SPEECH_BOOSTING_STATE_INPUT_INDEX[index])->data.int8,
            state->bytes + offset,
            SPEECH_BOOSTING_STATE_BYTES_PER_TENSOR[index]);
        offset += SPEECH_BOOSTING_STATE_BYTES_PER_TENSOR[index];
    }
}

void copy_state_outputs(speech_boosting_stream_state_t *state) {
    uint32_t offset = 0U;

    for (uint32_t index = 0U; index < SPEECH_BOOSTING_STATE_TENSOR_COUNT; ++index) {
        const int8_t *output =
            g_interpreter->output(SPEECH_BOOSTING_STATE_OUTPUT_INDEX[index])->data.int8;
        int8_t *destination = state->bytes + offset;

        for (uint32_t byte = 0U;
             byte < SPEECH_BOOSTING_STATE_BYTES_PER_TENSOR[index];
             ++byte) {
            destination[byte] = requantize_state(output[byte], index);
        }
        offset += SPEECH_BOOSTING_STATE_BYTES_PER_TENSOR[index];
    }
}

}  // namespace

extern "C" bool speech_boosting_inference_init(void) {
    const tflite::Model *model;
    flatbuffers::Verifier verifier(
        reinterpret_cast<const uint8_t *>(g_speech_boosting_u55_vela_model),
        g_speech_boosting_u55_vela_model_len);

    if (g_interpreter != nullptr) {
        g_init_status = k_init_ready;
        return true;
    }
    if (!tflite::VerifyModelBuffer(verifier) || !model_hash_valid()) {
        g_init_status = k_init_model_verify_failed;
        return false;
    }
    model = tflite::GetModel(g_speech_boosting_u55_vela_model);
    if (!model_contract_valid(model)) {
        g_init_status = k_init_model_contract_failed;
        return false;
    }
    if (!ethosu_u55_init()) {
        g_init_status = k_init_ethosu_failed;
        return false;
    }
    prepare_model_for_ethosu();

    g_resolver = new (g_resolver_storage) tflite::MicroMutableOpResolver<1>();
    if ((g_resolver == nullptr) || (g_resolver->AddEthosU() != kTfLiteOk)) {
        g_resolver = nullptr;
        g_init_status = k_init_resolver_failed;
        return false;
    }
    g_interpreter = new (g_interpreter_storage) tflite::MicroInterpreter(
        model, *g_resolver, g_tensor_arena, sizeof(g_tensor_arena));
    if ((g_interpreter == nullptr) ||
        (g_interpreter->AllocateTensors() != kTfLiteOk)) {
        g_interpreter = nullptr;
        g_init_status = k_init_arena_failed;
        return false;
    }
    if (!tensor_contract_valid()) {
        g_interpreter = nullptr;
        g_init_status = k_init_tensor_contract_failed;
        return false;
    }

    g_diagnostics.tensor_arena_capacity_bytes = sizeof(g_tensor_arena);
    g_diagnostics.tensor_arena_used_bytes =
        static_cast<uint32_t>(g_interpreter->arena_used_bytes());
    g_diagnostics.model_start_address =
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_speech_boosting_u55_vela_model));
    g_diagnostics.model_end_address =
        g_diagnostics.model_start_address + g_speech_boosting_u55_vela_model_len;
    g_diagnostics.tensor_arena_start_address =
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_tensor_arena));
    g_diagnostics.tensor_arena_end_address =
        g_diagnostics.tensor_arena_start_address + sizeof(g_tensor_arena);
    g_diagnostics.cpu_operator_count = SPEECH_BOOSTING_CPU_OPERATOR_COUNT;
    g_diagnostics.ethosu_operator_count = SPEECH_BOOSTING_ETHOSU_OPERATOR_COUNT;
    g_init_status = k_init_ready;
    return true;
}

extern "C" int speech_boosting_inference_last_init_status(void) {
    return static_cast<int>(g_init_status);
}

extern "C" void speech_boosting_inference_reset(
    speech_boosting_stream_state_t *state) {
    uint32_t offset = 0U;

    if (state == nullptr) {
        return;
    }
    for (uint32_t index = 0U; index < SPEECH_BOOSTING_STATE_TENSOR_COUNT; ++index) {
        std::memset(
            state->bytes + offset,
            SPEECH_BOOSTING_STATE_INPUT_QUANTIZATION_ZERO_POINT[index],
            SPEECH_BOOSTING_STATE_BYTES_PER_TENSOR[index]);
        offset += SPEECH_BOOSTING_STATE_BYTES_PER_TENSOR[index];
    }
}

extern "C" bool speech_boosting_inference_invoke(
    const int8_t input[SPEECH_BOOSTING_CHUNK_SAMPLES]
                      [SPEECH_BOOSTING_INPUT_CHANNELS],
    speech_boosting_stream_state_t *state,
    int8_t output[SPEECH_BOOSTING_CHUNK_SAMPLES]) {
    uint32_t start_cycle;

    if ((g_interpreter == nullptr) || (input == nullptr) || (state == nullptr) ||
        (output == nullptr) || !tensor_contract_valid()) {
        return false;
    }

    std::memcpy(
        g_interpreter->input(SPEECH_BOOSTING_AUDIO_INPUT_INDEX)->data.int8,
        input,
        SPEECH_BOOSTING_CHUNK_SAMPLES * SPEECH_BOOSTING_INPUT_CHANNELS);
    start_cycle = audio_transport_cycles_now();
    copy_state_inputs(state);
    g_diagnostics.input_state_tensor_copy_microseconds =
        elapsed_microseconds(start_cycle);

    start_cycle = audio_transport_cycles_now();
    if (g_interpreter->Invoke() != kTfLiteOk) {
        speech_boosting_inference_reset(state);
        return false;
    }
    g_diagnostics.neural_network_invocation_microseconds =
        elapsed_microseconds(start_cycle);

    start_cycle = audio_transport_cycles_now();
    std::memcpy(
        output,
        g_interpreter->output(SPEECH_BOOSTING_AUDIO_OUTPUT_INDEX)->data.int8,
        SPEECH_BOOSTING_CHUNK_SAMPLES);
    copy_state_outputs(state);
    g_diagnostics.output_state_tensor_copy_microseconds =
        elapsed_microseconds(start_cycle);
    return true;
}

extern "C" void speech_boosting_inference_get_diagnostics(
    speech_boosting_inference_diagnostics_t *diagnostics) {
    if (diagnostics != nullptr) {
        *diagnostics = g_diagnostics;
    }
}
