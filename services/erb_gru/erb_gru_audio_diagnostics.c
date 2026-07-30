/**
 * @file erb_gru_audio_diagnostics.c
 * @brief Internal integer-only ERB-GRU acoustic statistic accumulator.
 * @author Bui Minh Huy
 * @date 2026-07-29
 */

#include <limits.h>
#include <string.h>

#include "services/erb_gru/erb_gru_audio_diagnostics.h"

#define ERB_GRU_GAIN_NEAR_ZERO_MAX_Q (-116)
#define ERB_GRU_GAIN_NEAR_UNITY_MIN_Q 116

static erb_gru_audio_diagnostics_t g_diagnostics;

static void reset_extrema(void)
{
    g_diagnostics.feature_min = INT8_MAX;
    g_diagnostics.feature_max = INT8_MIN;
    g_diagnostics.gain_min = INT8_MAX;
    g_diagnostics.gain_max = INT8_MIN;
    g_diagnostics.state1_min = INT8_MAX;
    g_diagnostics.state1_max = INT8_MIN;
    g_diagnostics.state2_min = INT8_MAX;
    g_diagnostics.state2_max = INT8_MIN;
    g_diagnostics.bin_gain_min_q15 = UINT32_MAX;
}

static uint16_t pcm_amplitude(int16_t sample)
{
    const int32_t promoted = sample;
    return (uint16_t)((promoted < 0) ? -promoted : promoted);
}

static void update_int8_extrema(int8_t value, int8_t *minimum, int8_t *maximum)
{
    if (value < *minimum) {
        *minimum = value;
    }
    if (value > *maximum) {
        *maximum = value;
    }
}

void erb_gru_audio_diagnostics_reset(void)
{
    memset(&g_diagnostics, 0, sizeof(g_diagnostics));
    reset_extrema();
}

void erb_gru_audio_diagnostics_record_pcm(
    const int16_t *input, const int16_t *output, size_t count)
{
    if ((input == NULL) || (output == NULL) || (count == 0U)) {
        return;
    }
    for (size_t index = 0U; index < count; ++index) {
        const int64_t input_sample = input[index];
        const int64_t output_sample = output[index];
        const uint16_t input_amplitude = pcm_amplitude(input[index]);
        const uint16_t output_amplitude = pcm_amplitude(output[index]);

        g_diagnostics.input_energy += (uint64_t)(input_sample * input_sample);
        g_diagnostics.output_energy += (uint64_t)(output_sample * output_sample);
        if (input_amplitude > g_diagnostics.input_peak) {
            g_diagnostics.input_peak = input_amplitude;
        }
        if (output_amplitude > g_diagnostics.output_peak) {
            g_diagnostics.output_peak = output_amplitude;
        }
        if ((input[index] == INT16_MIN) || (input[index] == INT16_MAX)) {
            ++g_diagnostics.input_clip_count;
        }
        if ((output[index] == INT16_MIN) || (output[index] == INT16_MAX)) {
            ++g_diagnostics.output_clip_count;
        }
    }
    ++g_diagnostics.frames;
    g_diagnostics.pcm_samples += (uint32_t)count;
}

void erb_gru_audio_diagnostics_record_feature(
    const int8_t *feature, size_t count)
{
    if (feature == NULL) {
        return;
    }
    for (size_t index = 0U; index < count; ++index) {
        const int8_t value = feature[index];

        g_diagnostics.feature_sum += value;
        update_int8_extrema(
            value, &g_diagnostics.feature_min, &g_diagnostics.feature_max);
        if (value == INT8_MIN) {
            ++g_diagnostics.feature_sat_low;
        } else if (value == INT8_MAX) {
            ++g_diagnostics.feature_sat_high;
        }
    }
    g_diagnostics.feature_count += (uint32_t)count;
}

void erb_gru_audio_diagnostics_record_gain_state(
    const int8_t *gain,
    size_t gain_count,
    const int8_t *state1,
    const int8_t *state2,
    size_t state_count)
{
    if ((gain == NULL) || (state1 == NULL) || (state2 == NULL)) {
        return;
    }
    for (size_t index = 0U; index < gain_count; ++index) {
        const int8_t value = gain[index];

        g_diagnostics.gain_sum += value;
        update_int8_extrema(
            value, &g_diagnostics.gain_min, &g_diagnostics.gain_max);
        if (value <= ERB_GRU_GAIN_NEAR_ZERO_MAX_Q) {
            ++g_diagnostics.gain_near_zero;
        }
        if (value >= ERB_GRU_GAIN_NEAR_UNITY_MIN_Q) {
            ++g_diagnostics.gain_near_unity;
        }
    }
    g_diagnostics.gain_count += (uint32_t)gain_count;

    for (size_t index = 0U; index < state_count; ++index) {
        update_int8_extrema(
            state1[index], &g_diagnostics.state1_min, &g_diagnostics.state1_max);
        update_int8_extrema(
            state2[index], &g_diagnostics.state2_min, &g_diagnostics.state2_max);
        if ((state1[index] == INT8_MIN) || (state1[index] == INT8_MAX)) {
            ++g_diagnostics.state_sat_count;
        }
        if ((state2[index] == INT8_MIN) || (state2[index] == INT8_MAX)) {
            ++g_diagnostics.state_sat_count;
        }
    }
}

void erb_gru_audio_diagnostics_record_dsp(
    uint64_t spectrum_input_energy_q30,
    uint64_t spectrum_output_energy_q30,
    uint64_t ifft_energy_q30,
    uint64_t wola_energy_q30,
    uint32_t ifft_sample_count,
    uint32_t wola_sample_count,
    uint64_t bin_gain_sum_q15,
    uint32_t bin_gain_count,
    uint32_t bin_gain_min_q15,
    uint32_t bin_gain_max_q15,
    uint32_t wola_peak_q15)
{
    g_diagnostics.spectrum_input_energy_q30 += spectrum_input_energy_q30;
    g_diagnostics.spectrum_output_energy_q30 += spectrum_output_energy_q30;
    g_diagnostics.ifft_energy_q30 += ifft_energy_q30;
    g_diagnostics.wola_energy_q30 += wola_energy_q30;
    g_diagnostics.ifft_sample_count += ifft_sample_count;
    g_diagnostics.wola_sample_count += wola_sample_count;
    g_diagnostics.bin_gain_sum_q15 += bin_gain_sum_q15;
    g_diagnostics.bin_gain_count += bin_gain_count;
    if ((bin_gain_count != 0U) &&
        (bin_gain_min_q15 < g_diagnostics.bin_gain_min_q15)) {
        g_diagnostics.bin_gain_min_q15 = bin_gain_min_q15;
    }
    if ((bin_gain_count != 0U) &&
        (bin_gain_max_q15 > g_diagnostics.bin_gain_max_q15)) {
        g_diagnostics.bin_gain_max_q15 = bin_gain_max_q15;
    }
    if (wola_peak_q15 > g_diagnostics.wola_peak_q15) {
        g_diagnostics.wola_peak_q15 = wola_peak_q15;
    }
}

bool erb_gru_audio_diagnostics_take(erb_gru_audio_diagnostics_t *diagnostics)
{
    if ((diagnostics == NULL) || (g_diagnostics.frames == 0U)) {
        return false;
    }
    *diagnostics = g_diagnostics;
    erb_gru_audio_diagnostics_reset();
    return true;
}
