/**
 * @file audio_pipeline.c
 * @brief Foreground DMIC to DeepFIR inference and audio transport pipeline.
 * @author Bui Minh Huy
 * @date 2026-09-13
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "services/deepfir/deepfir_inference_service.h"
#include "drivers/audio_transport/audio_transport.h"
#include "services/audio_pipeline/audio_pipeline.h"
#include "common/audio_pcm.h"

#ifndef M55M1_ENABLE_DAC_I2S_OUTPUT
#define M55M1_ENABLE_DAC_I2S_OUTPUT 0
#endif
#ifndef M55M1_RUNTIME_TELEMETRY
#define M55M1_RUNTIME_TELEMETRY 0
#endif
#ifndef M55M1_AUDIO_RELEASE_PROBE
#define M55M1_AUDIO_RELEASE_PROBE 0
#endif
#ifndef M55M1_AUDIO_UART_PCM_CAPTURE
#define M55M1_AUDIO_UART_PCM_CAPTURE 0
#endif
#if M55M1_RUNTIME_TELEMETRY || M55M1_AUDIO_RELEASE_PROBE || M55M1_AUDIO_UART_PCM_CAPTURE
#include "drivers/debug_uart_tx/debug_uart_tx.h"
#endif

typedef struct {
    int32_t capture[AUDIO_TRANSPORT_SAMPLES_PER_BLOCK];
    int16_t playback[AUDIO_TRANSPORT_SAMPLES_PER_BLOCK];
} audio_working_set_t;

// Foreground owns both buffers; capture copies from DMA and submit copies into TX storage.
static audio_working_set_t g_audio_working_set;
static bool g_initialized;
static bool g_running;
static uint32_t g_expected_sequence;
// Foreground writes; SWD reads while halted. Linker places this in non-cacheable RAM.
// Preserve counters across stream restarts.
static volatile audio_pipeline_stats_t g_stats
    __attribute__((section(".bss.audio_pipeline_stats")));

static void record_input_levels(const int32_t *samples) {
    uint32_t peak = g_stats.input_peak;
    uint32_t clips = 0U;
    for (uint32_t index = 0U; index < AUDIO_TRANSPORT_SAMPLES_PER_BLOCK; ++index) {
        const int32_t sample = samples[index];
        const uint32_t magnitude = (uint32_t)(sample < 0 ? -sample : sample);
        if (magnitude > peak) {
            peak = magnitude;
        }
        if (sample == AUDIO_PCM24_MIN || sample == AUDIO_PCM24_MAX) {
            ++clips;
        }
    }
    g_stats.input_peak = peak;
    g_stats.input_clip_samples += clips;
    g_stats.input_samples += AUDIO_TRANSPORT_SAMPLES_PER_BLOCK;
}

#if M55M1_RUNTIME_TELEMETRY
// Foreground-only: boot and fault-report drains run only while DMA is stopped.
static bool g_boot_trace = true;
static uint32_t g_reported_faults;

static void trace_init(const char *stage, const char *status, int code) {
    if (!g_boot_trace) {
        return;
    }
    char message[160];
    const int length = snprintf(message, sizeof(message), "[INIT] stage=%s status=%s code=%d\r\n",
                                stage, status, code);
    if (length > 0 && (size_t)length < sizeof(message)) {
        (void)debug_uart_tx_try_enqueue(message, (size_t)length);
        if (!g_running) {
            debug_uart_tx_flush_blocking();
        }
    }
}

static void trace_board_progress(audio_transport_board_stage_t stage, void *context) {
    (void)context;
    static const char *const stages[] = {
        "board.enter",
        "board.hirc_ready",
        "board.hxt_wait",
        "board.hxt_ready",
        "board.apll1_wait",
        "board.apll1_ready",
        "board.apll0_wait",
        "board.apll0_ready",
        "board.sclk_ready",
        "board.module_clocks_ready",
        "board.audio_pins_ready",
        "board.i2c_recovery_ready",
        "board.i2c_pins_ready",
        "board.i2c_reset_ready",
        "board.timebase_ready",
        "board.peripherals_ready",
    };
    if ((unsigned)stage < sizeof(stages) / sizeof(stages[0])) {
        trace_init(stages[stage], "REACHED", 0);
    }
}

/** @brief Render the completed board snapshot; never called between recovery edges. */
static void trace_i2c_recovery(const i2c_recovery_trace_t *trace) {
    if (trace == NULL || !trace->attempted) {
        return;
    }
    static const char *const phases[] = {
        "release",      "clock.low",        "clock.high",
        "stop.sda_low", "stop.scl_release", "stop.sda_release",
    };
    const uint8_t count = trace->event_count <= I2C_RECOVERY_TRACE_CAPACITY
                              ? trace->event_count
                              : I2C_RECOVERY_TRACE_CAPACITY;
    const i2c_recovery_event_t *last = count != 0U ? &trace->events[count - 1U] : NULL;
    const char *result = trace->released ? "BUS_FREE" : "NO_FINAL_SAMPLE";
    if (!trace->released && last != NULL) {
        result = !last->sda && !last->scl ? "BOTH_LOW"
                 : !last->scl             ? "SCL_LOW"
                 : !last->sda             ? "SDA_LOW"
                                          : "READBACK_CHANGED";
    }
    char messages[6][256];
    const int lengths[] = {
        snprintf(messages[0], sizeof(messages[0]),
                 "[I2C_RECOVERY] result=%s released=%u rststs=%08lx pulses=%u events=%u "
                 "first_scl_low_event=%u\r\n",
                 result, trace->released ? 1U : 0U, (unsigned long)trace->reset_flags,
                 trace->pulses, count, trace->first_scl_low_event),
        snprintf(messages[1], sizeof(messages[1]),
                 "[I2C_GPIO] stage=before mfp=%08lx mode=%08lx dout=%08lx pin=%08lx "
                 "dinoff=%08lx pusel=%08lx smten=%08lx\r\n",
                 (unsigned long)trace->before.mfp, (unsigned long)trace->before.mode,
                 (unsigned long)trace->before.dout, (unsigned long)trace->before.pin,
                 (unsigned long)trace->before.dinoff, (unsigned long)trace->before.pusel,
                 (unsigned long)trace->before.smten),
        snprintf(messages[2], sizeof(messages[2]),
                 "[I2C_GPIO] stage=after mfp=%08lx mode=%08lx dout=%08lx pin=%08lx "
                 "dinoff=%08lx pusel=%08lx smten=%08lx\r\n",
                 (unsigned long)trace->after.mfp, (unsigned long)trace->after.mode,
                 (unsigned long)trace->after.dout, (unsigned long)trace->after.pin,
                 (unsigned long)trace->after.dinoff, (unsigned long)trace->after.pusel,
                 (unsigned long)trace->after.smten),
        snprintf(messages[3], sizeof(messages[3]),
                 "[I2C_REG] ctl0=%08lx status0=%08lx clkdiv=%08lx gpio_clk=%08lx i2c_clk=%08lx\r\n",
                 (unsigned long)trace->i2c_control, (unsigned long)trace->i2c_status,
                 (unsigned long)trace->i2c_divider, (unsigned long)trace->gpio_clock,
                 (unsigned long)trace->i2c_clock),
        snprintf(messages[4], sizeof(messages[4]),
                 "[I2C_TIMEBASE] source_before=%08lx source_after=%08lx "
                 "load_before=%08lx load_after=%08lx core_hz=%lu\r\n",
                 (unsigned long)trace->systick_source_before,
                 (unsigned long)trace->systick_source_after,
                 (unsigned long)trace->systick_load_before,
                 (unsigned long)trace->systick_load_after, (unsigned long)trace->core_hz),
        snprintf(messages[5], sizeof(messages[5]),
                 "[I2C_NOTE] pins=PA4:SDA,PA5:SCL rststs_sticky=1 "
                 "samples_not_waveform=1 nominal_delay_us=5\r\n"),
    };
    // UART drains only after the bus sequence, with DMA/audio still stopped.
    for (unsigned index = 0U; index < sizeof(lengths) / sizeof(lengths[0]); ++index) {
        if (lengths[index] > 0 && (size_t)lengths[index] < sizeof(messages[index])) {
            (void)debug_uart_tx_try_enqueue(messages[index], (size_t)lengths[index]);
            debug_uart_tx_flush_blocking();
        }
    }
    for (uint8_t index = 0U; index < count; ++index) {
        const i2c_recovery_event_t *event = &trace->events[index];
        const char *phase =
            event->phase < sizeof(phases) / sizeof(phases[0]) ? phases[event->phase] : "unknown";
        const uint32_t elapsed =
            audio_transport_cycles_elapsed(trace->events[0].cycle, event->cycle);
        const int length = snprintf(
            messages[0], sizeof(messages[0]),
            "[I2C_EDGE] index=%u phase=%s pulse=%u cycle=%lu elapsed_us=%lu sda=%u scl=%u\r\n",
            index, phase, event->pulse, (unsigned long)event->cycle,
            (unsigned long)audio_transport_cycles_to_us(elapsed), event->sda, event->scl);
        if (length > 0 && (size_t)length < sizeof(messages[0])) {
            (void)debug_uart_tx_try_enqueue(messages[0], (size_t)length);
            debug_uart_tx_flush_blocking();
        }
    }
}

static void trace_transport_report(const audio_transport_init_report_t *report) {
    if (!g_boot_trace) {
        return;
    }
    trace_i2c_recovery(report->i2c_trace);
    char message[256];
    const int length =
        snprintf(message, sizeof(message),
                 "[INIT_TRANSPORT] board_status=%d codec_status=%d clocks_valid=%lu "
                 "core_hz=%lu apll0_hz=%lu apll1_hz=%lu i2s_src_hz=%lu mclk_hz=%lu\r\n",
                 report->board_status, report->codec_status, (unsigned long)report->clocks_valid,
                 (unsigned long)report->core_hz, (unsigned long)report->apll0_hz,
                 (unsigned long)report->apll1_hz, (unsigned long)report->i2s_src_hz,
                 (unsigned long)report->mclk_hz);
    if (length > 0 && (size_t)length < sizeof(message)) {
        (void)debug_uart_tx_try_enqueue(message, (size_t)length);
        debug_uart_tx_flush_blocking();
    }
    if (report->codec_i2c_failures != 0U) {
        static const char *const steps[] = {"none",     "start", "address",
                                            "register", "value", "stop"};
        const unsigned step = report->codec_i2c_step;
        const int codec_length =
            snprintf(message, sizeof(message),
                     "[CODEC_I2C] failures=%lu page=%u reg=%u phase=%u step=%s attempts=%u "
                     "status=%02x expected=%02x ctl=%08lx timeout=%u stop_timeout=%u\r\n",
                     (unsigned long)report->codec_i2c_failures, report->codec_i2c_page,
                     report->codec_i2c_register, report->codec_i2c_phase,
                     step < (sizeof(steps) / sizeof(steps[0])) ? steps[step] : "unknown",
                     report->codec_i2c_attempts, report->codec_i2c_status,
                     report->codec_i2c_expected_status, (unsigned long)report->codec_i2c_control,
                     report->codec_i2c_timed_out, report->codec_i2c_stop_timed_out);
        if (codec_length > 0 && (size_t)codec_length < sizeof(message)) {
            (void)debug_uart_tx_try_enqueue(message, (size_t)codec_length);
            debug_uart_tx_flush_blocking();
        }
    }
}
#define AUDIO_PIPELINE_TRACE_INIT(stage, status, code) trace_init(stage, status, code)
#else
#define AUDIO_PIPELINE_TRACE_INIT(stage, status, code) ((void)0)
#endif

#if M55M1_RUNTIME_TELEMETRY || M55M1_AUDIO_RELEASE_PROBE
static void trace_startup_after_stop(void) {
    const volatile audio_startup_trace_t *trace = audio_transport_get_startup_trace();
    if (!trace || trace->active || trace->count == 0u) {
        return;
    }
    static const char *const phases[] = {"UNKNOWN", "TX_ARM", "TX_ENABLED",   "DMIC_ENABLED",
                                         "TX_TD",   "RX_TD",  "CAPTURE_TAKE", "STOP"};
    const uint32_t count =
        trace->count < AUDIO_STARTUP_TRACE_CAPACITY ? trace->count : AUDIO_STARTUP_TRACE_CAPACITY;
    for (uint32_t i = 0; i < count; ++i) {
        const audio_startup_event_t event = trace->events[i];
        const char *phase = event.phase <= AUDIO_STARTUP_STOP ? phases[event.phase] : phases[0];
        char message[512];
        const int length =
            snprintf(message, sizeof(message),
                     "[AUDIO_STARTUP] event=%lu phase=%s cycle=%lu elapsed_us=%lu rx_seq=%lu "
                     "txfifo=%lu txth=%lu i2s0=%08lx i2s1=%08lx ctl1=%08lx "
                     "txctl=%08lx rxctl=%08lx txtd=%08lx rxtd=%08lx "
                     "primask=%lu basepri=%lu ipsr=%lu pending=%08lx\r\n",
                     (unsigned long)i, phase, (unsigned long)event.cycle,
                     (unsigned long)audio_transport_cycles_to_us(
                         audio_transport_cycles_elapsed(trace->events[0].cycle, event.cycle)),
                     (unsigned long)event.rx_sequence, (unsigned long)event.tx_fifo_words,
                     (unsigned long)event.tx_threshold_words, (unsigned long)event.i2s_status0,
                     (unsigned long)event.i2s_status1, (unsigned long)event.i2s_ctl1,
                     (unsigned long)event.tx_dma_ctl, (unsigned long)event.rx_dma_ctl,
                     (unsigned long)event.tx_td_status, (unsigned long)event.rx_td_status,
                     (unsigned long)event.primask, (unsigned long)event.basepri,
                     (unsigned long)event.ipsr, (unsigned long)event.irq_pending);
        if (length > 0 && (size_t)length < sizeof(message)) {
            (void)debug_uart_tx_try_enqueue(message, (size_t)length);
            debug_uart_tx_flush_blocking();
        }
    }
}
#endif

typedef enum {
    AUDIO_PIPELINE_FAULT_TRANSPORT,
    AUDIO_PIPELINE_FAULT_SEQUENCE,
    AUDIO_PIPELINE_FAULT_INFERENCE,
    AUDIO_PIPELINE_FAULT_PROCESS_DEADLINE,
    AUDIO_PIPELINE_FAULT_SUBMIT,
    AUDIO_PIPELINE_FAULT_PUBLISH_DEADLINE,
    AUDIO_PIPELINE_FAULT_RESTART,
} audio_pipeline_fault_t;

#if M55M1_AUDIO_RELEASE_PROBE
static void finish_release_probe_if_ready(bool failed, uint32_t fault, uint32_t detail);
#endif

static void trace_fault(audio_pipeline_fault_t fault, uint32_t detail) {
#if M55M1_RUNTIME_TELEMETRY
    const uint32_t bit = 1UL << (unsigned)fault;
    if ((g_reported_faults & bit) != 0U) {
        return;
    }
    static const char *const stages[] = {
        "transport", "sequence",         "inference", "process_deadline",
        "submit",    "publish_deadline", "restart",
    };
    audio_transport_health_t health;
    audio_transport_get_health(&health);
    char message[512];
    const int length = snprintf(
        message, sizeof(message),
        "[AUDIO_FAULT] stage=%s detail=%lu seq=%lu expected=%lu "
        "deadline=%lu model=%lu restarts=%lu restart_failures=%lu "
        "dmic_overrun=%lu dmic_dma=%lu dac_underrun=%lu dac_dma=%lu "
        "capture_cycle=%lu process_start=%lu process_end=%lu published_cycle=%lu "
        "playback_slot=%lu\r\n",
        stages[fault], (unsigned long)detail, (unsigned long)g_stats.last_sequence,
        (unsigned long)g_expected_sequence, (unsigned long)g_stats.deadline_misses,
        (unsigned long)g_stats.model_faults, (unsigned long)g_stats.restarts,
        (unsigned long)g_stats.restart_failures, (unsigned long)health.dmic_overrun_blocks,
        (unsigned long)health.dmic_dma_errors, (unsigned long)health.dac_underrun_blocks,
        (unsigned long)health.dac_dma_errors, (unsigned long)g_stats.capture_completed_cycle,
        (unsigned long)g_stats.process_start_cycle, (unsigned long)g_stats.process_end_cycle,
        (unsigned long)g_stats.published_cycle, (unsigned long)g_stats.scheduled_playback_slot);
    // Log a fault category once, retrying admission if the UART ring was full.
    if (length > 0 && (size_t)length < sizeof(message) &&
        debug_uart_tx_try_enqueue(message, (size_t)length)) {
        g_reported_faults |= bit;
    }
#else
    (void)fault;
    (void)detail;
#endif
}

void audio_pipeline_get_stats(audio_pipeline_stats_t *stats) {
    if (stats != NULL) {
        *stats = g_stats;
    }
}

static bool transport_faulted(void) {
    audio_transport_health_t health = {0};
    audio_transport_get_health(&health);
    return health.dmic_overrun_blocks || health.dmic_dma_errors || health.dac_underrun_blocks ||
           health.dac_dma_errors;
}

/* Foreground only: stop DMA before resetting any shared model/audio history. */
static void restart_stream(audio_pipeline_fault_t fault, uint32_t detail) {
#if M55M1_AUDIO_RELEASE_PROBE
    // A diagnostic run ends at its first fault; never replace it with a clean restart window.
    finish_release_probe_if_ready(true, (uint32_t)fault, detail);
    return;
#endif
    audio_pipeline_stop_safe();
    // Quiesce both DMA owners before spending any time formatting diagnostics.
#if M55M1_RUNTIME_TELEMETRY
    deepfir_inference_report_trace();
    trace_startup_after_stop();
#endif
    trace_fault(fault, detail);
    memset(&g_audio_working_set, 0, sizeof(g_audio_working_set));
    if (audio_pipeline_init() && audio_pipeline_start()) {
        ++g_stats.restarts;
    } else {
        ++g_stats.restart_failures;
        trace_fault(AUDIO_PIPELINE_FAULT_RESTART, 0U);
    }
}

#if M55M1_AUDIO_UART_PCM_CAPTURE
#define AUDIO_PIPELINE_CAPTURE_SAMPLE_COUNT AUDIO_TRANSPORT_SAMPLE_RATE_HZ
#define AUDIO_PIPELINE_CAPTURE_HEADER_BYTES 48U
#define AUDIO_PIPELINE_CAPTURE_CHUNK_BYTES 512U

/* One second per stream; this diagnostic buffer lives in cacheable SRAM. */
static int16_t g_capture_input[AUDIO_PIPELINE_CAPTURE_SAMPLE_COUNT];
static int16_t g_capture_output[AUDIO_PIPELINE_CAPTURE_SAMPLE_COUNT];
static uint32_t g_capture_sample_count;
static uint32_t g_capture_input_clips;
static uint32_t g_capture_output_clips;
static uint32_t g_capture_sequence_gaps;
static uint32_t g_capture_last_sequence;
static bool g_capture_has_sequence;
static bool g_capture_armed;
static bool g_capture_sent;

static void capture_store_u16(uint8_t *destination, uint16_t value) {
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8U);
}

static void capture_store_u32(uint8_t *destination, uint32_t value) {
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8U);
    destination[2] = (uint8_t)(value >> 16U);
    destination[3] = (uint8_t)(value >> 24U);
}

static uint32_t capture_crc32_byte(uint32_t crc, uint8_t value) {
    crc ^= value;
    for (uint32_t bit = 0U; bit < 8U; ++bit) {
        crc = (crc >> 1U) ^ ((crc & 1U) ? 0xEDB88320U : 0U);
    }
    return crc;
}

static uint32_t capture_payload_crc32(uint32_t sample_count) {
    uint32_t crc = 0xFFFFFFFFU;
    for (uint32_t index = 0U; index < sample_count; ++index) {
        const uint16_t input = (uint16_t)g_capture_input[index];
        const uint16_t output = (uint16_t)g_capture_output[index];
        crc = capture_crc32_byte(crc, (uint8_t)input);
        crc = capture_crc32_byte(crc, (uint8_t)(input >> 8U));
        crc = capture_crc32_byte(crc, (uint8_t)output);
        crc = capture_crc32_byte(crc, (uint8_t)(output >> 8U));
    }
    return ~crc;
}

static bool capture_send_bytes(const uint8_t *bytes, uint32_t length) {
    if (!debug_uart_tx_try_enqueue((const char *)bytes, length)) {
        debug_uart_tx_flush_blocking();
        if (!debug_uart_tx_try_enqueue((const char *)bytes, length)) {
            return false;
        }
    }
    debug_uart_tx_flush_blocking();
    return true;
}

static void capture_send_packet(uint32_t status) {
    uint8_t header[AUDIO_PIPELINE_CAPTURE_HEADER_BYTES] = {0U};
    uint8_t chunk[AUDIO_PIPELINE_CAPTURE_CHUNK_BYTES];
    const uint32_t sample_count = (status == 0U) ? g_capture_sample_count : 0U;
    uint32_t chunk_length = 0U;

    if (g_capture_sent) {
        return;
    }
    g_capture_sent = true;
    g_capture_armed = false;
    audio_pipeline_stop_safe();

    memcpy(header, "DFIRPCM1", 8U);
    capture_store_u16(&header[8], 1U);
    capture_store_u16(&header[10], AUDIO_PIPELINE_CAPTURE_HEADER_BYTES);
    capture_store_u32(&header[12], status);
    capture_store_u32(&header[16], AUDIO_TRANSPORT_SAMPLE_RATE_HZ);
    capture_store_u32(&header[20], sample_count);
    capture_store_u16(&header[24], AUDIO_TRANSPORT_SAMPLES_PER_BLOCK);
    capture_store_u16(&header[26], 0U);
    capture_store_u16(&header[28], (uint16_t)(int16_t)M55M1_DMIC_GAIN_DB);
    capture_store_u16(&header[30], 0U);
    capture_store_u32(&header[32], g_capture_input_clips);
    capture_store_u32(&header[36], g_capture_output_clips);
    capture_store_u32(&header[40], g_capture_sequence_gaps);
    capture_store_u32(&header[44], (status == 0U) ? capture_payload_crc32(sample_count) : 0U);
    if (!capture_send_bytes(header, sizeof(header)) || (status != 0U)) {
        return;
    }

    for (uint32_t index = 0U; index < sample_count; ++index) {
        const uint16_t input = (uint16_t)g_capture_input[index];
        const uint16_t output = (uint16_t)g_capture_output[index];
        chunk[chunk_length++] = (uint8_t)input;
        chunk[chunk_length++] = (uint8_t)(input >> 8U);
        chunk[chunk_length++] = (uint8_t)output;
        chunk[chunk_length++] = (uint8_t)(output >> 8U);
        if (chunk_length == sizeof(chunk)) {
            if (!capture_send_bytes(chunk, chunk_length)) {
                return;
            }
            chunk_length = 0U;
        }
    }
    if (chunk_length > 0U) {
        (void)capture_send_bytes(chunk, chunk_length);
    }
}

static void capture_store_hop(const int32_t *input, const int16_t *output, uint32_t sequence) {
    if (g_capture_has_sequence && (sequence != g_capture_last_sequence + 1U)) {
        ++g_capture_sequence_gaps;
    }
    g_capture_last_sequence = sequence;
    g_capture_has_sequence = true;

    for (uint32_t index = 0U; index < AUDIO_TRANSPORT_SAMPLES_PER_BLOCK; ++index) {
        const uint32_t destination = g_capture_sample_count + index;
        g_capture_input[destination] = audio_pcm24_to_pcm16(input[index]);
        g_capture_output[destination] = output[index];
        if ((input[index] == AUDIO_PCM24_MIN) || (input[index] == AUDIO_PCM24_MAX)) {
            ++g_capture_input_clips;
        }
        if ((output[index] == INT16_MIN) || (output[index] == INT16_MAX)) {
            ++g_capture_output_clips;
        }
    }
    g_capture_sample_count += AUDIO_TRANSPORT_SAMPLES_PER_BLOCK;
    if (g_capture_sample_count == AUDIO_PIPELINE_CAPTURE_SAMPLE_COUNT) {
        capture_send_packet(0U);
    }
}
#endif

#if M55M1_RUNTIME_TELEMETRY || M55M1_AUDIO_RELEASE_PROBE
#if M55M1_RUNTIME_TELEMETRY
#define AUDIO_PIPELINE_REPORT_HOPS 1000U
#endif
#if M55M1_AUDIO_RELEASE_PROBE
#define AUDIO_PIPELINE_PROBE_HOPS 60000U
#endif
#define AUDIO_PIPELINE_HOP_BUDGET_US                                                               \
    ((AUDIO_TRANSPORT_SAMPLES_PER_BLOCK * 1000000UL) / AUDIO_TRANSPORT_SAMPLE_RATE_HZ)

typedef struct {
    uint32_t input_peak;
    uint32_t output_peak;
    uint32_t input_clip_samples;
    uint32_t output_clip_samples;
    uint32_t model_failures;
    uint32_t submit_busy;
    uint32_t submit_errors;
    uint32_t max_feature_us;
    uint32_t max_pre_fft_us;
    uint32_t max_fft_us;
    uint32_t max_post_fft_us;
    uint32_t max_npu_us;
    uint32_t max_invoke_us;
    uint32_t max_overhead_us;
    uint32_t max_dsp_us;
    uint32_t max_total_us;
    uint32_t max_e2e_us;
    uint32_t late_hops;
    uint32_t max_bridge_us;
    uint32_t max_carry_us;
    uint32_t max_taps_us;
    uint32_t max_fir_us;
    uint32_t max_pcm_us;
    uint32_t max_other_us;
#if M55M1_AUDIO_RELEASE_PROBE
    uint64_t total_us_sum;
    uint64_t e2e_us_sum;
    uint64_t bridge_us_sum;
    uint64_t carry_us_sum;
    uint64_t taps_us_sum;
    uint64_t fir_us_sum;
    uint64_t pcm_us_sum;
    uint64_t other_us_sum;
    uint32_t capture_last_sequence;
#endif
} audio_pipeline_debug_stats_t;

static uint32_t g_frame_count;
static audio_pipeline_debug_stats_t g_debug_stats;

static void record_pcm24_stats(const int32_t *samples, uint32_t *peak, uint32_t *clip_samples) {
    for (uint32_t index = 0U; index < AUDIO_TRANSPORT_SAMPLES_PER_BLOCK; ++index) {
        const int32_t sample = samples[index];
        const uint32_t magnitude = (uint32_t)(sample < 0 ? -sample : sample);
        if (magnitude > *peak) {
            *peak = magnitude;
        }
        if (sample == AUDIO_PCM24_MIN || sample == AUDIO_PCM24_MAX) {
            ++*clip_samples;
        }
    }
}

static void record_pcm_stats(const int16_t *samples, uint32_t *peak, uint32_t *clip_samples) {
    for (uint32_t index = 0U; index < AUDIO_TRANSPORT_SAMPLES_PER_BLOCK; ++index) {
        const int32_t sample = samples[index];
        const uint32_t magnitude = (uint32_t)(sample < 0 ? -sample : sample);
        if (magnitude > *peak) {
            *peak = magnitude;
        }
        if (sample == -32768 || sample == 32767) {
            ++*clip_samples;
        }
    }
}

static void record_timing(const deepfir_inference_timing_t *timing) {
    if (timing->feature_us > g_debug_stats.max_feature_us) {
        g_debug_stats.max_feature_us = timing->feature_us;
    }
    if (timing->pre_fft_us > g_debug_stats.max_pre_fft_us) {
        g_debug_stats.max_pre_fft_us = timing->pre_fft_us;
    }
    if (timing->fft_us > g_debug_stats.max_fft_us) {
        g_debug_stats.max_fft_us = timing->fft_us;
    }
    if (timing->post_fft_us > g_debug_stats.max_post_fft_us) {
        g_debug_stats.max_post_fft_us = timing->post_fft_us;
    }
    if (timing->npu_us > g_debug_stats.max_npu_us) {
        g_debug_stats.max_npu_us = timing->npu_us;
    }
    if (timing->invoke_us > g_debug_stats.max_invoke_us) {
        g_debug_stats.max_invoke_us = timing->invoke_us;
    }
    if (timing->invoke_overhead_us > g_debug_stats.max_overhead_us) {
        g_debug_stats.max_overhead_us = timing->invoke_overhead_us;
    }
    if (timing->dsp_us > g_debug_stats.max_dsp_us) {
        g_debug_stats.max_dsp_us = timing->dsp_us;
    }
    if (timing->total_us > g_debug_stats.max_total_us) {
        g_debug_stats.max_total_us = timing->total_us;
    }
    if (timing->bridge_us > g_debug_stats.max_bridge_us) {
        g_debug_stats.max_bridge_us = timing->bridge_us;
    }
    if (timing->carry_us > g_debug_stats.max_carry_us) {
        g_debug_stats.max_carry_us = timing->carry_us;
    }
    if (timing->taps_us > g_debug_stats.max_taps_us) {
        g_debug_stats.max_taps_us = timing->taps_us;
    }
    if (timing->fir_us > g_debug_stats.max_fir_us) {
        g_debug_stats.max_fir_us = timing->fir_us;
    }
    if (timing->pcm_us > g_debug_stats.max_pcm_us) {
        g_debug_stats.max_pcm_us = timing->pcm_us;
    }
    if (timing->other_us > g_debug_stats.max_other_us) {
        g_debug_stats.max_other_us = timing->other_us;
    }
#if M55M1_AUDIO_RELEASE_PROBE
    g_debug_stats.total_us_sum += timing->total_us;
    g_debug_stats.bridge_us_sum += timing->bridge_us;
    g_debug_stats.carry_us_sum += timing->carry_us;
    g_debug_stats.taps_us_sum += timing->taps_us;
    g_debug_stats.fir_us_sum += timing->fir_us;
    g_debug_stats.pcm_us_sum += timing->pcm_us;
    g_debug_stats.other_us_sum += timing->other_us;
#endif
}

static void record_progress(uint32_t capture_completed_cycle, bool model_succeeded) {
    uint32_t e2e_us = audio_transport_cycles_to_us(
        audio_transport_cycles_elapsed(capture_completed_cycle, audio_transport_cycles_now()));

    if (model_succeeded) {
        deepfir_inference_timing_t timing;
        deepfir_inference_get_last_timing(&timing);
        record_timing(&timing);
    }
    if (e2e_us > g_debug_stats.max_e2e_us) {
        g_debug_stats.max_e2e_us = e2e_us;
    }
    if (e2e_us >= AUDIO_PIPELINE_HOP_BUDGET_US) {
        ++g_debug_stats.late_hops;
    }
#if M55M1_AUDIO_RELEASE_PROBE
    g_debug_stats.e2e_us_sum += e2e_us;
#endif
#if M55M1_RUNTIME_TELEMETRY
    if ((g_frame_count % AUDIO_PIPELINE_REPORT_HOPS) != 0U) {
        return;
    }

    audio_transport_health_t health;
    audio_transport_get_health(&health);
    bool feature_mve_ready = false;
    bool fir_mve_ready = false;
    uint32_t mve_self_test_us = 0U;
    deepfir_inference_get_dsp_status(&feature_mve_ready, &fir_mve_ready, &mve_self_test_us);
    char message[768];
    const int length = snprintf(
        message, sizeof(message),
        "[AUDIO] frame=%lu in_peak=%lu out_peak=%lu in_clip=%lu out_clip=%lu "
        "model_fail=%lu submit_busy=%lu submit_error=%lu "
        "dmic_overrun=%lu dac_underrun=%lu dmic_dma=%lu dac_dma=%lu "
        "feature_mve=%lu fir_mve=%lu mve_test_us=%lu "
        "feature_max_us=%lu pre_fft_max_us=%lu fft_max_us=%lu "
        "post_fft_max_us=%lu npu_max_us=%lu invoke_max_us=%lu "
        "overhead_max_us=%lu bridge_max_us=%lu carry_max_us=%lu "
        "taps_max_us=%lu fir_max_us=%lu pcm_max_us=%lu other_max_us=%lu "
        "dsp_max_us=%lu total_max_us=%lu e2e_max_us=%lu late_1ms=%lu "
        "uart_drop=%lu\r\n",
        (unsigned long)g_frame_count, (unsigned long)g_debug_stats.input_peak,
        (unsigned long)g_debug_stats.output_peak, (unsigned long)g_debug_stats.input_clip_samples,
        (unsigned long)g_debug_stats.output_clip_samples,
        (unsigned long)g_debug_stats.model_failures, (unsigned long)g_debug_stats.submit_busy,
        (unsigned long)g_debug_stats.submit_errors, (unsigned long)health.dmic_overrun_blocks,
        (unsigned long)health.dac_underrun_blocks, (unsigned long)health.dmic_dma_errors,
        (unsigned long)health.dac_dma_errors, (unsigned long)feature_mve_ready,
        (unsigned long)fir_mve_ready, (unsigned long)mve_self_test_us,
        (unsigned long)g_debug_stats.max_feature_us, (unsigned long)g_debug_stats.max_pre_fft_us,
        (unsigned long)g_debug_stats.max_fft_us, (unsigned long)g_debug_stats.max_post_fft_us,
        (unsigned long)g_debug_stats.max_npu_us, (unsigned long)g_debug_stats.max_invoke_us,
        (unsigned long)g_debug_stats.max_overhead_us, (unsigned long)g_debug_stats.max_bridge_us,
        (unsigned long)g_debug_stats.max_carry_us, (unsigned long)g_debug_stats.max_taps_us,
        (unsigned long)g_debug_stats.max_fir_us, (unsigned long)g_debug_stats.max_pcm_us,
        (unsigned long)g_debug_stats.max_other_us, (unsigned long)g_debug_stats.max_dsp_us,
        (unsigned long)g_debug_stats.max_total_us, (unsigned long)g_debug_stats.max_e2e_us,
        (unsigned long)g_debug_stats.late_hops, (unsigned long)debug_uart_tx_drop_count());
    if (length > 0 && (size_t)length < sizeof(message)) {
        (void)debug_uart_tx_try_enqueue(message, (size_t)length);
    }
#endif
}

#if M55M1_AUDIO_RELEASE_PROBE
static void finish_release_probe_if_ready(bool failed, uint32_t fault, uint32_t detail) {
    if (!failed && g_frame_count != AUDIO_PIPELINE_PROBE_HOPS) {
        return;
    }

    audio_transport_health_t health;
    audio_transport_get_health(&health);
    deepfir_inference_timing_t last_timing;
    deepfir_inference_get_last_timing(&last_timing);
    const bool unrecorded_hop = failed && (fault == AUDIO_PIPELINE_FAULT_PROCESS_DEADLINE ||
                                           fault == AUDIO_PIPELINE_FAULT_SUBMIT ||
                                           fault == AUDIO_PIPELINE_FAULT_PUBLISH_DEADLINE);
    const uint32_t failure_elapsed_us =
        unrecorded_hop ? audio_transport_cycles_to_us(audio_transport_cycles_elapsed(
                             g_stats.capture_completed_cycle, audio_transport_cycles_now()))
                       : 0U;
    audio_pipeline_stop_safe();
    if (unrecorded_hop) {
        // These exits precede record_progress; include their completed inference exactly once.
        record_timing(&last_timing);
        if (failure_elapsed_us > g_debug_stats.max_e2e_us) {
            g_debug_stats.max_e2e_us = failure_elapsed_us;
        }
        if (failure_elapsed_us >= AUDIO_PIPELINE_HOP_BUDGET_US) {
            ++g_debug_stats.late_hops;
        }
        g_debug_stats.e2e_us_sum += failure_elapsed_us;
    }
    debug_uart_tx_init();
    trace_startup_after_stop();
    bool feature_mve_ready = false;
    bool fir_mve_ready = false;
    uint32_t mve_self_test_us = 0U;
    deepfir_inference_get_dsp_status(&feature_mve_ready, &fir_mve_ready, &mve_self_test_us);

    char message[1024];
    // A transport/sequence failure can occur before any hop has completed.
    const uint32_t observed_hops = g_frame_count == 0U ? 1U : g_frame_count;
    const int length = snprintf(
        message, sizeof(message),
        "[AUDIO_RELEASE_PROBE] frame=%lu in_peak=%lu out_peak=%lu "
        "in_clip=%lu out_clip=%lu model_fail=%lu "
        "submit_busy=%lu submit_error=%lu "
        "dmic_overrun=%lu dac_underrun=%lu dmic_dma=%lu dac_dma=%lu "
        "feature_mve_ready=%lu fir_mve_ready=%lu mve_test_us=%lu "
        "feature_max_us=%lu pre_fft_max_us=%lu fft_max_us=%lu "
        "post_fft_max_us=%lu npu_max_us=%lu invoke_max_us=%lu "
        "overhead_max_us=%lu dsp_max_us=%lu "
        "bridge_max_us=%lu carry_max_us=%lu taps_max_us=%lu "
        "fir_max_us=%lu pcm_max_us=%lu other_max_us=%lu total_max_us=%lu "
        "e2e_max_us=%lu late_1ms=%lu "
        "bridge_mean_us=%lu carry_mean_us=%lu taps_mean_us=%lu "
        "fir_mean_us=%lu pcm_mean_us=%lu other_mean_us=%lu "
        "total_mean_us=%lu e2e_mean_us=%lu capture_last_seq=%lu\r\n",
        (unsigned long)g_frame_count, (unsigned long)g_debug_stats.input_peak,
        (unsigned long)g_debug_stats.output_peak, (unsigned long)g_debug_stats.input_clip_samples,
        (unsigned long)g_debug_stats.output_clip_samples,
        (unsigned long)g_debug_stats.model_failures, (unsigned long)g_debug_stats.submit_busy,
        (unsigned long)g_debug_stats.submit_errors, (unsigned long)health.dmic_overrun_blocks,
        (unsigned long)health.dac_underrun_blocks, (unsigned long)health.dmic_dma_errors,
        (unsigned long)health.dac_dma_errors, (unsigned long)feature_mve_ready,
        (unsigned long)fir_mve_ready, (unsigned long)mve_self_test_us,
        (unsigned long)g_debug_stats.max_feature_us, (unsigned long)g_debug_stats.max_pre_fft_us,
        (unsigned long)g_debug_stats.max_fft_us, (unsigned long)g_debug_stats.max_post_fft_us,
        (unsigned long)g_debug_stats.max_npu_us, (unsigned long)g_debug_stats.max_invoke_us,
        (unsigned long)g_debug_stats.max_overhead_us, (unsigned long)g_debug_stats.max_dsp_us,
        (unsigned long)g_debug_stats.max_bridge_us, (unsigned long)g_debug_stats.max_carry_us,
        (unsigned long)g_debug_stats.max_taps_us, (unsigned long)g_debug_stats.max_fir_us,
        (unsigned long)g_debug_stats.max_pcm_us, (unsigned long)g_debug_stats.max_other_us,
        (unsigned long)g_debug_stats.max_total_us, (unsigned long)g_debug_stats.max_e2e_us,
        (unsigned long)g_debug_stats.late_hops,
        (unsigned long)(g_debug_stats.bridge_us_sum / observed_hops),
        (unsigned long)(g_debug_stats.carry_us_sum / observed_hops),
        (unsigned long)(g_debug_stats.taps_us_sum / observed_hops),
        (unsigned long)(g_debug_stats.fir_us_sum / observed_hops),
        (unsigned long)(g_debug_stats.pcm_us_sum / observed_hops),
        (unsigned long)(g_debug_stats.other_us_sum / observed_hops),
        (unsigned long)(g_debug_stats.total_us_sum / observed_hops),
        (unsigned long)(g_debug_stats.e2e_us_sum / observed_hops),
        (unsigned long)g_debug_stats.capture_last_sequence);
    if (length > 0 && (size_t)length < sizeof(message)) {
        (void)debug_uart_tx_try_enqueue(message, (size_t)length);
        debug_uart_tx_flush_blocking();
    }
    const int result_length = snprintf(
        message, sizeof(message),
        "[AUDIO_RELEASE_RESULT] result=%s fault=%lu detail=%lu seq=%lu playback_slot=%lu "
        "deadline=%lu restarts=%lu last_feature_us=%lu last_invoke_us=%lu "
        "last_pre_fft_us=%lu last_fft_us=%lu last_post_fft_us=%lu "
        "last_npu_us=%lu last_bridge_us=%lu last_other_us=%lu "
        "last_carry_us=%lu last_taps_us=%lu last_fir_us=%lu last_pcm_us=%lu "
        "last_total_us=%lu capture_publish_max_us=%lu\r\n",
        failed ? "FAIL" : "COMPLETE", (unsigned long)fault, (unsigned long)detail,
        (unsigned long)g_stats.last_sequence, (unsigned long)g_stats.scheduled_playback_slot,
        (unsigned long)g_stats.deadline_misses, (unsigned long)g_stats.restarts,
        (unsigned long)last_timing.feature_us, (unsigned long)last_timing.invoke_us,
        (unsigned long)last_timing.pre_fft_us, (unsigned long)last_timing.fft_us,
        (unsigned long)last_timing.post_fft_us, (unsigned long)last_timing.npu_us,
        (unsigned long)last_timing.bridge_us, (unsigned long)last_timing.other_us,
        (unsigned long)last_timing.carry_us, (unsigned long)last_timing.taps_us,
        (unsigned long)last_timing.fir_us, (unsigned long)last_timing.pcm_us,
        (unsigned long)last_timing.total_us, (unsigned long)g_stats.max_capture_to_publish_us);
    if (result_length > 0 && (size_t)result_length < sizeof(message)) {
        (void)debug_uart_tx_try_enqueue(message, (size_t)result_length);
        debug_uart_tx_flush_blocking();
    }
}
#endif
#endif

bool audio_pipeline_init(void) {
    audio_transport_init_report_t transport_report = {0};

    if (g_initialized) {
        return true;
    }
#if M55M1_RUNTIME_TELEMETRY
    // UART uses its own HIRC source, so board clock boundaries remain observable.
    debug_uart_tx_init();
#endif
    AUDIO_PIPELINE_TRACE_INIT("transport", "BEGIN", 0);
    const bool transport_ok = audio_transport_init(&transport_report,
#if M55M1_RUNTIME_TELEMETRY
                                                   g_boot_trace ? trace_board_progress : NULL,
#else
                                                   NULL,
#endif
                                                   NULL);
#if M55M1_RUNTIME_TELEMETRY
    trace_transport_report(&transport_report);
#endif
    if (!transport_ok || !transport_report.clocks_valid) {
        AUDIO_PIPELINE_TRACE_INIT("transport", "FAIL", -1);
        return false;
    }
    AUDIO_PIPELINE_TRACE_INIT("transport", "OK", 0);
    AUDIO_PIPELINE_TRACE_INIT("inference", "BEGIN", 0);
    if (!deepfir_inference_init()) {
        AUDIO_PIPELINE_TRACE_INIT("inference", "FAIL", -1);
        return false;
    }
    AUDIO_PIPELINE_TRACE_INIT("inference", "OK", 0);
#if M55M1_AUDIO_UART_PCM_CAPTURE
    debug_uart_tx_init();
    g_capture_sample_count = 0U;
    g_capture_input_clips = 0U;
    g_capture_output_clips = 0U;
    g_capture_sequence_gaps = 0U;
    g_capture_last_sequence = 0U;
    g_capture_has_sequence = false;
    g_capture_armed = false;
    g_capture_sent = false;
#endif

#if M55M1_RUNTIME_TELEMETRY || M55M1_AUDIO_RELEASE_PROBE
    g_frame_count = 0U;
    g_debug_stats = (audio_pipeline_debug_stats_t){0};
#endif
    g_initialized = true;
    AUDIO_PIPELINE_TRACE_INIT("pipeline", "OK", 0);
    return true;
}

bool audio_pipeline_start(void) {
    if (!g_initialized || g_running) {
        return false;
    }
    deepfir_inference_reset();
    memset(&g_audio_working_set, 0, sizeof(g_audio_working_set));
    g_expected_sequence = 1U;
    AUDIO_PIPELINE_TRACE_INIT("transport_start", "BEGIN", 0);
    const audio_transport_status_t start_status = audio_transport_start();
    if (start_status != AUDIO_TRANSPORT_STATUS_OK) {
        audio_transport_stop_safe();
        g_initialized = false;
        AUDIO_PIPELINE_TRACE_INIT("transport_start", "FAIL", (int)start_status);
        return false;
    }
    g_running = true;
    AUDIO_PIPELINE_TRACE_INIT("stream", "RUNNING", 0);
#if M55M1_RUNTIME_TELEMETRY
    g_boot_trace = false;
#endif
#if M55M1_AUDIO_UART_PCM_CAPTURE
    if (!debug_uart_tx_try_enqueue("READY\r\n", 7U)) {
        audio_pipeline_stop_safe();
        return false;
    }
#endif
    return true;
}

void audio_pipeline_wait_for_work(void) {
    if (g_running) {
        audio_transport_wait_for_work();
    }
}

void audio_pipeline_poll(void) {
    audio_transport_capture_timing_t timing;

    if (!g_running) {
        return;
    }
    if (transport_faulted()) {
        ++g_stats.transport_faults;
        restart_stream(AUDIO_PIPELINE_FAULT_TRANSPORT, 0U);
        return;
    }
#if M55M1_AUDIO_UART_PCM_CAPTURE
    if (!g_capture_armed && !g_capture_sent) {
        uint8_t command = 0U;
        if (debug_uart_rx_try_read(&command) && (command == (uint8_t)'C')) {
            g_capture_armed = true;
            (void)debug_uart_tx_try_enqueue("CAPTURE_ARMED\r\n", 15U);
        }
    }
#endif
    if (!audio_transport_take_capture(g_audio_working_set.capture, &timing)) {
        return;
    }
    if (timing.sequence != g_expected_sequence) {
        ++g_stats.sequence_faults;
        restart_stream(AUDIO_PIPELINE_FAULT_SEQUENCE, timing.sequence);
        return;
    }
    g_stats.last_sequence = timing.sequence;
    g_stats.capture_sample_position =
        ((uint64_t)timing.sequence - 1U) * AUDIO_TRANSPORT_SAMPLES_PER_BLOCK;
    g_stats.scheduled_playback_slot = timing.sequence + 1U;
    g_stats.capture_completed_cycle = timing.completed_cycle;
    g_stats.process_end_cycle = 0U;
    g_stats.published_cycle = 0U;
    g_stats.process_start_cycle = audio_transport_cycles_now();
    record_input_levels(g_audio_working_set.capture);

#if M55M1_RUNTIME_TELEMETRY || M55M1_AUDIO_RELEASE_PROBE
    ++g_frame_count;
#if M55M1_AUDIO_RELEASE_PROBE
    g_debug_stats.capture_last_sequence = timing.sequence;
#endif
    record_pcm24_stats(g_audio_working_set.capture, &g_debug_stats.input_peak,
                     &g_debug_stats.input_clip_samples);
#endif
    if (!deepfir_inference_process_pcm24(g_audio_working_set.capture, g_audio_working_set.playback,
                                   AUDIO_TRANSPORT_SAMPLES_PER_BLOCK)) {
#if M55M1_RUNTIME_TELEMETRY || M55M1_AUDIO_RELEASE_PROBE
        ++g_debug_stats.model_failures;
        record_progress(timing.completed_cycle, false);
#if M55M1_AUDIO_RELEASE_PROBE
        finish_release_probe_if_ready(false, 0U, 0U);
#endif
#endif
#if M55M1_AUDIO_UART_PCM_CAPTURE
        if (g_capture_armed) {
            capture_send_packet(1U);
            return;
        }
#endif
        ++g_stats.model_faults;
        restart_stream(AUDIO_PIPELINE_FAULT_INFERENCE, 0U);
        return;
    }
    g_stats.process_end_cycle = audio_transport_cycles_now();
    if (audio_transport_cycles_to_us(audio_transport_cycles_elapsed(
            timing.completed_cycle, g_stats.process_end_cycle)) >= 1000U) {
        ++g_stats.deadline_misses;
        restart_stream(AUDIO_PIPELINE_FAULT_PROCESS_DEADLINE,
                       audio_transport_cycles_to_us(audio_transport_cycles_elapsed(
                           timing.completed_cycle, g_stats.process_end_cycle)));
        return;
    }
    if (transport_faulted()) {
        ++g_stats.transport_faults;
        restart_stream(AUDIO_PIPELINE_FAULT_TRANSPORT, 0U);
        return;
    }
#if M55M1_RUNTIME_TELEMETRY || M55M1_AUDIO_RELEASE_PROBE
    record_pcm_stats(g_audio_working_set.playback, &g_debug_stats.output_peak,
                     &g_debug_stats.output_clip_samples);
#endif

#if M55M1_ENABLE_DAC_I2S_OUTPUT
    const audio_transport_status_t submit_status = audio_transport_submit_tagged(
        g_audio_working_set.playback, &(audio_transport_block_tag_t){
                                          .sequence = timing.sequence,
                                          .capture_completed_cycle = timing.completed_cycle,
                                      });
#if M55M1_RUNTIME_TELEMETRY || M55M1_AUDIO_RELEASE_PROBE
    if (submit_status == AUDIO_TRANSPORT_STATUS_BUSY) {
        ++g_debug_stats.submit_busy;
    } else if (submit_status != AUDIO_TRANSPORT_STATUS_OK) {
        ++g_debug_stats.submit_errors;
    }
#endif
    if (submit_status != AUDIO_TRANSPORT_STATUS_OK) {
        ++g_stats.transport_faults;
        restart_stream(AUDIO_PIPELINE_FAULT_SUBMIT, (uint32_t)submit_status);
        return;
    }
#if M55M1_AUDIO_UART_PCM_CAPTURE
    if (g_capture_armed) {
        capture_store_hop(g_audio_working_set.capture, g_audio_working_set.playback,
                          timing.sequence);
        if (!g_running) {
            return;
        }
    }
#endif
#endif
    g_stats.published_cycle = audio_transport_cycles_now();
    const uint32_t elapsed_us = audio_transport_cycles_to_us(
        audio_transport_cycles_elapsed(timing.completed_cycle, g_stats.published_cycle));
    if (elapsed_us > g_stats.max_capture_to_publish_us) {
        g_stats.max_capture_to_publish_us = elapsed_us;
    }
    if (elapsed_us >= 1000U) {
        ++g_stats.deadline_misses;
        restart_stream(AUDIO_PIPELINE_FAULT_PUBLISH_DEADLINE, elapsed_us);
        return;
    }
    ++g_expected_sequence;
#if M55M1_RUNTIME_TELEMETRY || M55M1_AUDIO_RELEASE_PROBE
    record_progress(timing.completed_cycle, true);
#if M55M1_AUDIO_RELEASE_PROBE
    finish_release_probe_if_ready(false, 0U, 0U);
#endif
#endif
}

void audio_pipeline_stop_safe(void) {
    if (g_running) {
        audio_transport_stop_safe();
    }

    g_initialized = false;
    g_running = false;
    deepfir_inference_reset();
}
