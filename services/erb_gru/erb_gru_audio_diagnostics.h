/**
 * @file erb_gru_audio_diagnostics.h
 * @brief Internal integer-only ERB-GRU acoustic statistic accumulator.
 * @author Bui Minh Huy
 * @date 2026-07-29
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "services/erb_gru/erb_gru_audio_enhancer.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Reset all static acoustic and DSP diagnostic accumulators.
 * @return None.
 *
 * Foreground-only; starts a new accumulation interval.
 */
void erb_gru_audio_diagnostics_reset(void);

/**
 * @brief Accumulate paired input and output PCM statistics.
 * @param input Caller-owned input samples.
 * @param output Caller-owned output samples.
 * @param count Matching sample count in both buffers.
 * @return None.
 *
 * Foreground-only and allocation-free. Neither buffer is retained.
 */
void erb_gru_audio_diagnostics_record_pcm(
    const int16_t *input, const int16_t *output, size_t count);

/**
 * @brief Accumulate quantized feature range and saturation statistics.
 * @param feature Caller-owned INT8 feature vector.
 * @param count Number of feature elements.
 * @return None.
 *
 * Foreground-only and allocation-free; the input is not retained.
 */
void erb_gru_audio_diagnostics_record_feature(
    const int8_t *feature, size_t count);

/**
 * @brief Accumulate successful gain and recurrent-state statistics.
 * @param gain Caller-owned quantized band gains.
 * @param gain_count Number of gain elements.
 * @param state1 Caller-owned first recurrent state.
 * @param state2 Caller-owned second recurrent state.
 * @param state_count Number of elements in each recurrent state.
 * @return None.
 *
 * Foreground-only and allocation-free. Inputs are not retained.
 */
void erb_gru_audio_diagnostics_record_gain_state(
    const int8_t *gain,
    size_t gain_count,
    const int8_t *state1,
    const int8_t *state2,
    size_t state_count);

/**
 * @brief Record precomputed DSP energy and expanded-gain aggregates.
 * @param spectrum_input_energy_q30 Input spectrum energy.
 * @param spectrum_output_energy_q30 Gain-adjusted spectrum energy.
 * @param ifft_energy_q30 Normalized IFFT energy.
 * @param wola_energy_q30 WOLA output energy before PCM conversion.
 * @param ifft_sample_count Number of IFFT samples represented.
 * @param wola_sample_count Number of WOLA samples represented.
 * @param bin_gain_sum_q15 Sum of expanded bin gains.
 * @param bin_gain_count Number of expanded bin gains.
 * @param bin_gain_min_q15 Minimum expanded bin gain.
 * @param bin_gain_max_q15 Maximum expanded bin gain.
 * @param wola_peak_q15 Peak absolute WOLA sample.
 * @return None.
 *
 * Foreground-only; all values are accumulated without allocation or formatting.
 */
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
    uint32_t wola_peak_q15);

/**
 * @brief Copy and reset the current diagnostic aggregate.
 * @param diagnostics Caller-owned snapshot destination.
 * @return True when the snapshot was copied; false when diagnostics is NULL.
 *
 * Foreground-only. A successful call terminates the current interval and starts
 * a new empty one.
 */
bool erb_gru_audio_diagnostics_take(erb_gru_audio_diagnostics_t *diagnostics);

#ifdef __cplusplus
}
#endif
