/**
 * @file deepfir_inference_service.h
 * @brief Foreground DeepFIR inference service for the production audio path.
 * @author Bui Minh Huy
 * @date 2026-09-23
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t feature_us;
    uint32_t pre_fft_us;
    uint32_t fft_us;
    uint32_t post_fft_us;
    uint32_t invoke_us;
    uint32_t npu_us;
    uint32_t invoke_overhead_us;
    uint32_t dsp_us;
    uint32_t total_us;
#if M55M1_RUNTIME_TELEMETRY || M55M1_AUDIO_RELEASE_PROBE
    uint32_t bridge_us;
    uint32_t carry_us;
    uint32_t taps_us;
    uint32_t fir_us;
    uint32_t pcm_us;
    uint32_t other_us;
#endif
} deepfir_inference_timing_t;

typedef struct {
    uint32_t model_bytes, arena_capacity_bytes, arena_used_bytes;
    uint32_t adapter_bytes, frontend_bytes, fft_bytes;
    uintptr_t model_address, arena_address;
    bool initialized;
} deepfir_inference_memory_t;

/** Foreground/debugger snapshot; actual arena use is valid only after init. */
void deepfir_inference_get_memory(deepfir_inference_memory_t *memory);

/**
 * @brief Initialize the single TFLM interpreter and the model state.
 * @return True when the model, ABI, runtime, and SRAM arena are ready.
 *
 * Foreground-only before audio starts. No allocation occurs after return.
 */
bool deepfir_inference_init(void);

/**
 * @brief Reset input dynamics, recurrent, feature and FIR state at a stream boundary.
 * @note Preserves conditioner lifetime counters, peaks, minimum gain and latest
 * complete RMS window; discards its partial window. Call with audio stopped.
 * @return None.
 */
void deepfir_inference_reset(void);

/**
 * @brief Process one 16-sample PCM16 hop into the model output stream.
 * @param input Signed mono PCM16 samples.
 * @param output Signed mono PCM16 output samples.
 * @param sample_count Must equal 16.
 * @return True when the hop completed successfully.
 */
bool deepfir_inference_process(const int16_t *input, int16_t *output, uint32_t sample_count);

/** Foreground production hop: 16 sign-extended PCM24 input codes, PCM16 output.
 * Fixed input preamp/compressor/peak guard feeds the same float hop to frontend/FIR.
 * Input/output must be separate caller-owned storage. Shares the single stream
 * state with the PCM16 replay API; reset before changing streams/formats. */
bool deepfir_inference_process_pcm24(const int32_t *input, int16_t *output, uint32_t sample_count);

/**
 * @brief Read the last foreground stage timing sample.
 * @param timing Destination; ignored when NULL.
 * @return None.
 */
void deepfir_inference_get_last_timing(deepfir_inference_timing_t *timing);
/**
 * @brief Report retained Debug hop marks after capture/playback have stopped.
 * @pre Foreground only, DMA quiesced; never call during processing or from an ISR.
 * @note May drain Debug UART while stopped; raw marks remain visible until the next hop.
 */
void deepfir_inference_report_trace(void);

/** @brief Read active feature/FIR MVE paths and optional Debug self-test time. */
void deepfir_inference_get_dsp_status(bool *feature_mve_ready, bool *fir_mve_ready,
                                      uint32_t *self_test_us);

#if M55M1_DEEPFIR_COMPILED_REPLAY
/** Diagnostic foreground loop; audio must never start in this build mode. */
void deepfir_inference_replay_poll(void);
#endif

#ifdef __cplusplus
}
#endif
