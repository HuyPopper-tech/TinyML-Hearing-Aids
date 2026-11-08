/**
 * @file audio_startup_trace.h
 * @brief Portable raw startup observations; timestamps are board counter units.
 * @author Bui Minh Huy
 * @date 2026-11-08
 */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_STARTUP_TRACE_CAPACITY 12u
typedef enum {
    AUDIO_STARTUP_TX_ARM = 1,
    AUDIO_STARTUP_TX_ENABLED,
    AUDIO_STARTUP_DMIC_ENABLED,
    AUDIO_STARTUP_TX_TD,
    AUDIO_STARTUP_RX_TD,
    AUDIO_STARTUP_CAPTURE_TAKE,
    AUDIO_STARTUP_STOP,
} audio_startup_phase_t;

typedef struct {
    uint32_t phase;
    uint32_t cycle;
    uint32_t i2s_status0;
    uint32_t i2s_status1;
    uint32_t i2s_ctl1;
    uint32_t tx_dma_ctl;
    uint32_t rx_dma_ctl;
    uint32_t tx_td_status;
    uint32_t rx_td_status;
    uint32_t rx_sequence;
    uint32_t primask;
    uint32_t basepri;
    uint32_t ipsr;
    uint32_t irq_pending;
    uint32_t tx_fifo_words;
    uint32_t tx_threshold_words;
} audio_startup_event_t;

/** Board owns storage. Foreground/IRQ writers serialize with a bounded IRQ mask;
 * readers borrow only after stop. No DMA accesses this cacheable CPU-only buffer.
 * Counters count recorded events, not total stream transfers. */
typedef struct {
    uint32_t count;
    uint32_t tx_events;
    uint32_t rx_events;
    uint32_t take_events;
    uint32_t active;
    audio_startup_event_t events[AUDIO_STARTUP_TRACE_CAPACITY];
} audio_startup_trace_t;

#ifdef __cplusplus
}
#endif
