/**
 * @file audio_telemetry.c
 * @brief One-hertz bounded ERB-GRU performance and acoustic telemetry.
 * @author Bui Minh Huy
 * @date 2026-07-29
 */

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "drivers/debug_uart_tx/debug_uart_tx.h"
#include "services/audio_telemetry.h"

#define AUDIO_TELEMETRY_INTERVAL_FRAMES 100U
#define AUDIO_TELEMETRY_MESSAGE_BYTES 896U
#define AUDIO_TELEMETRY_E2E_LIMIT_US 40000U

typedef struct {
    uint32_t average;
    uint32_t maximum;
    uint32_t p99;
    uint32_t count;
    uint64_t sum;
} audio_telemetry_window_stats_t;

static char g_message[AUDIO_TELEMETRY_MESSAGE_BYTES];
static size_t g_pending_length;
static uint32_t g_last_snapshot_frame;
static uint16_t g_processing_us[AUDIO_TELEMETRY_INTERVAL_FRAMES];
static uint16_t g_invoke_us[AUDIO_TELEMETRY_INTERVAL_FRAMES];
static uint16_t g_e2e_us[AUDIO_TELEMETRY_INTERVAL_FRAMES];
static uint16_t g_processing_count;
static uint16_t g_invoke_count;
static uint16_t g_e2e_count;
static uint32_t g_e2e_gap_count;
static uint32_t g_e2e_over_limit_count;
static uint32_t g_last_e2e_sequence;
static bool g_have_e2e_sequence;

static uint16_t bounded_u16(uint32_t value) {
    return (value > UINT16_MAX) ? UINT16_MAX : (uint16_t)value;
}

static void record_sample(
    uint16_t samples[AUDIO_TELEMETRY_INTERVAL_FRAMES],
    uint16_t *count,
    uint32_t value) {
    if (*count < AUDIO_TELEMETRY_INTERVAL_FRAMES) {
        samples[*count] = bounded_u16(value);
        ++(*count);
    }
}

static audio_telemetry_window_stats_t window_stats(
    const uint16_t *samples, uint16_t count) {
    audio_telemetry_window_stats_t result = {0U, 0U, 0U, count, 0U};
    uint16_t sorted[AUDIO_TELEMETRY_INTERVAL_FRAMES];

    if (count == 0U) {
        return result;
    }
    memcpy(sorted, samples, (size_t)count * sizeof(sorted[0]));
    for (uint16_t index = 1U; index < count; ++index) {
        const uint16_t value = sorted[index];
        uint16_t position = index;

        while ((position > 0U) && (sorted[position - 1U] > value)) {
            sorted[position] = sorted[position - 1U];
            --position;
        }
        sorted[position] = value;
    }
    for (uint16_t index = 0U; index < count; ++index) {
        result.sum += samples[index];
        if (samples[index] > result.maximum) {
            result.maximum = samples[index];
        }
    }
    result.average = (uint32_t)(result.sum / count);
    // Nearest-rank p99 keeps the bounded 100-frame window integer-only.
    result.p99 = sorted[(((uint32_t)count * 99U + 99U) / 100U) - 1U];
    return result;
}

static uint32_t integer_sqrt_u64(uint64_t value) {
    uint64_t bit = 1ULL << 62;
    uint64_t result = 0U;

    while (bit > value) {
        bit >>= 2U;
    }
    while (bit != 0U) {
        if (value >= (result + bit)) {
            value -= result + bit;
            result = (result >> 1U) + bit;
        } else {
            result >>= 1U;
        }
        bit >>= 2U;
    }
    return (uint32_t)result;
}

static uint32_t sqrt_ratio_milli(uint64_t numerator, uint64_t denominator) {
    if (denominator == 0U) {
        return 0U;
    }
    /*
     * Rescale numerator and denominator together before multiplying by
     * 1,000,000, preserving the ratio while preventing uint64_t overflow.
     */
    while (numerator > (UINT64_MAX / 1000000ULL)) {
        numerator >>= 1U;
        denominator = (denominator >> 1U) + (denominator & 1U);
    }
    return integer_sqrt_u64((numerator * 1000000ULL) / denominator);
}

static uint32_t gain_q_to_milli(int32_t quantized) {
    int32_t shifted = quantized + 128;

    if (shifted < 0) {
        shifted = 0;
    } else if (shifted > 255) {
        shifted = 255;
    }
    return (uint32_t)((shifted * 1000 + 128) / 256);
}

static uint32_t q15_to_milli(uint64_t value) {
    return (uint32_t)((value * 1000U + 16384U) / 32768U);
}

static int32_t mean_int8(int64_t sum, uint32_t count) {
    return (count == 0U) ? 0 : (int32_t)(sum / (int64_t)count);
}

static uint32_t percentage(uint32_t numerator, uint32_t denominator) {
    return (denominator == 0U) ? 0U :
        (uint32_t)(((uint64_t)numerator * 100U) / denominator);
}

static void clear_windows(void) {
    g_processing_count = 0U;
    g_invoke_count = 0U;
    g_e2e_count = 0U;
}

void audio_telemetry_init(void) {
    g_pending_length = 0U;
    g_last_snapshot_frame = 0U;
    g_e2e_gap_count = 0U;
    g_e2e_over_limit_count = 0U;
    g_have_e2e_sequence = false;
    clear_windows();
    debug_uart_tx_init();
}

void audio_telemetry_record_processing_us(uint32_t elapsed_us) {
    record_sample(g_processing_us, &g_processing_count, elapsed_us);
}

void audio_telemetry_record_invoke_us(uint32_t elapsed_us) {
    record_sample(g_invoke_us, &g_invoke_count, elapsed_us);
}

void audio_telemetry_record_e2e_us(uint32_t elapsed_us, uint32_t sequence) {
    if (g_have_e2e_sequence) {
        const uint32_t delta = sequence - g_last_e2e_sequence;

        if (delta > 1U) {
            g_e2e_gap_count += delta - 1U;
        }
    }
    g_last_e2e_sequence = sequence;
    g_have_e2e_sequence = true;
    if (elapsed_us > AUDIO_TELEMETRY_E2E_LIMIT_US) {
        ++g_e2e_over_limit_count;
    }
    record_sample(g_e2e_us, &g_e2e_count, elapsed_us);
}

void audio_telemetry_poll(
    const audio_telemetry_snapshot_t *snapshot, bool dmic_block_pending) {
    audio_telemetry_window_stats_t processing;
    audio_telemetry_window_stats_t invoke;
    audio_telemetry_window_stats_t e2e;
    const erb_gru_audio_diagnostics_t *audio;
    uint32_t input_rms;
    uint32_t output_rms;
    uint32_t rms_ratio_milli;
    uint32_t active_mmac_s_x1000;
    uint32_t stream_mmac_s_x1000;
    uint32_t duty_permille;
    uint32_t gain_min_milli;
    uint32_t gain_mean_milli;
    uint32_t gain_max_milli;
    uint32_t dsp_valid;
    uint32_t bin_gain_min_milli;
    uint32_t bin_gain_mean_milli;
    uint32_t bin_gain_max_milli;
    uint32_t spectrum_ratio_milli;
    uint32_t ifft_rms;
    uint32_t wola_rms;
    int length;

    if ((snapshot == NULL) || dmic_block_pending) {
        return;
    }
    /*
     * A previously formatted snapshot remains intact until the UART ring can
     * accept the whole message. Window counters reset only after that atomic
     * enqueue succeeds, so UART pressure cannot erase diagnostic evidence.
     */
    if (g_pending_length != 0U) {
        if (debug_uart_tx_try_enqueue(g_message, g_pending_length)) {
            g_pending_length = 0U;
            g_last_snapshot_frame = snapshot->frame_count;
            clear_windows();
        }
        return;
    }
    if ((snapshot->frame_count == 0U) ||
        ((snapshot->frame_count - g_last_snapshot_frame) <
         AUDIO_TELEMETRY_INTERVAL_FRAMES)) {
        return;
    }

    processing = window_stats(g_processing_us, g_processing_count);
    invoke = window_stats(g_invoke_us, g_invoke_count);
    e2e = window_stats(g_e2e_us, g_e2e_count);
    audio = &snapshot->audio;
    input_rms = (audio->pcm_samples == 0U) ? 0U :
        integer_sqrt_u64(audio->input_energy / audio->pcm_samples);
    output_rms = (audio->pcm_samples == 0U) ? 0U :
        integer_sqrt_u64(audio->output_energy / audio->pcm_samples);
    rms_ratio_milli = (input_rms == 0U) ? 0U :
        (uint32_t)(((uint64_t)output_rms * 1000U) / input_rms);
    active_mmac_s_x1000 = (invoke.sum == 0U) ? 0U :
        (uint32_t)(((uint64_t)snapshot->macs_per_inference *
                    invoke.count * 1000U) /
                   invoke.sum);
    stream_mmac_s_x1000 = (uint32_t)(
        ((uint64_t)snapshot->macs_per_inference * 100U * 1000U + 500000U) /
        1000000U);
    duty_permille = (processing.count == 0U) ? 0U :
        (uint32_t)((invoke.sum * 1000U) /
                   ((uint64_t)processing.count * 10000U));
    gain_min_milli = (audio->gain_count == 0U) ? 0U :
        gain_q_to_milli(audio->gain_min);
    gain_mean_milli = (audio->gain_count == 0U) ? 0U :
        gain_q_to_milli(mean_int8(audio->gain_sum, audio->gain_count));
    gain_max_milli = (audio->gain_count == 0U) ? 0U :
        gain_q_to_milli(audio->gain_max);
    dsp_valid = ((audio->bin_gain_count != 0U) &&
                 (audio->ifft_sample_count != 0U) &&
                 (audio->wola_sample_count != 0U)) ? 1U : 0U;
    bin_gain_min_milli = (audio->bin_gain_count == 0U) ? 0U :
        q15_to_milli(audio->bin_gain_min_q15);
    bin_gain_mean_milli = (audio->bin_gain_count == 0U) ? 0U :
        q15_to_milli(audio->bin_gain_sum_q15 / audio->bin_gain_count);
    bin_gain_max_milli = (audio->bin_gain_count == 0U) ? 0U :
        q15_to_milli(audio->bin_gain_max_q15);
    spectrum_ratio_milli = sqrt_ratio_milli(
        audio->spectrum_output_energy_q30,
        audio->spectrum_input_energy_q30);
    ifft_rms = (audio->ifft_sample_count == 0U) ? 0U :
        integer_sqrt_u64(audio->ifft_energy_q30 / audio->ifft_sample_count);
    wola_rms = (audio->wola_sample_count == 0U) ? 0U :
        integer_sqrt_u64(audio->wola_energy_q30 / audio->wola_sample_count);

    length = snprintf(
        g_message, sizeof(g_message),
        "[PERF] f=%lu enh_us=%lu/%lu/%lu npu_us=%lu/%lu/%lu dsp_us=%lu "
        "e2e_us=%lu/%lu/%lu e2e_n=%lu e2e_gap=%lu e2e_40ms=%lu "
        "fallback=%lu deadline=%lu dmic_ovr=%lu dmic_dma=%lu dac_busy=%lu "
        "underrun=%lu dac_dma=%lu npu_error=%lu fault_injected=%lu "
        "uart_drop=%lu\r\n"
        "[MODEL] mac_inf=%lu npu_ops=%lu cpu_ops=%lu active_mmac_s_x1000=%lu "
        "stream_mmac_s_x1000=%lu duty_permille=%lu invoke_count=%lu "
        "model_flash=%lu arena=%lu/%lu dsp_static=%lu stack_cfg=%lu\r\n"
        "[AUDIO] frames=%lu in_peak=%lu in_rms=%lu in_clip=%lu "
        "out_peak=%lu out_rms=%lu out_clip=%lu rms_ratio_milli=%lu "
        "feat_q=%ld/%ld/%ld feat_sat=%lu/%lu gain_milli=%lu/%lu/%lu "
        "gain_zero_pct=%lu gain_unity_pct=%lu state1_q=%ld/%ld "
        "state2_q=%ld/%ld state_sat=%lu\r\n"
        "[DSP] valid=%lu bin_gain_milli=%lu/%lu/%lu spec_ratio_milli=%lu "
        "ifft_rms=%lu wola_rms=%lu wola_peak=%lu\r\n",
        (unsigned long)snapshot->frame_count,
        (unsigned long)processing.average,
        (unsigned long)processing.maximum,
        (unsigned long)processing.p99,
        (unsigned long)invoke.average,
        (unsigned long)invoke.maximum,
        (unsigned long)invoke.p99,
        (unsigned long)((processing.average > invoke.average) ?
            (processing.average - invoke.average) : 0U),
        (unsigned long)e2e.average,
        (unsigned long)e2e.maximum,
        (unsigned long)e2e.p99,
        (unsigned long)e2e.count,
        (unsigned long)g_e2e_gap_count,
        (unsigned long)g_e2e_over_limit_count,
        (unsigned long)snapshot->unity_fallback,
        (unsigned long)snapshot->deadline_miss,
        (unsigned long)snapshot->dmic_overrun,
        (unsigned long)snapshot->dmic_dma_error,
        (unsigned long)snapshot->dac_busy,
        (unsigned long)snapshot->dac_underrun,
        (unsigned long)snapshot->dac_dma_error,
        (unsigned long)snapshot->npu_error,
        (unsigned long)snapshot->fault_injected,
        (unsigned long)debug_uart_tx_drop_count(),
        (unsigned long)snapshot->macs_per_inference,
        (unsigned long)snapshot->npu_operator_count,
        (unsigned long)snapshot->cpu_operator_count,
        (unsigned long)active_mmac_s_x1000,
        (unsigned long)stream_mmac_s_x1000,
        (unsigned long)duty_permille,
        (unsigned long)snapshot->invoke_count,
        (unsigned long)snapshot->model_flash_bytes,
        (unsigned long)snapshot->arena_used_bytes,
        (unsigned long)snapshot->arena_capacity_bytes,
        (unsigned long)snapshot->dsp_static_bytes,
        (unsigned long)snapshot->stack_configured_bytes,
        (unsigned long)audio->frames,
        (unsigned long)audio->input_peak,
        (unsigned long)input_rms,
        (unsigned long)audio->input_clip_count,
        (unsigned long)audio->output_peak,
        (unsigned long)output_rms,
        (unsigned long)audio->output_clip_count,
        (unsigned long)rms_ratio_milli,
        (long)((audio->feature_count == 0U) ? 0 : audio->feature_min),
        (long)mean_int8(audio->feature_sum, audio->feature_count),
        (long)((audio->feature_count == 0U) ? 0 : audio->feature_max),
        (unsigned long)audio->feature_sat_low,
        (unsigned long)audio->feature_sat_high,
        (unsigned long)gain_min_milli,
        (unsigned long)gain_mean_milli,
        (unsigned long)gain_max_milli,
        (unsigned long)percentage(audio->gain_near_zero, audio->gain_count),
        (unsigned long)percentage(audio->gain_near_unity, audio->gain_count),
        (long)((audio->gain_count == 0U) ? 0 : audio->state1_min),
        (long)((audio->gain_count == 0U) ? 0 : audio->state1_max),
        (long)((audio->gain_count == 0U) ? 0 : audio->state2_min),
        (long)((audio->gain_count == 0U) ? 0 : audio->state2_max),
        (unsigned long)audio->state_sat_count,
        (unsigned long)dsp_valid,
        (unsigned long)bin_gain_min_milli,
        (unsigned long)bin_gain_mean_milli,
        (unsigned long)bin_gain_max_milli,
        (unsigned long)spectrum_ratio_milli,
        (unsigned long)ifft_rms,
        (unsigned long)wola_rms,
        (unsigned long)audio->wola_peak_q15);
    if ((length <= 0) || ((size_t)length >= sizeof(g_message))) {
        return;
    }
    // Publish the bounded four-line snapshot as one indivisible ring message.
    g_pending_length = (size_t)length;
    if (debug_uart_tx_try_enqueue(g_message, g_pending_length)) {
        g_pending_length = 0U;
        g_last_snapshot_frame = snapshot->frame_count;
        clear_windows();
    }
}

void audio_telemetry_fatal(const char *stage, int status) {
    const int length = snprintf(
        g_message, sizeof(g_message), "[FAIL] %s (status=%d)\r\n",
        (stage == NULL) ? "unknown" : stage, status);

    if ((length > 0) && ((size_t)length < sizeof(g_message))) {
        (void)debug_uart_tx_try_enqueue(g_message, (size_t)length);
    }
}
