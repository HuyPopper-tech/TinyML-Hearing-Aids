/**
 * @file deepfir_inference_service.cpp
 * @brief Single-interpreter DeepFIR service with SRAM model and cache-aware runtime.
 * @author Bui Minh Huy
 * @date 2026-09-23
 */

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#if M55M1_RUNTIME_TELEMETRY
#include <cstdio>
#endif

#include "drivers/audio_transport/audio_transport.h"
#if M55M1_RUNTIME_TELEMETRY
#include "drivers/debug_uart_tx/debug_uart_tx.h"
#endif
#include "drivers/ethosu_runtime/ethosu_runtime.h"
#include "services/deepfir/deepfir_model_binding.h"
#include "services/deepfir/deepfir_cmsis_fft_adapter.h"
#include "services/deepfir/deepfir_inference_service.h"
#include "services/deepfir/deepfir_model_package.h"
#include "services/deepfir/deepfir_model_adapter.h"
#include "services/deepfir/deepfir_tensor_binding.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

#ifndef M55M1_DEEPFIR_MVE_SELF_TEST
#define M55M1_DEEPFIR_MVE_SELF_TEST 0
#endif

namespace {

constexpr size_t DEEPFIR_TENSOR_ARENA_BYTES = 64U * 1024U;

// Single foreground owner: storage and histories live until shutdown; ISR never invokes.
alignas(32) static uint8_t g_tensor_arena[DEEPFIR_TENSOR_ARENA_BYTES];
alignas(tflite::MicroInterpreter) static uint8_t
    g_interpreter_storage[sizeof(tflite::MicroInterpreter)];
static tflite::MicroInterpreter *g_interpreter;
static const tflite::Model *g_model;
alignas(tflite::MicroMutableOpResolver<1>) static uint8_t
    g_resolver_storage[sizeof(tflite::MicroMutableOpResolver<1>)];
static tflite::MicroMutableOpResolver<1> *g_resolver;
static TfLiteTensor *g_inputs[DEEPFIR_MODEL_INPUT_COUNT];
static TfLiteTensor *g_outputs[DEEPFIR_MODEL_OUTPUT_COUNT];
static deepfir_model_adapter_t g_adapter;
static deepfir_feature_state_t g_feature_state;
static deepfir_cmsis_fft_context_t g_fft_context;
static deepfir_inference_timing_t g_last_timing;
static bool g_initialized;
static bool g_faulted;
static bool g_processing;
static bool g_feature_mve_ready;
static bool g_fir_mve_ready;
static uint32_t g_mve_self_test_us;
#if M55M1_RUNTIME_TELEMETRY
// Only cold initialization drains UART. Runtime breadcrumbs never wait for TX.
void trace_init(const char *stage, const char *status, int code) {
    if (g_initialized) {
        return;
    }
    char message[160];
    const int length = std::snprintf(message, sizeof(message),
                                     "[INIT] stage=%s status=%s code=%d\r\n", stage, status, code);
    if (length > 0 && static_cast<size_t>(length) < sizeof(message)) {
        (void)debug_uart_tx_try_enqueue(message, static_cast<size_t>(length));
        debug_uart_tx_flush_blocking();
    }
}

enum deepfir_hop_stage_t {
    DEEPFIR_HOP_FRONTEND_BEGIN,
    DEEPFIR_HOP_FFT_BEGIN,
    DEEPFIR_HOP_FFT_END,
    DEEPFIR_HOP_FRONTEND_END,
    DEEPFIR_HOP_INPUTS_READY,
    DEEPFIR_HOP_OUTPUTS_READY,
    DEEPFIR_HOP_FEEDBACK_DONE,
    DEEPFIR_HOP_TAPS_DONE,
    DEEPFIR_HOP_FIR_DONE,
    DEEPFIR_HOP_INVOKE_BEGIN,
    DEEPFIR_HOP_INVOKE_END,
    DEEPFIR_HOP_ADAPTER_FAIL,
    DEEPFIR_HOP_PCM_NONFINITE,
    DEEPFIR_HOP_PCM_DONE,
};

struct deepfir_hop_event_t {
    deepfir_hop_stage_t stage;
    int status;
    uint32_t cycle;
};

// Foreground owns one bounded trace of the latest hop, retained through stream reset.
static deepfir_hop_event_t g_hop_events[16];
static unsigned g_hop_event_count;
static unsigned g_hop_reported_events;
static bool g_hop_trace_overflow;

void trace_hop(deepfir_hop_stage_t stage, int status) {
#if M55M1_DEEPFIR_COMPILED_REPLAY
    (void)stage;
    (void)status;
#else
    // Boot self-tests do not own a live PCM hop and must not overwrite failure evidence.
    if (!g_initialized || !g_processing) {
        return;
    }
    unsigned index = g_hop_event_count;
    if (index < sizeof(g_hop_events) / sizeof(g_hop_events[0])) {
        ++g_hop_event_count;
    } else {
        // Keep the last failure even if a future change adds too many stage marks.
        index = g_hop_event_count - 1U;
        g_hop_trace_overflow = true;
    }
    g_hop_events[index] = {stage, status, audio_transport_cycles_now()};
#endif
}

void trace_report_hop(void) {
    if (g_processing) {
        return;
    }
    static const char *const names[] = {
        "frontend.begin", "fft.begin",     "fft.end",       "frontend.end", "inputs_ready",
        "outputs_ready",  "feedback_done", "taps_done",     "fir_done",     "invoke.begin",
        "invoke.end",     "adapter.fail",  "pcm.nonfinite", "pcm.done",
    };
    if (g_hop_reported_events < g_hop_event_count) {
        // The caller has stopped DMA: drain here so a full ring cannot hide the failure.
        debug_uart_tx_flush_blocking();
    }
    while (g_hop_reported_events < g_hop_event_count) {
        const auto &event = g_hop_events[g_hop_reported_events];
        char message[128];
        const int length = std::snprintf(
            message, sizeof(message), "[HOP_STAGE] stage=%s code=%d cycle=%lu overflow=%u\r\n",
            names[event.stage], event.status, static_cast<unsigned long>(event.cycle),
            g_hop_trace_overflow ? 1U : 0U);
        if (length <= 0 || static_cast<size_t>(length) >= sizeof(message) ||
            !debug_uart_tx_try_enqueue(message, static_cast<size_t>(length))) {
            return;
        }
        ++g_hop_reported_events;
        debug_uart_tx_flush_blocking();
    }
}

void trace_tensor(bool input, unsigned semantic, const char *kind,
                  const deepfir_tensor_descriptor_t *descriptor) {
    if (descriptor == nullptr || g_initialized) {
        return;
    }
    uint32_t scale_bits = 0U;
    std::memcpy(&scale_bits, &descriptor->scale, sizeof(scale_bits));
    char message[384];
    const int length = std::snprintf(
        message, sizeof(message),
        "[INIT_TENSOR] direction=%s semantic=%u kind=%s port=%u index=%d "
        "int16=%u rank=%u shape=%d,%d,%d,%d bytes=%lu scale_bits=%08lx zp=%ld name=%s\r\n",
        input ? "input" : "output", semantic, kind, descriptor->port, descriptor->tensor_index,
        descriptor->int16_type ? 1U : 0U, descriptor->rank, descriptor->shape[0],
        descriptor->shape[1], descriptor->shape[2], descriptor->shape[3],
        static_cast<unsigned long>(descriptor->bytes), static_cast<unsigned long>(scale_bits),
        static_cast<long>(descriptor->zero_point), descriptor->name ? descriptor->name : "(null)");
    if (length > 0 && static_cast<size_t>(length) < sizeof(message)) {
        (void)debug_uart_tx_try_enqueue(message, static_cast<size_t>(length));
        debug_uart_tx_flush_blocking();
    }
}
#define DEEPFIR_TRACE_INIT(stage, status, code) trace_init(stage, status, code)
#define DEEPFIR_TRACE_HOP(stage, status) trace_hop(stage, status)
#else
#define DEEPFIR_TRACE_INIT(stage, status, code) ((void)0)
#define DEEPFIR_TRACE_HOP(stage, status) ((void)0)
#endif
#if M55M1_RUNTIME_TELEMETRY || M55M1_AUDIO_RELEASE_PROBE
static uint32_t g_feature_start_cycle;
static uint32_t g_feature_fft_end_cycle;
static bool g_feature_fft_called;
static uint32_t g_adapter_stage_cycles[DEEPFIR_PROBE_STAGE_COUNT];
#endif

bool in_sram(const void *data, size_t bytes) {
    return ethosu_runtime_memory_is_accessible(data, bytes);
}

const char *tensor_name(const tflite::Model *model, int32_t index) {
    if (model == nullptr || model->subgraphs() == nullptr || model->subgraphs()->size() == 0U) {
        return nullptr;
    }
    const auto *subgraph = model->subgraphs()->Get(0);
    if (subgraph == nullptr || subgraph->tensors() == nullptr || index < 0 ||
        static_cast<size_t>(index) >= subgraph->tensors()->size()) {
        return nullptr;
    }
    const auto *tensor = subgraph->tensors()->Get(static_cast<size_t>(index));
    return tensor != nullptr && tensor->name() != nullptr ? tensor->name()->c_str() : nullptr;
}

TfLiteTensor *find_tensor(bool input, unsigned semantic) {
    const auto *expected = deepfir_tensor_expected(input, semantic);
    const auto &indices = input ? g_interpreter->inputs() : g_interpreter->outputs();
    const size_t count = input ? g_interpreter->inputs_size() : g_interpreter->outputs_size();
    if (expected == nullptr || expected->port >= count) {
        DEEPFIR_TRACE_INIT("tensor.port", "FAIL", static_cast<int>(semantic));
        return nullptr;
    }
#if M55M1_RUNTIME_TELEMETRY
    trace_tensor(input, semantic, "expected", expected);
#endif
    TfLiteTensor *tensor =
        input ? g_interpreter->input(expected->port) : g_interpreter->output(expected->port);
    if (!tensor || !tensor->dims || tensor->dims->size < 0 || tensor->dims->size > 4 ||
        !tensor->data.i16 || !in_sram(tensor->data.i16, tensor->bytes)) {
        DEEPFIR_TRACE_INIT("tensor.storage_or_rank", "FAIL", static_cast<int>(expected->port));
        return nullptr;
    }
    deepfir_tensor_descriptor_t actual = {};
    actual.port = expected->port;
    actual.tensor_index = indices.Get(expected->port);
    actual.name = tensor_name(g_model, actual.tensor_index);
    actual.rank = static_cast<unsigned>(tensor->dims->size);
    actual.bytes = tensor->bytes;
    actual.scale = tensor->params.scale;
    actual.zero_point = tensor->params.zero_point;
    actual.int16_type = tensor->type == kTfLiteInt16;
    for (unsigned i = 0; i < actual.rank; ++i) {
        actual.shape[i] = tensor->dims->data[i];
    }
#if M55M1_RUNTIME_TELEMETRY
    trace_tensor(input, semantic, "actual", &actual);
#endif
    const bool valid = deepfir_tensor_validate(input, semantic, &actual);
    DEEPFIR_TRACE_INIT(input ? "tensor.input" : "tensor.output", valid ? "OK" : "FAIL",
                       static_cast<int>(expected->port));
    return valid ? tensor : nullptr;
}

uint32_t elapsed_us(uint32_t start, uint32_t end) {
    return audio_transport_cycles_to_us(audio_transport_cycles_elapsed(start, end));
}

#if M55M1_RUNTIME_TELEMETRY || M55M1_AUDIO_RELEASE_PROBE
void adapter_probe_mark(uint32_t stage, void *context) {
    (void)context;
    if (stage < DEEPFIR_PROBE_STAGE_COUNT) {
        g_adapter_stage_cycles[stage] = audio_transport_cycles_now();
#if M55M1_RUNTIME_TELEMETRY
        DEEPFIR_TRACE_HOP(static_cast<deepfir_hop_stage_t>(DEEPFIR_HOP_INPUTS_READY + stage), 0);
#endif
    }
}

int timed_fft_callback(float *buffer, uint32_t length, uint8_t inverse, void *context) {
    DEEPFIR_TRACE_HOP(DEEPFIR_HOP_FFT_BEGIN, 0);
    const uint32_t start = audio_transport_cycles_now();
    g_last_timing.pre_fft_us = elapsed_us(g_feature_start_cycle, start);
    const int status = deepfir_cmsis_fft_callback(buffer, length, inverse, context);
    g_feature_fft_end_cycle = audio_transport_cycles_now();
    g_last_timing.fft_us = elapsed_us(start, g_feature_fft_end_cycle);
    g_feature_fft_called = true;
    DEEPFIR_TRACE_HOP(DEEPFIR_HOP_FFT_END, status);
    return status;
}
#endif

int feature_callback(const float *pcm, uint32_t sample_count, int16_t *feature, void *context) {
    DEEPFIR_TRACE_HOP(DEEPFIR_HOP_FRONTEND_BEGIN, 0);
    const uint32_t start = audio_transport_cycles_now();
#if M55M1_RUNTIME_TELEMETRY || M55M1_AUDIO_RELEASE_PROBE
    g_feature_start_cycle = start;
    g_feature_fft_called = false;
    g_last_timing.pre_fft_us = 0U;
    g_last_timing.fft_us = 0U;
    g_last_timing.post_fft_us = 0U;
#endif
    const int status = deepfir_feature_float(pcm, sample_count, feature, context);
    const uint32_t end = audio_transport_cycles_now();
    g_last_timing.feature_us = elapsed_us(start, end);
    DEEPFIR_TRACE_HOP(DEEPFIR_HOP_FRONTEND_END, status);
#if M55M1_RUNTIME_TELEMETRY || M55M1_AUDIO_RELEASE_PROBE
    if (g_feature_fft_called) {
        g_last_timing.post_fft_us = elapsed_us(g_feature_fft_end_cycle, end);
    }
#endif
    return status;
}

int infer_callback(const deepfir_tensor_map_t *inputs, uint32_t input_count,
                   deepfir_tensor_map_t *outputs, uint32_t output_count, void *context) {
    (void)context;
    if (g_interpreter == nullptr || inputs == nullptr || outputs == nullptr ||
        input_count != DEEPFIR_MODEL_INPUT_COUNT || output_count != DEEPFIR_MODEL_OUTPUT_COUNT) {
        return -1;
    }
    for (uint32_t index = 0U; index < input_count; ++index) {
        std::memcpy(g_inputs[index]->data.i16, inputs[index].data,
                    inputs[index].elements * sizeof(int16_t));
    }
    DEEPFIR_TRACE_HOP(DEEPFIR_HOP_INVOKE_BEGIN, 0);
    const uint32_t start = audio_transport_cycles_now();
    const TfLiteStatus status = g_interpreter->Invoke();
    const uint32_t end = audio_transport_cycles_now();
    g_last_timing.invoke_us = elapsed_us(start, end);
    DEEPFIR_TRACE_HOP(DEEPFIR_HOP_INVOKE_END, static_cast<int>(status));

    ethosu_runtime_stats_t runtime_stats = {};
    if (ethosu_runtime_get_stats(&runtime_stats) == ETHOSU_RUNTIME_OK) {
        g_last_timing.npu_us = audio_transport_cycles_to_us(runtime_stats.last_duration_cycles);
    } else {
        g_last_timing.npu_us = 0U;
    }
    g_last_timing.invoke_overhead_us = g_last_timing.invoke_us > g_last_timing.npu_us
                                           ? g_last_timing.invoke_us - g_last_timing.npu_us
                                           : 0U;
    if (status != kTfLiteOk) {
        return -2;
    }
    for (uint32_t index = 0U; index < output_count; ++index) {
        outputs[index].data = g_outputs[index]->data.i16;
    }
    return 0;
}

} // namespace

#if M55M1_DEEPFIR_COMPILED_REPLAY
/* Debugger produces feature/request; foreground produces outputs/response.
 * Existing board MPU maps this linker section uncached. Only replay owns it;
 * audio DMA stays stopped. Wire ABI: tools/replay_deepfir_r2.py, little endian. */
struct deepfir_replay_mailbox_t {
    uint32_t magic, request, response, command, status;
    int16_t feature[774], gain[65], phase[65], h_next[200], c_next[200], state[400];
};
static_assert(sizeof(deepfir_replay_mailbox_t) == 3428, "replay mailbox ABI");
extern "C" {
alignas(32) __attribute__((section(".bss.NonCacheable.ZeroInit"))) volatile deepfir_replay_mailbox_t
    g_deepfir_replay_mailbox;
}

extern "C" void deepfir_inference_replay_poll(void) {
    volatile deepfir_replay_mailbox_t &mailbox = g_deepfir_replay_mailbox;
    mailbox.magic = 0x52325250U;
    if (mailbox.request == mailbox.response) {
        return;
    }
    const uint32_t request = mailbox.request;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    int status = -1;
    if (mailbox.command == 1U) {
        if (deepfir_inference_init()) {
            deepfir_inference_reset();
            status = 0;
        }
    } else if (mailbox.command == 2U && g_initialized && !g_faulted && !g_processing) {
        g_processing = true;
        for (unsigned i = 0; i < 774; ++i) {
            g_adapter.feature_context_buffer[i] = mailbox.feature[i];
        }
        status = infer_callback(g_adapter.input_map, 3, g_adapter.output_map, 4, nullptr);
        if (status == 0) {
            deepfir_model_adapter_commit_state(&g_adapter);
            for (unsigned i = 0; i < 65; ++i) {
                mailbox.gain[i] = g_adapter.output_map[0].data[i];
                mailbox.phase[i] = g_adapter.output_map[1].data[i];
            }
            for (unsigned i = 0; i < 200; ++i) {
                mailbox.h_next[i] = g_adapter.output_map[2].data[i];
                mailbox.c_next[i] = g_adapter.output_map[3].data[i];
            }
        } else {
            g_faulted = true;
        }
        g_processing = false;
    }
    if (status == 0) {
        for (unsigned i = 0; i < 200; ++i) {
            mailbox.state[i] = g_adapter.h[i];
            mailbox.state[200 + i] = g_adapter.c[i];
        }
    }
    mailbox.status = static_cast<uint32_t>(status);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    mailbox.response = request;
}
#endif

extern "C" bool deepfir_inference_init(void) {
    if (g_initialized) {
        if (g_faulted && ethosu_runtime_soft_reset() != ETHOSU_RUNTIME_OK) {
            return false;
        }
        g_faulted = false;
        deepfir_inference_reset();
        return true;
    }
    const unsigned char *model_data = deepfir_model_package_data();
    const size_t model_size = deepfir_model_package_size();
    DEEPFIR_TRACE_INIT("model.sram", "BEGIN", 0);
    if (!in_sram(model_data, model_size) || model_size == 0U) {
        DEEPFIR_TRACE_INIT("model.sram", "FAIL", -1);
        return false;
    }
    DEEPFIR_TRACE_INIT("model.sram", "OK", 0);

    ethosu_runtime_config_t runtime_config = {};
    runtime_config.immutable_memory = model_data;
    runtime_config.immutable_memory_size = model_size;
    runtime_config.secure_enable = true;
    runtime_config.privilege_enable = true;
    DEEPFIR_TRACE_INIT("ethosu.init", "BEGIN", 0);
    const ethosu_runtime_status_t runtime_status = ethosu_runtime_init(&runtime_config);
    if (runtime_status != ETHOSU_RUNTIME_OK) {
        DEEPFIR_TRACE_INIT("ethosu.init", "FAIL", static_cast<int>(runtime_status));
        return false;
    }
    DEEPFIR_TRACE_INIT("ethosu.init", "OK", 0);

    g_model = tflite::GetModel(model_data);
    if (g_resolver == nullptr) {
        g_resolver = new (g_resolver_storage) tflite::MicroMutableOpResolver<1>();
        DEEPFIR_TRACE_INIT("resolver.ethosu", "BEGIN", 0);
        const TfLiteStatus resolver_status = g_resolver->AddEthosU();
        if (resolver_status != kTfLiteOk) {
            DEEPFIR_TRACE_INIT("resolver.ethosu", "FAIL", static_cast<int>(resolver_status));
            g_resolver = nullptr;
            (void)ethosu_runtime_deinit();
            return false;
        }
        DEEPFIR_TRACE_INIT("resolver.ethosu", "OK", 0);
    }
    DEEPFIR_TRACE_INIT("model.schema", "BEGIN", 0);
    if (g_model == nullptr || g_model->version() != TFLITE_SCHEMA_VERSION ||
        g_model->subgraphs() == nullptr || g_model->subgraphs()->size() != 1U ||
        g_model->subgraphs()->Get(0)->inputs() == nullptr ||
        g_model->subgraphs()->Get(0)->outputs() == nullptr ||
        g_model->subgraphs()->Get(0)->inputs()->size() != 3U ||
        g_model->subgraphs()->Get(0)->outputs()->size() != 4U) {
        DEEPFIR_TRACE_INIT("model.schema", "FAIL", -1);
        (void)ethosu_runtime_deinit();
        return false;
    }
    DEEPFIR_TRACE_INIT("model.schema", "OK", 0);
    g_interpreter = new (g_interpreter_storage) tflite::MicroInterpreter(
        g_model, *g_resolver, g_tensor_arena, sizeof(g_tensor_arena), nullptr, nullptr, false);
    DEEPFIR_TRACE_INIT("tensors.allocate", "BEGIN", 0);
    const TfLiteStatus allocation_status = g_interpreter->AllocateTensors();
    if (allocation_status != kTfLiteOk) {
#if M55M1_RUNTIME_TELEMETRY
        char message[128];
        const int length =
            std::snprintf(message, sizeof(message),
                          "[MODEL_MEMORY] status=ALLOCATE_FAILED arena_capacity=%lu\r\n",
                          static_cast<unsigned long>(sizeof(g_tensor_arena)));
        if (length > 0 && static_cast<size_t>(length) < sizeof(message)) {
            (void)debug_uart_tx_try_enqueue(message, static_cast<size_t>(length));
        }
#endif
        DEEPFIR_TRACE_INIT("tensors.allocate", "FAIL", static_cast<int>(allocation_status));
        (void)ethosu_runtime_deinit();
        g_interpreter = nullptr;
        return false;
    }
    if (!in_sram(g_tensor_arena, sizeof(g_tensor_arena))) {
        DEEPFIR_TRACE_INIT("arena.sram", "FAIL", -1);
        (void)ethosu_runtime_deinit();
        g_interpreter = nullptr;
        return false;
    }
#if M55M1_RUNTIME_TELEMETRY
    {
        const size_t used = g_interpreter->arena_used_bytes();
        const size_t capacity = sizeof(g_tensor_arena);
        char message[320];
        const int length =
            std::snprintf(message, sizeof(message),
                          "[MODEL_MEMORY] status=ALLOCATED model_bytes=%lu "
                          "arena_used=%lu arena_capacity=%lu arena_free=%lu "
                          "model_address=%p compiled_sha256=%s\r\n",
                          static_cast<unsigned long>(model_size), static_cast<unsigned long>(used),
                          static_cast<unsigned long>(capacity),
                          static_cast<unsigned long>(capacity >= used ? capacity - used : 0U),
                          static_cast<const void *>(model_data), deepfir_compiled_sha256);
        if (length > 0 && static_cast<size_t>(length) < sizeof(message)) {
            (void)debug_uart_tx_try_enqueue(message, static_cast<size_t>(length));
        }
    }
#endif
    DEEPFIR_TRACE_INIT("tensors.allocate", "OK", 0);
    DEEPFIR_TRACE_INIT("tensor.binding", "BEGIN", 0);
    for (uint32_t index = 0U; index < DEEPFIR_MODEL_INPUT_COUNT; ++index) {
        g_inputs[index] = find_tensor(true, index);
        if (g_inputs[index] == nullptr) {
            (void)ethosu_runtime_deinit();
            g_interpreter = nullptr;
            return false;
        }
    }
    for (uint32_t index = 0U; index < DEEPFIR_MODEL_OUTPUT_COUNT; ++index) {
        g_outputs[index] = find_tensor(false, index);
        if (g_outputs[index] == nullptr) {
            (void)ethosu_runtime_deinit();
            g_interpreter = nullptr;
            return false;
        }
    }
    DEEPFIR_TRACE_INIT("tensor.binding", "OK", 0);
    std::memset(&g_adapter, 0, sizeof(g_adapter));
    DEEPFIR_TRACE_INIT("adapter.qparams", "BEGIN", 0);
    const int qparams_status = deepfir_model_init_qparams(&g_adapter);
    if (qparams_status != 0) {
        DEEPFIR_TRACE_INIT("adapter.qparams", "FAIL", qparams_status);
        (void)ethosu_runtime_deinit();
        g_interpreter = nullptr;
        return false;
    }
    DEEPFIR_TRACE_INIT("adapter.qparams", "OK", 0);
    DEEPFIR_TRACE_INIT("fft.init", "BEGIN", 0);
    const int fft_status = deepfir_cmsis_fft_init(&g_fft_context);
    if (fft_status != 0) {
        DEEPFIR_TRACE_INIT("fft.init", "FAIL", fft_status);
        (void)ethosu_runtime_deinit();
        g_interpreter = nullptr;
        return false;
    }
    DEEPFIR_TRACE_INIT("fft.init", "OK", 0);
    g_feature_state.scale = deepfir_input_scale[0];
    g_feature_state.zero_point = deepfir_input_zero[0];
#if M55M1_RUNTIME_TELEMETRY || M55M1_AUDIO_RELEASE_PROBE
    g_feature_state.fft = timed_fft_callback;
#else
    g_feature_state.fft = deepfir_cmsis_fft_callback;
#endif
    g_feature_state.fft_context = &g_fft_context;
    g_adapter.feature = feature_callback;
    g_adapter.feature_context = &g_feature_state;
    g_adapter.infer_mapped = infer_callback;
    g_adapter.infer_context = nullptr;
    g_adapter.fft = deepfir_cmsis_fft_callback;
    g_adapter.fft_context = &g_fft_context;
#if M55M1_RUNTIME_TELEMETRY || M55M1_AUDIO_RELEASE_PROBE
    g_adapter.probe_mark = adapter_probe_mark;
    g_adapter.probe_context = nullptr;
#endif
    g_feature_mve_ready = deepfir_feature_mve_ready();
#if M55M1_DEEPFIR_MVE_SELF_TEST
    {
        DEEPFIR_TRACE_INIT("mve.feature", "BEGIN", 0);
        const uint32_t feature_start = audio_transport_cycles_now();
        g_feature_mve_ready = deepfir_feature_prepare_mve(&g_feature_state);
        const uint32_t feature_us = elapsed_us(feature_start, audio_transport_cycles_now());
        DEEPFIR_TRACE_INIT("mve.feature", g_feature_mve_ready ? "OK" : "FAIL",
                           g_feature_mve_ready ? 0 : -1);
        DEEPFIR_TRACE_INIT("mve.fir", "BEGIN", 0);
        const uint32_t fir_start = audio_transport_cycles_now();
        g_fir_mve_ready = deepfir_dsp_prepare_mve();
        g_mve_self_test_us = feature_us + elapsed_us(fir_start, audio_transport_cycles_now());
        DEEPFIR_TRACE_INIT("mve.fir", g_fir_mve_ready ? "OK" : "FAIL", g_fir_mve_ready ? 0 : -1);
    }
#else
    g_fir_mve_ready = deepfir_dsp_mve_ready();
    g_mve_self_test_us = 0U;
#endif
#if M55M1_DEEPFIR_MVE
    if (!g_feature_mve_ready || !g_fir_mve_ready) {
        DEEPFIR_TRACE_INIT("mve.required", "FAIL", -1);
        (void)ethosu_runtime_deinit();
        g_interpreter = nullptr;
        return false;
    }
#endif
    DEEPFIR_TRACE_INIT("state.reset", "BEGIN", 0);
    g_initialized = true;
    deepfir_inference_reset();
    return true;
}

extern "C" void deepfir_inference_reset(void) {
    if (!g_initialized) {
        return;
    }
    deepfir_feature_reset(&g_feature_state);
    deepfir_model_adapter_reset(&g_adapter);
    g_last_timing = {};
}

extern "C" bool deepfir_inference_process(const int16_t *input, int16_t *output,
                                          uint32_t sample_count) {
    if (!g_initialized || g_faulted || g_processing || input == nullptr || output == nullptr ||
        sample_count != DEEPFIR_DSP_HOP) {
        return false;
    }
    if (reinterpret_cast<uintptr_t>(output) <
            reinterpret_cast<uintptr_t>(input) + sample_count * sizeof(*input) &&
        reinterpret_cast<uintptr_t>(input) <
            reinterpret_cast<uintptr_t>(output) + sample_count * sizeof(*output)) {
        return false;
    }
    g_processing = true;
#if M55M1_RUNTIME_TELEMETRY || M55M1_AUDIO_RELEASE_PROBE
    // A failed hop must not report stage durations left over from its predecessor.
    g_last_timing = {};
#endif
#if M55M1_RUNTIME_TELEMETRY
    g_hop_event_count = 0U;
    g_hop_reported_events = 0U;
    g_hop_trace_overflow = false;
#endif
    float output_float[DEEPFIR_DSP_HOP];
    const uint32_t start = audio_transport_cycles_now();
    const int status =
        deepfir_model_adapter_process_pcm16(&g_adapter, input, sample_count, output_float);
    g_last_timing.total_us = elapsed_us(start, audio_transport_cycles_now());
    if (status != 0) {
        DEEPFIR_TRACE_HOP(DEEPFIR_HOP_ADAPTER_FAIL, status);
        g_faulted = true;
        g_processing = false;
        return false;
    }
    int16_t rendered[DEEPFIR_DSP_HOP];
#if M55M1_RUNTIME_TELEMETRY || M55M1_AUDIO_RELEASE_PROBE
    const uint32_t pcm_start = audio_transport_cycles_now();
#endif
    for (uint32_t index = 0U; index < sample_count; ++index) {
        if (!std::isfinite(output_float[index])) {
            DEEPFIR_TRACE_HOP(DEEPFIR_HOP_PCM_NONFINITE, static_cast<int>(index));
            g_faulted = true;
            g_processing = false;
            return false;
        }
        rendered[index] = deepfir_pcm16_from_float(output_float[index]);
    }
    std::memcpy(output, rendered, sizeof(rendered));
    DEEPFIR_TRACE_HOP(DEEPFIR_HOP_PCM_DONE, 0);
#if M55M1_RUNTIME_TELEMETRY || M55M1_AUDIO_RELEASE_PROBE
    g_last_timing.pcm_us = elapsed_us(pcm_start, audio_transport_cycles_now());
#endif
    g_last_timing.total_us = elapsed_us(start, audio_transport_cycles_now());
    const uint32_t accounted = g_last_timing.feature_us + g_last_timing.invoke_us;
    g_last_timing.dsp_us =
        g_last_timing.total_us > accounted ? g_last_timing.total_us - accounted : 0U;
#if M55M1_RUNTIME_TELEMETRY || M55M1_AUDIO_RELEASE_PROBE
    const uint32_t mapped_infer_us = elapsed_us(g_adapter_stage_cycles[DEEPFIR_PROBE_BEFORE_INFER],
                                                g_adapter_stage_cycles[DEEPFIR_PROBE_AFTER_INFER]);
    g_last_timing.bridge_us =
        mapped_infer_us > g_last_timing.invoke_us ? mapped_infer_us - g_last_timing.invoke_us : 0U;
    g_last_timing.carry_us = elapsed_us(g_adapter_stage_cycles[DEEPFIR_PROBE_AFTER_INFER],
                                        g_adapter_stage_cycles[DEEPFIR_PROBE_AFTER_CARRY]);
    g_last_timing.taps_us = elapsed_us(g_adapter_stage_cycles[DEEPFIR_PROBE_AFTER_CARRY],
                                       g_adapter_stage_cycles[DEEPFIR_PROBE_AFTER_TAPS]);
    g_last_timing.fir_us = elapsed_us(g_adapter_stage_cycles[DEEPFIR_PROBE_AFTER_TAPS],
                                      g_adapter_stage_cycles[DEEPFIR_PROBE_AFTER_FIR]);
    const uint32_t probe_accounted = accounted + g_last_timing.bridge_us + g_last_timing.carry_us +
                                     g_last_timing.taps_us + g_last_timing.fir_us +
                                     g_last_timing.pcm_us;
    g_last_timing.other_us =
        g_last_timing.total_us > probe_accounted ? g_last_timing.total_us - probe_accounted : 0U;
#endif
    g_processing = false;
    return true;
}

extern "C" void deepfir_inference_report_trace(void) {
#if M55M1_RUNTIME_TELEMETRY
    trace_report_hop();
#endif
}

extern "C" void deepfir_inference_get_last_timing(deepfir_inference_timing_t *timing) {
    if (timing != nullptr) {
        *timing = g_last_timing;
    }
}

extern "C" void deepfir_inference_get_memory(deepfir_inference_memory_t *memory) {
    if (memory != nullptr) {
        *memory = {};
        memory->model_bytes = deepfir_model_package_size();
        memory->arena_capacity_bytes = sizeof(g_tensor_arena);
        memory->arena_used_bytes = g_initialized ? g_interpreter->arena_used_bytes() : 0U;
        memory->adapter_bytes = sizeof(g_adapter);
        memory->frontend_bytes = sizeof(g_feature_state);
        memory->fft_bytes = sizeof(g_fft_context);
        memory->model_address = reinterpret_cast<uintptr_t>(deepfir_model_package_data());
        memory->arena_address = reinterpret_cast<uintptr_t>(g_tensor_arena);
        memory->initialized = g_initialized;
    }
}

extern "C" void deepfir_inference_get_dsp_status(bool *feature_mve_ready, bool *fir_mve_ready,
                                                 uint32_t *self_test_us) {
    if (feature_mve_ready != nullptr) {
        *feature_mve_ready = g_feature_mve_ready;
    }
    if (fir_mve_ready != nullptr) {
        *fir_mve_ready = g_fir_mve_ready;
    }
    if (self_test_us != nullptr) {
        *self_test_us = g_mve_self_test_us;
    }
}
