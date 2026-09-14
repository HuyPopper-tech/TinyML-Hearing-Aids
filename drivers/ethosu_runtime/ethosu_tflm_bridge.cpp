/**
 * @file ethosu_tflm_bridge.cpp
 * @brief TFLM custom-op bridge to the model-neutral Ethos-U runtime.
 * @author Bui Minh Huy
 * @date 2026-09-14
 */

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#include "drivers/ethosu_runtime/ethosu_runtime.h"
#include "flatbuffers/flexbuffers.h"
#include "tensorflow/lite/micro/kernels/kernel_util.h"
#include "tensorflow/lite/micro/memory_helpers.h"
#include "tensorflow/lite/micro/micro_context.h"
#include "tensorflow/lite/micro/micro_log.h"

namespace tflite {
namespace {

constexpr uint8_t kCoTypeEthosU = 1U;

struct OpData {
    int cms_data_size;
    int base_addr_idx;
    int base_addr_size_idx;
};

void *Init(TfLiteContext *context, const char *buffer, size_t length)
{
    (void)buffer;
    (void)length;
    TFLITE_DCHECK(context->AllocatePersistentBuffer != nullptr);
    return context->AllocatePersistentBuffer(context, sizeof(OpData));
}

TfLiteStatus Prepare(TfLiteContext *context, TfLiteNode *node)
{
    TFLITE_DCHECK(context != nullptr);
    TF_LITE_ENSURE(context, node != nullptr);
    TF_LITE_ENSURE(context, node->inputs != nullptr);
    TF_LITE_ENSURE(context, node->outputs != nullptr);
    TF_LITE_ENSURE(context, node->inputs->size > 0);
    TF_LITE_ENSURE(context, node->outputs->size <= INT_MAX - node->inputs->size);
    TFLITE_DCHECK(node->user_data != nullptr);
    TF_LITE_ENSURE(context, node->custom_initial_data_size > 0);

    OpData *data = static_cast<OpData *>(node->user_data);
    const int num_base_addr = node->inputs->size + node->outputs->size;
    const size_t base_count = static_cast<size_t>(num_base_addr);
    TF_LITE_ENSURE(context, base_count <= SIZE_MAX / sizeof(uint64_t));
    TF_LITE_ENSURE(context, base_count <= SIZE_MAX / sizeof(size_t));
    TF_LITE_ENSURE_STATUS(context->RequestScratchBufferInArena(
        context, base_count * sizeof(uint64_t), &data->base_addr_idx));
    TF_LITE_ENSURE_STATUS(context->RequestScratchBufferInArena(
        context, base_count * sizeof(size_t), &data->base_addr_size_idx));

    MicroContext *micro_context = GetMicroContext(context);
    TfLiteTensor *tensor = micro_context->AllocateTempInputTensor(node, 0);
    TF_LITE_ENSURE(context, tensor != nullptr);
    data->cms_data_size = tensor->bytes;
    micro_context->DeallocateTempTfLiteTensor(tensor);
    return kTfLiteOk;
}

TfLiteStatus Eval(TfLiteContext *context, TfLiteNode *node)
{
    TFLITE_DCHECK(context != nullptr);
    TFLITE_DCHECK(context->GetEvalTensor != nullptr);
    TFLITE_DCHECK(context->GetScratchBuffer != nullptr);
    TFLITE_DCHECK(node != nullptr);
    TFLITE_DCHECK(node->user_data != nullptr);

    const OpData *data = static_cast<const OpData *>(node->user_data);
    if ((node->inputs == nullptr) || (node->outputs == nullptr) ||
        (node->inputs->size <= 0) ||
        (node->outputs->size > INT_MAX - node->inputs->size)) {
        return kTfLiteError;
    }
    uint64_t *base_addresses = static_cast<uint64_t *>(
        context->GetScratchBuffer(context, data->base_addr_idx));
    size_t *base_sizes = static_cast<size_t *>(
        context->GetScratchBuffer(context, data->base_addr_size_idx));
    if ((base_addresses == nullptr) || (base_sizes == nullptr)) {
        return kTfLiteError;
    }

    const uint8_t *custom_data = static_cast<const uint8_t *>(
        node->custom_initial_data);
    if ((custom_data == nullptr) || (node->custom_initial_data_size == 0U)) {
        return kTfLiteError;
    }
    const auto root = flexbuffers::GetRoot(
        custom_data, node->custom_initial_data_size);
    if (root.AsInt8() != kCoTypeEthosU) {
        MicroPrintf("CO_TYPE != ETHOSU");
        return kTfLiteError;
    }

    TfLiteEvalTensor *tensor = context->GetEvalTensor(
        context, node->inputs->data[0]);
    if ((tensor == nullptr) || (tensor->data.uint8 == nullptr)) {
        return kTfLiteError;
    }
    const void *custom_operator_payload =
        static_cast<const void *>(tensor->data.uint8);

    int tensor_index = 1;
    int base_index = 0;
    for (; tensor_index < node->inputs->size; ++tensor_index, ++base_index) {
        tensor = context->GetEvalTensor(
            context, node->inputs->data[tensor_index]);
        if ((tensor == nullptr) || (tensor->data.uint8 == nullptr) ||
            (TfLiteEvalTensorByteLength(tensor, &base_sizes[base_index]) !=
             kTfLiteOk)) {
            return kTfLiteError;
        }
        base_addresses[base_index] =
            static_cast<uint64_t>(reinterpret_cast<uintptr_t>(
                tensor->data.uint8));
    }
    for (tensor_index = 0; tensor_index < node->outputs->size;
         ++tensor_index, ++base_index) {
        tensor = context->GetEvalTensor(
            context, node->outputs->data[tensor_index]);
        if ((tensor == nullptr) || (tensor->data.uint8 == nullptr) ||
            (TfLiteEvalTensorByteLength(tensor, &base_sizes[base_index]) !=
             kTfLiteOk)) {
            return kTfLiteError;
        }
        base_addresses[base_index] =
            static_cast<uint64_t>(reinterpret_cast<uintptr_t>(
                tensor->data.uint8));
    }

    ethosu_runtime_invocation_t invocation = {};
    invocation.custom_operator_payload = custom_operator_payload;
    invocation.custom_operator_payload_bytes = static_cast<size_t>(
        data->cms_data_size);
    invocation.base_addresses = base_addresses;
    invocation.base_sizes = base_sizes;
    invocation.base_count = static_cast<size_t>(base_index);
    ethosu_runtime_result_t result = {};
    return (ethosu_runtime_invoke(&invocation, &result) ==
            ETHOSU_RUNTIME_OK)
               ? kTfLiteOk
               : kTfLiteError;
}

}  // namespace

TFLMRegistration *Register_ETHOSU()
{
    static TFLMRegistration registration =
        tflite::micro::RegisterOp(Init, Prepare, Eval);
    return &registration;
}

const char *GetString_ETHOSU()
{
    return "ethos-u";
}

}  // namespace tflite