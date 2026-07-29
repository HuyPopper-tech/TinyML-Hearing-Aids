/**
 * @file audio_telemetry.c
 * @brief Rate-limited foreground audio telemetry.
 * @author Bui Minh Huy
 * @date 2026-07-29
 */

#include <stdio.h>

#include "drivers/debug_uart_tx/debug_uart_tx.h"
#include "services/audio_telemetry.h"

#define AUDIO_TELEMETRY_INTERVAL_FRAMES 500U
#define AUDIO_TELEMETRY_MESSAGE_BYTES 320U
#define AUDIO_TELEMETRY_HISTOGRAM_BUCKET_US 100U
#define AUDIO_TELEMETRY_HISTOGRAM_BUCKETS 102U

static char g_message[AUDIO_TELEMETRY_MESSAGE_BYTES];
static uint32_t g_last_snapshot_frame;
static uint16_t g_processing_histogram[AUDIO_TELEMETRY_HISTOGRAM_BUCKETS];
static uint16_t g_processing_observations;

void audio_telemetry_init(void)
{
    g_last_snapshot_frame = 0U;
    g_processing_observations = 0U;
    for (uint32_t index = 0U; index < AUDIO_TELEMETRY_HISTOGRAM_BUCKETS; ++index) {
        g_processing_histogram[index] = 0U;
    }
    debug_uart_tx_init();
}

void audio_telemetry_record_processing_us(uint32_t elapsed_us)
{
    uint32_t bucket = elapsed_us / AUDIO_TELEMETRY_HISTOGRAM_BUCKET_US;

    if (bucket >= AUDIO_TELEMETRY_HISTOGRAM_BUCKETS) {
        bucket = AUDIO_TELEMETRY_HISTOGRAM_BUCKETS - 1U;
    }
    ++g_processing_histogram[bucket];
    ++g_processing_observations;
}

void audio_telemetry_poll(
    const audio_telemetry_snapshot_t *snapshot, bool dmic_block_pending)
{
    int length;
    uint32_t cumulative = 0U;
    uint32_t p99_us = 0U;
    const uint32_t p99_rank =
        ((uint32_t)g_processing_observations * 99U + 99U) / 100U;

    if ((snapshot == NULL) || dmic_block_pending || (snapshot->frame_count == 0U) ||
        ((snapshot->frame_count % AUDIO_TELEMETRY_INTERVAL_FRAMES) != 0U) ||
        (snapshot->frame_count == g_last_snapshot_frame)) {
        return;
    }

    g_last_snapshot_frame = snapshot->frame_count;
    for (uint32_t index = 0U; index < AUDIO_TELEMETRY_HISTOGRAM_BUCKETS; ++index) {
        cumulative += g_processing_histogram[index];
        if ((p99_us == 0U) && (cumulative >= p99_rank)) {
            p99_us = (index + 1U) * AUDIO_TELEMETRY_HISTOGRAM_BUCKET_US;
        }
    }
    length = snprintf(
        g_message, sizeof(g_message),
        "[PERF] f=%lu enhance_us=%lu/%lu p99_us=%lu npu_us=%lu/%lu fallback=%lu deadline=%lu "
        "dmic_ovr=%lu dmic_dma=%lu dac_busy=%lu underrun=%lu dac_dma=%lu "
        "npu_error=%lu arena=%lu telemetry_drop=%lu\r\n",
        (unsigned long)snapshot->frame_count,
        (unsigned long)snapshot->enhance_last_us,
        (unsigned long)snapshot->enhance_max_us,
        (unsigned long)p99_us,
        (unsigned long)snapshot->invoke_last_us,
        (unsigned long)snapshot->invoke_max_us,
        (unsigned long)snapshot->unity_fallback,
        (unsigned long)snapshot->deadline_miss,
        (unsigned long)snapshot->dmic_overrun,
        (unsigned long)snapshot->dmic_dma_error,
        (unsigned long)snapshot->dac_busy,
        (unsigned long)snapshot->dac_underrun,
        (unsigned long)snapshot->dac_dma_error,
        (unsigned long)snapshot->npu_error,
        (unsigned long)snapshot->arena_used_bytes,
        (unsigned long)debug_uart_tx_drop_count());
    if ((length <= 0) || ((size_t)length >= sizeof(g_message))) {
        return;
    }
    (void)debug_uart_tx_try_enqueue(g_message, (size_t)length);
    g_processing_observations = 0U;
    for (uint32_t index = 0U; index < AUDIO_TELEMETRY_HISTOGRAM_BUCKETS; ++index) {
        g_processing_histogram[index] = 0U;
    }
}

void audio_telemetry_fatal(const char *stage, int status)
{
    const int length = snprintf(
        g_message, sizeof(g_message), "[FAIL] %s (status=%d)\r\n",
        (stage == NULL) ? "unknown" : stage, status);

    if ((length > 0) && ((size_t)length < sizeof(g_message))) {
        (void)debug_uart_tx_try_enqueue(g_message, (size_t)length);
    }
}
