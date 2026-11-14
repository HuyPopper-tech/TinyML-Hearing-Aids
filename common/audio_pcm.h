/**
 * @file audio_pcm.h
 * @brief Portable signed PCM24 decoding, normalization and diagnostic export.
 * @author Bui Minh Huy
 * @date 2026-11-14
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_PCM24_MIN (-8388608)
#define AUDIO_PCM24_MAX 8388607

/** Decode a right-aligned FIFO payload, independent of the unused upper byte. */
static inline int32_t audio_pcm24_decode(uint32_t word) {
    return (int32_t)((word & 0x00ffffffU) ^ 0x00800000U) - 8388608;
}

/** Normalize a sign-extended PCM24 code to [-1, 1), retaining its low bits. */
static inline float audio_pcm24_to_float(int32_t sample) {
    return (float)sample / 8388608.0f;
}

/** Diagnostic PCM16 export only; discard low eight bits of a valid PCM24 code. */
static inline int16_t audio_pcm24_to_pcm16(int32_t sample) {
    return (int16_t)((sample - (sample < 0 ? 255 : 0)) / 256);
}

#ifdef __cplusplus
}
#endif
