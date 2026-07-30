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

/** @brief Reset static diagnostic accumulators. @return None. Foreground-only; starts a new accumulation lifecycle. */
void erb_gru_audio_diagnostics_reset(void);
/** @brief Accumulate paired PCM statistics. @param input Input samples. @param output Output samples. @param count Matching sample count. @return None. Foreground-only; buffers remain caller-owned. */
void erb_gru_audio_diagnostics_record_pcm(
    const int16_t *input, const int16_t *output, size_t count);
/** @brief Accumulate quantized features. @param feature Feature vector. @param count Elements. @return None. Foreground-only. */
void erb_gru_audio_diagnostics_record_feature(
    const int8_t *feature, size_t count);
/** @brief Accumulate gains and recurrent-state ranges. @param gain Gain vector. @param gain_count Gain elements. @param state1 First state. @param state2 Second state. @param state_count Elements per state. @return None. Foreground-only. */
void erb_gru_audio_diagnostics_record_gain_state(
    const int8_t *gain,
    size_t gain_count,
    const int8_t *state1,
    const int8_t *state2,
    size_t state_count);
/** @brief Record DSP energy and gain aggregates. @param spectrum_input_energy_q30 Input spectrum energy. @param spectrum_output_energy_q30 Output spectrum energy. @param ifft_energy_q30 IFFT energy. @param wola_energy_q30 WOLA energy. @param ifft_sample_count IFFT samples. @param wola_sample_count WOLA samples. @param bin_gain_sum_q15 Gain sum. @param bin_gain_count Gain count. @param bin_gain_min_q15 Minimum. @param bin_gain_max_q15 Maximum. @param wola_peak_q15 Peak. @return None. Foreground-only. */
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
/** @brief Copy and reset the current diagnostic aggregate. @param diagnostics Destination. @return True on copy; false for NULL. Foreground-only terminal snapshot for the current interval. */
bool erb_gru_audio_diagnostics_take(erb_gru_audio_diagnostics_t *diagnostics);

#ifdef __cplusplus
}
#endif
