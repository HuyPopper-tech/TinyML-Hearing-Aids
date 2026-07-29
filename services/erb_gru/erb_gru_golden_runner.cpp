/**
 * @file erb_gru_golden_runner.cpp
 * @brief Test-only finite-corpus board parity runner.
 * @author Bui Minh Huy
 * @date 2026-07-29
 */

#include <cstdint>
#include <cstring>

#include "services/erb_gru/erb_gru_audio_enhancer.h"
#include "services/erb_gru/erb_gru_golden_runner.h"

#if M55M1_ERB_GRU_GOLDEN_SILENCE
#include "models/erb_gru/generated/golden/golden_silence.h"
#define ERB_GRU_GOLDEN_INPUT erb_gru_silence_pcm_input_int16
#define ERB_GRU_GOLDEN_OUTPUT erb_gru_silence_pcm_output_board_int16
#elif M55M1_ERB_GRU_GOLDEN_SINE_1KHZ
#include "models/erb_gru/generated/golden/golden_sine_1khz.h"
#define ERB_GRU_GOLDEN_INPUT erb_gru_sine_1khz_pcm_input_int16
#define ERB_GRU_GOLDEN_OUTPUT erb_gru_sine_1khz_pcm_output_board_int16
#elif M55M1_ERB_GRU_GOLDEN_CLEAN_SPEECH
#include "models/erb_gru/generated/golden/golden_clean_speech.h"
#define ERB_GRU_GOLDEN_INPUT erb_gru_clean_speech_pcm_input_int16
#define ERB_GRU_GOLDEN_OUTPUT erb_gru_clean_speech_pcm_output_board_int16
#elif M55M1_ERB_GRU_GOLDEN_SPEECH_NOISE
#include "models/erb_gru/generated/golden/golden_speech_noise.h"
#define ERB_GRU_GOLDEN_INPUT erb_gru_speech_noise_pcm_input_int16
#define ERB_GRU_GOLDEN_OUTPUT erb_gru_speech_noise_pcm_output_board_int16
#elif M55M1_ERB_GRU_GOLDEN_NEAR_FULL_SCALE
#include "models/erb_gru/generated/golden/golden_near_full_scale.h"
#define ERB_GRU_GOLDEN_INPUT erb_gru_near_full_scale_pcm_input_int16
#define ERB_GRU_GOLDEN_OUTPUT erb_gru_near_full_scale_pcm_output_board_int16
#else
#error "Exactly one M55M1_ERB_GRU_GOLDEN_* scenario must be selected"
#endif

namespace {

constexpr uint32_t kCorpusSamples = 4000U;
constexpr uint32_t kHopSamples = 160U;
constexpr uint32_t kStreamingFrames = 24U;
alignas(16) int16_t g_actual[kCorpusSamples];
alignas(16) int16_t g_block[kHopSamples];

} // namespace

extern "C" bool erb_gru_golden_run(erb_gru_golden_result_t *result)
{
    erb_gru_golden_result_t local = {};
    uint32_t output_offset = 0U;

    if (result == nullptr) {
        return false;
    }
    erb_gru_audio_enhancer_reset();
    for (uint32_t input_offset = 0U; input_offset < kCorpusSamples;
         input_offset += kHopSamples) {
        const erb_gru_audio_result_t process_result =
            erb_gru_audio_enhancer_process(
                ERB_GRU_GOLDEN_INPUT + input_offset, g_block);

        if (input_offset == 0U) {
            if (process_result != ERB_GRU_AUDIO_RESULT_PREROLL_SILENCE) {
                return false;
            }
            continue;
        }
        if (process_result != ERB_GRU_AUDIO_RESULT_ENHANCED) {
            return false;
        }
        std::memcpy(g_actual + output_offset, g_block, sizeof(g_block));
        output_offset += kHopSamples;
        ++local.processed_frames;
    }
    if ((local.processed_frames != kStreamingFrames) ||
        !erb_gru_audio_enhancer_flush(g_actual + output_offset)) {
        return false;
    }

    for (uint32_t index = 0U; index < kCorpusSamples; ++index) {
        const int32_t expected = ERB_GRU_GOLDEN_OUTPUT[index];
        const int32_t error = static_cast<int32_t>(g_actual[index]) - expected;
        const uint32_t magnitude = (error < 0) ? (uint32_t)-error : (uint32_t)error;
        const uint64_t expected_square = (uint64_t)(expected * expected);
        const uint32_t error_square = magnitude * magnitude;

        local.signal_energy += expected_square;
        local.noise_energy += error_square;
        local.squared_error += error_square;
        if (magnitude > local.max_absolute_error) {
            local.max_absolute_error = (uint16_t)magnitude;
        }
    }
    *result = local;
    return (local.max_absolute_error <= 16U) &&
           (local.squared_error <= (4U * kCorpusSamples)) &&
           ((local.signal_energy == 0U) ? (local.noise_energy == 0U) :
            (local.noise_energy <= (local.signal_energy / 1000000U)));
}
