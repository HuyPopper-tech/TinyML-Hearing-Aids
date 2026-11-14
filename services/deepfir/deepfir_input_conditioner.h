/**
 * @file deepfir_input_conditioner.h
 * @brief Fixed-gain PCM24 input compressor and immediate peak guard.
 * @author Bui Minh Huy
 * @date 2026-11-14
 */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    // Sole foreground owner. Current stream dynamics; gain excludes fixed preamp.
    float gain;
    float compressor_gain;
    float limiter_gain;
    float detector_peak;
    // Lifetime since cold reset; modulo-2^64. Compression means >= 0.1 dB reduction.
    uint64_t processed_samples;
    uint64_t compressed_samples;
    uint64_t limited_samples; // Immediate limiter interventions, excluding release tail.
    float input_peak;
    float output_peak;
    float min_gain;
    // Latest complete 100 ms window; raw normalized input and conditioned output.
    float input_rms;
    float output_rms;
    // Partial window belongs to current stream and is discarded by stream reset.
    float input_square_sum;
    float output_square_sum;
    uint32_t rms_window_samples;
} deepfir_input_conditioner_t;

/** Reset envelope, counters and peaks; NULL is ignored. */
void deepfir_input_conditioner_reset(deepfir_input_conditioner_t *state);

/** Reset stream dynamics/partial RMS only, preserving lifetime stats. Audio stopped. */
void deepfir_input_conditioner_reset_stream(deepfir_input_conditioner_t *state);

/**
 * @brief Process one already captured 16-sample mono hop at 16 kHz.
 * @param state Initialized foreground state; must not overlap either buffer.
 * @param input Borrowed sign-extended PCM24, [-8388608, 8388607].
 * @param count Must equal 16; buffers must be separate and non-overlapping.
 * @param output Normalized floats with +21 dB preamp, -12 dBFS/3:1/6 dB knee,
 * compressor 3/120 ms attack/release (10-90%), 20 ms detector decay and
 * independent immediate -1 dBFS limiter with 20 ms release.
 * @return 0 on success, -1 for invalid arguments/state; no mutation on failure.
 * @note No extra hop delay. The guard retains reduction for smooth release;
 * transient limiting may distort and cannot restore already saturated input.
 */
int deepfir_input_conditioner_process(deepfir_input_conditioner_t *state,
                                      const int32_t *input, uint32_t count, float *output);

#ifdef __cplusplus
}
#endif
