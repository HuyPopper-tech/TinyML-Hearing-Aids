/**
 * @file audio_telemetry.c
 * @brief One-hertz bounded audio performance and acoustic telemetry.
 * @author Bui Minh Huy
 * @date 2026-07-29
 */

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "drivers/debug_uart_tx/debug_uart_tx.h"
#include "services/audio_telemetry.h"

#define AUDIO_TELEMETRY_INTERVAL_FRAMES 100U
#define AUDIO_TELEMETRY_MESSAGE_BYTES 1536U
#define AUDIO_TELEMETRY_E2E_LIMIT_US 16000U

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
static uint16_t g_fft_us[AUDIO_TELEMETRY_INTERVAL_FRAMES];
static uint16_t g_feature_us[AUDIO_TELEMETRY_INTERVAL_FRAMES];
static uint16_t g_df_us[AUDIO_TELEMETRY_INTERVAL_FRAMES];
static uint16_t g_wola_us[AUDIO_TELEMETRY_INTERVAL_FRAMES];
static uint16_t g_transport_us[AUDIO_TELEMETRY_INTERVAL_FRAMES];
static uint16_t g_processing_count;
static uint16_t g_invoke_count;
static uint16_t g_e2e_count;
static uint16_t g_fft_count;
static uint16_t g_feature_count;
static uint16_t g_df_count;
static uint16_t g_wola_count;
static uint16_t g_transport_count;
static uint32_t g_e2e_gap_count;
static uint32_t g_e2e_over_limit_count;
static uint32_t g_last_e2e_sequence;
static bool g_have_e2e_sequence;
static uint32_t g_transport_gap_count;
static uint32_t g_last_transport_sequence;
static bool g_have_transport_sequence;
static audio_telemetry_audio_stats_t g_audio_window;
static bool g_audio_window_valid;

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
    g_fft_count = 0U;
    g_feature_count = 0U;
    g_df_count = 0U;
    g_wola_count = 0U;
    g_transport_count = 0U;
    memset(&g_audio_window, 0, sizeof(g_audio_window));
    g_audio_window_valid = false;
}

void audio_telemetry_init(void) {
    g_pending_length = 0U;
    g_last_snapshot_frame = 0U;
    g_e2e_gap_count = 0U;
    g_e2e_over_limit_count = 0U;
    g_have_e2e_sequence = false;
    g_transport_gap_count = 0U;
    g_have_transport_sequence = false;
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

void audio_telemetry_record_fft_us(uint32_t elapsed_us) {
    record_sample(g_fft_us, &g_fft_count, elapsed_us);
}

void audio_telemetry_record_feature_us(uint32_t elapsed_us) {
    record_sample(g_feature_us, &g_feature_count, elapsed_us);
}

void audio_telemetry_record_df_us(uint32_t elapsed_us) {
    record_sample(g_df_us, &g_df_count, elapsed_us);
}

void audio_telemetry_record_wola_us(uint32_t elapsed_us) {
    record_sample(g_wola_us, &g_wola_count, elapsed_us);
}

void audio_telemetry_record_transport_us(uint32_t elapsed_us, uint32_t sequence) {
    if (g_have_transport_sequence) {
        const uint32_t delta = sequence - g_last_transport_sequence;
        if (delta > 1U) {
            g_transport_gap_count += delta - 1U;
        }
    }
    g_last_transport_sequence = sequence;
    g_have_transport_sequence = true;
    record_sample(g_transport_us, &g_transport_count, elapsed_us);
}

#define AUDIO_STATS_RANGE(field) \
    do { \
        if (!g_audio_window_valid || (stats->field##_min < g_audio_window.field##_min)) { \
            g_audio_window.field##_min = stats->field##_min; \
        } \
        if (!g_audio_window_valid || (stats->field##_max > g_audio_window.field##_max)) { \
            g_audio_window.field##_max = stats->field##_max; \
        } \
    } while (0)

void audio_telemetry_record_audio_stats(
    const audio_telemetry_audio_stats_t *stats) {
    if (stats == NULL) {
        return;
    }
    AUDIO_STATS_RANGE(feature);
    AUDIO_STATS_RANGE(complex);
    AUDIO_STATS_RANGE(gain);
    AUDIO_STATS_RANGE(df);
    AUDIO_STATS_RANGE(alpha);
    AUDIO_STATS_RANGE(erb_cache);
    AUDIO_STATS_RANGE(complex_cache);
    AUDIO_STATS_RANGE(enc0);
    AUDIO_STATS_RANGE(enc1);
    AUDIO_STATS_RANGE(enc2);
    AUDIO_STATS_RANGE(df0);
    AUDIO_STATS_RANGE(df1);
    g_audio_window.frames += stats->frames;
    g_audio_window.pcm_samples += stats->pcm_samples;
    g_audio_window.input_energy += stats->input_energy;
    g_audio_window.model_input_energy += stats->model_input_energy;
    g_audio_window.output_energy += stats->output_energy;
    if (stats->input_peak > g_audio_window.input_peak) {
        g_audio_window.input_peak = stats->input_peak;
    }
    if (stats->model_input_peak > g_audio_window.model_input_peak) {
        g_audio_window.model_input_peak = stats->model_input_peak;
    }
    if (stats->output_peak > g_audio_window.output_peak) {
        g_audio_window.output_peak = stats->output_peak;
    }
    g_audio_window.input_clip_count += stats->input_clip_count;
    g_audio_window.model_input_clip_count += stats->model_input_clip_count;
    g_audio_window.output_clip_count += stats->output_clip_count;
    g_audio_window.feature_sat_low += stats->feature_sat_low;
    g_audio_window.feature_sat_high += stats->feature_sat_high;
    g_audio_window.feature_count += stats->feature_count;
    g_audio_window.feature_sum += stats->feature_sum;
    g_audio_window.complex_sat_count += stats->complex_sat_count;
    g_audio_window.gain_sat_count += stats->gain_sat_count;
    g_audio_window.df_sat_count += stats->df_sat_count;
    g_audio_window.alpha_sat_count += stats->alpha_sat_count;
    g_audio_window.erb_cache_sat_count += stats->erb_cache_sat_count;
    g_audio_window.complex_cache_sat_count += stats->complex_cache_sat_count;
    g_audio_window.enc0_sat_count += stats->enc0_sat_count;
    g_audio_window.enc1_sat_count += stats->enc1_sat_count;
    g_audio_window.enc2_sat_count += stats->enc2_sat_count;
    g_audio_window.df0_sat_count += stats->df0_sat_count;
    g_audio_window.df1_sat_count += stats->df1_sat_count;
    g_audio_window.gain_count += stats->gain_count;
    g_audio_window.gain_sum += stats->gain_sum;
    g_audio_window.gain_near_zero += stats->gain_near_zero;
    g_audio_window.gain_near_unity += stats->gain_near_unity;
    g_audio_window.spectrum_input_energy_q30 +=
        stats->spectrum_input_energy_q30;
    g_audio_window.spectrum_output_energy_q30 +=
        stats->spectrum_output_energy_q30;
    g_audio_window.ifft_energy_q30 += stats->ifft_energy_q30;
    g_audio_window.wola_energy_q30 += stats->wola_energy_q30;
    g_audio_window.ifft_sample_count += stats->ifft_sample_count;
    g_audio_window.wola_sample_count += stats->wola_sample_count;
    g_audio_window.bin_gain_count += stats->bin_gain_count;
    g_audio_window.bin_gain_sum_q15 += stats->bin_gain_sum_q15;
    if (!g_audio_window_valid ||
        (stats->bin_gain_min_q15 < g_audio_window.bin_gain_min_q15)) {
        g_audio_window.bin_gain_min_q15 = stats->bin_gain_min_q15;
    }
    if (!g_audio_window_valid ||
        (stats->bin_gain_max_q15 > g_audio_window.bin_gain_max_q15)) {
        g_audio_window.bin_gain_max_q15 = stats->bin_gain_max_q15;
    }
    if (stats->wola_peak_q15 > g_audio_window.wola_peak_q15) {
        g_audio_window.wola_peak_q15 = stats->wola_peak_q15;
    }
    if (!g_audio_window_valid) {
        g_audio_window_valid = true;
    }
}
#undef AUDIO_STATS_RANGE

void audio_telemetry_boot(const audio_telemetry_boot_info_t *info) {
    int length;

    if (info == NULL) {
        return;
    }
    length = snprintf(
        g_message, sizeof(g_message),
        "[BOOT] build=%s git=%s dirty=%lu hop=%lu frame=%lu "
        "core_hz=%lu dsp_mode=%s input_gain_db=%lu model_hash=%s model_bytes=%lu diagnostics=1\r\n",
        (info->build == NULL) ? "unknown" : info->build,
        (info->git_rev == NULL) ? "unknown" : info->git_rev,
        (unsigned long)info->git_dirty,
        (unsigned long)info->hop_samples, (unsigned long)info->frame_samples,
        (unsigned long)info->core_hz,
        (info->dsp_mode == NULL) ? "unknown" : info->dsp_mode,
        (unsigned long)info->input_gain_db,
        (info->model_hash == NULL) ? "unknown" : info->model_hash,
        (unsigned long)info->model_bytes);
    if ((length > 0) && ((size_t)length < sizeof(g_message))) {
        (void)debug_uart_tx_try_enqueue(g_message, (size_t)length);
    }
}

void audio_telemetry_poll(
    const audio_telemetry_snapshot_t *snapshot, bool dmic_block_pending) {
    audio_telemetry_window_stats_t processing;
    audio_telemetry_window_stats_t invoke;
    audio_telemetry_window_stats_t e2e;
    audio_telemetry_window_stats_t fft;
    audio_telemetry_window_stats_t feature;
    audio_telemetry_window_stats_t df;
    audio_telemetry_window_stats_t wola;
    audio_telemetry_window_stats_t transport;
    const audio_telemetry_audio_stats_t *audio;
    uint32_t input_rms;
    uint32_t model_input_rms;
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
    fft = window_stats(g_fft_us, g_fft_count);
    feature = window_stats(g_feature_us, g_feature_count);
    df = window_stats(g_df_us, g_df_count);
    wola = window_stats(g_wola_us, g_wola_count);
    transport = window_stats(g_transport_us, g_transport_count);
    audio = g_audio_window_valid ? &g_audio_window : &snapshot->audio;
    input_rms = (audio->pcm_samples == 0U) ? 0U :
        integer_sqrt_u64(audio->input_energy / audio->pcm_samples);
    model_input_rms = (audio->pcm_samples == 0U) ? 0U :
        integer_sqrt_u64(audio->model_input_energy / audio->pcm_samples);
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
        "[PERF] f=%lu fft_us=%lu/%lu/%lu feature_us=%lu/%lu/%lu "
        "npu_us=%lu/%lu/%lu df_us=%lu/%lu/%lu wola_us=%lu/%lu/%lu "
        "e2e_us=%lu/%lu/%lu transport_us=%lu/%lu/%lu "
        "enh_us=%lu/%lu/%lu e2e_n=%lu transport_n=%lu e2e_gap=%lu "
        "transport_gap=%lu e2e_16ms=%lu "
        "fallback=%lu deadline=%lu dmic_ovr=%lu dmic_dma=%lu dac_busy=%lu "
        "underrun=%lu dac_dma=%lu npu_error=%lu fault_injected=%lu "
        "parity_pcm_lsb=%lu parity_violation=%lu uart_drop=%lu\r\n"
        "[MODEL] mac_inf=%lu npu_ops=%lu cpu_ops=%lu active_mmac_s_x1000=%lu "
        "stream_mmac_s_x1000=%lu duty_permille=%lu invoke_count=%lu "
        "model_flash=%lu arena=%lu/%lu dsp_static=%lu stack_cfg=%lu\r\n"
        "[AUDIO] frames=%lu raw_peak=%lu raw_rms=%lu raw_clip=%lu "
        "model_peak=%lu model_rms=%lu model_clip=%lu "
        "out_peak=%lu out_rms=%lu out_clip=%lu rms_ratio_milli=%lu "
        "feat_q=%ld/%ld/%ld feat_sat=%lu/%lu gain_milli=%lu/%lu/%lu "
        "gain_zero_pct=%lu gain_unity_pct=%lu state1_q=%ld/%ld "
        "state2_q=%ld/%ld state_sat=%lu\r\n"
        "[DSP] valid=%lu bin_gain_milli=%lu/%lu/%lu spec_ratio_milli=%lu "
        "ifft_rms=%lu wola_rms=%lu wola_peak=%lu\r\n",
        (unsigned long)snapshot->frame_count,
        (unsigned long)fft.average,
        (unsigned long)fft.maximum,
        (unsigned long)fft.p99,
        (unsigned long)feature.average,
        (unsigned long)feature.maximum,
        (unsigned long)feature.p99,
        (unsigned long)invoke.average,
        (unsigned long)invoke.maximum,
        (unsigned long)invoke.p99,
        (unsigned long)df.average,
        (unsigned long)df.maximum,
        (unsigned long)df.p99,
        (unsigned long)wola.average,
        (unsigned long)wola.maximum,
        (unsigned long)wola.p99,
        (unsigned long)e2e.average,
        (unsigned long)e2e.maximum,
        (unsigned long)e2e.p99,
        (unsigned long)transport.average,
        (unsigned long)transport.maximum,
        (unsigned long)transport.p99,
        (unsigned long)processing.average,
        (unsigned long)processing.maximum,
        (unsigned long)processing.p99,
        (unsigned long)e2e.count,
        (unsigned long)transport.count,
        (unsigned long)g_e2e_gap_count,
        (unsigned long)g_transport_gap_count,
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
        (unsigned long)snapshot->parity_max_pcm_difference,
        (unsigned long)snapshot->parity_violation_count,
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
        (unsigned long)audio->model_input_peak,
        (unsigned long)model_input_rms,
        (unsigned long)audio->model_input_clip_count,
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
    if (snapshot->diagnostics_enabled != 0U &&
        length > 0 && (size_t)length < sizeof(g_message)) {
        length += snprintf(
            g_message + length, sizeof(g_message) - (size_t)length,
            "[DFN] complex_q=%ld/%ld/%lu gain_q=%ld/%ld/%lu "
            "df_q=%ld/%ld/%lu alpha_q=%ld/%ld/%lu "
            "erb_cache_q=%ld/%ld/%lu complex_cache_q=%ld/%ld/%lu "
            "enc0_q=%ld/%ld/%lu enc1_q=%ld/%ld/%lu enc2_q=%ld/%ld/%lu "
            "df0_q=%ld/%ld/%lu df1_q=%ld/%ld/%lu\r\n",
            (long)audio->complex_min, (long)audio->complex_max,
            (unsigned long)audio->complex_sat_count,
            (long)audio->gain_min, (long)audio->gain_max,
            (unsigned long)audio->gain_sat_count,
            (long)audio->df_min, (long)audio->df_max,
            (unsigned long)audio->df_sat_count,
            (long)audio->alpha_min, (long)audio->alpha_max,
            (unsigned long)audio->alpha_sat_count,
            (long)audio->erb_cache_min, (long)audio->erb_cache_max,
            (unsigned long)audio->erb_cache_sat_count,
            (long)audio->complex_cache_min, (long)audio->complex_cache_max,
            (unsigned long)audio->complex_cache_sat_count,
            (long)audio->enc0_min, (long)audio->enc0_max,
            (unsigned long)audio->enc0_sat_count,
            (long)audio->enc1_min, (long)audio->enc1_max,
            (unsigned long)audio->enc1_sat_count,
            (long)audio->enc2_min, (long)audio->enc2_max,
            (unsigned long)audio->enc2_sat_count,
            (long)audio->df0_min, (long)audio->df0_max,
            (unsigned long)audio->df0_sat_count,
            (long)audio->df1_min, (long)audio->df1_max,
            (unsigned long)audio->df1_sat_count);
        if (length > 0 && (size_t)length < sizeof(g_message)) {
            length += snprintf(
                g_message + length, sizeof(g_message) - (size_t)length,
                "[STACK] cfg=%lu high_water=%lu headroom=%lu gate=%s\r\n",
                (unsigned long)snapshot->stack_configured_bytes,
                (unsigned long)snapshot->stack_high_water_bytes,
                (unsigned long)snapshot->stack_headroom_bytes,
                (snapshot->stack_headroom_bytes >= 2048U) ? "pass" : "pending");
        }
    }
    if ((snapshot->diagnostics_enabled == 0U) &&
        (length > 0) && ((size_t)length < sizeof(g_message))) {
        length += snprintf(g_message + length,
                           sizeof(g_message) - (size_t)length,
                           "e2e_16ms=%lu\r\n",
                           (unsigned long)g_e2e_over_limit_count);
    }
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
