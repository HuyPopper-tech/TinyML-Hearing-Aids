/**
 * @file fixed_byte_ring.c
 * @brief Fixed-capacity single-producer/single-consumer byte queue.
 * @author Bui Minh Huy
 * @date 2026-07-29
 *
 * The foreground producer is the only writer of head. The UART ISR consumer is
 * the only writer of tail. Aligned 32-bit loads/stores are atomic on Cortex-M55.
 */

#include "common/fixed_byte_ring.h"

void fixed_byte_ring_init(fixed_byte_ring_t *ring, uint8_t *storage, size_t capacity)
{
    if (ring == NULL) {
        return;
    }

    ring->storage = storage;
    ring->capacity = capacity;
    ring->head = 0U;
    ring->tail = 0U;
}

size_t fixed_byte_ring_count(const fixed_byte_ring_t *ring)
{
    if (ring == NULL) {
        return 0U;
    }
    return (size_t)(ring->head - ring->tail);
}

bool fixed_byte_ring_try_enqueue(
    fixed_byte_ring_t *ring, const uint8_t *message, size_t length)
{
    uint32_t head;
    uint32_t tail;
    size_t index;

    if ((ring == NULL) || (ring->storage == NULL) ||
        ((message == NULL) && (length != 0U)) || (length > ring->capacity)) {
        return false;
    }

    head = ring->head;
    tail = ring->tail;
    if (length > (ring->capacity - (size_t)(head - tail))) {
        return false;
    }

    for (index = 0U; index < length; ++index) {
        ring->storage[(head + (uint32_t)index) % ring->capacity] = message[index];
    }
    ring->head = head + (uint32_t)length;
    return true;
}

bool fixed_byte_ring_try_pop(fixed_byte_ring_t *ring, uint8_t *value)
{
    const uint32_t tail = (ring == NULL) ? 0U : ring->tail;

    if ((ring == NULL) || (value == NULL) || (ring->storage == NULL) ||
        (tail == ring->head)) {
        return false;
    }

    *value = ring->storage[tail % ring->capacity];
    ring->tail = tail + 1U;
    return true;
}
