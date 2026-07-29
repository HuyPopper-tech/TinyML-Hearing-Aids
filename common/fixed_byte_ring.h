/**
 * @file fixed_byte_ring.h
 * @brief Fixed-capacity single-producer/single-consumer byte queue.
 * @author Bui Minh Huy
 * @date 2026-07-29
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t *storage;
    size_t capacity;
    volatile uint32_t head;
    volatile uint32_t tail;
} fixed_byte_ring_t;

void fixed_byte_ring_init(fixed_byte_ring_t *ring, uint8_t *storage, size_t capacity);
bool fixed_byte_ring_try_enqueue(
    fixed_byte_ring_t *ring, const uint8_t *message, size_t length);
bool fixed_byte_ring_try_pop(fixed_byte_ring_t *ring, uint8_t *value);
size_t fixed_byte_ring_count(const fixed_byte_ring_t *ring);

#ifdef __cplusplus
}
#endif
