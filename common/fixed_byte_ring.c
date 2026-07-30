/**
 * @file fixed_byte_ring.c
 * @brief Fixed-capacity single-producer/single-consumer byte queue.
 * @author Bui Minh Huy
 * @date 2026-07-29
 *
 * The foreground producer is the only writer of head. The UART ISR consumer is
 * the only writer of tail.
 */

#include "common/fixed_byte_ring.h"

/*
 * The compiler builtins map to the target's acquire/release instructions on
 * Armclang and GCC without importing a project or vendor dependency. Producer
 * bytes precede the release store of head; consumer acquires head before it
 * reads a byte, then releases tail only after the read completes.
 */
#define FIXED_BYTE_RING_LOAD_ACQUIRE(pointer) \
    __atomic_load_n((pointer), __ATOMIC_ACQUIRE)
#define FIXED_BYTE_RING_LOAD_RELAXED(pointer) \
    __atomic_load_n((pointer), __ATOMIC_RELAXED)
#define FIXED_BYTE_RING_STORE_RELEASE(pointer, value) \
    __atomic_store_n((pointer), (value), __ATOMIC_RELEASE)
#define FIXED_BYTE_RING_STORE_RELAXED(pointer, value) \
    __atomic_store_n((pointer), (value), __ATOMIC_RELAXED)

void fixed_byte_ring_init(fixed_byte_ring_t *ring, uint8_t *storage, size_t capacity) {
    if (ring == NULL) {
        return;
    }

    ring->storage = storage;
    ring->capacity = capacity;
    FIXED_BYTE_RING_STORE_RELAXED(&ring->head, 0U);
    FIXED_BYTE_RING_STORE_RELAXED(&ring->tail, 0U);
}

size_t fixed_byte_ring_count(const fixed_byte_ring_t *ring) {
    if (ring == NULL) {
        return 0U;
    }
    return (size_t)(FIXED_BYTE_RING_LOAD_ACQUIRE(&ring->head) -
                    FIXED_BYTE_RING_LOAD_ACQUIRE(&ring->tail));
}

bool fixed_byte_ring_try_enqueue(
    fixed_byte_ring_t *ring, const uint8_t *message, size_t length) {
    uint32_t head;
    uint32_t tail;
    size_t index;

    if ((ring == NULL) || (ring->storage == NULL) ||
        ((message == NULL) && (length != 0U)) || (length > ring->capacity)) {
        return false;
    }

    head = FIXED_BYTE_RING_LOAD_RELAXED(&ring->head);
    tail = FIXED_BYTE_RING_LOAD_ACQUIRE(&ring->tail);
    if (length > (ring->capacity - (size_t)(head - tail))) {
        return false;
    }

    for (index = 0U; index < length; ++index) {
        ring->storage[(head + (uint32_t)index) % ring->capacity] = message[index];
    }
    FIXED_BYTE_RING_STORE_RELEASE(&ring->head, head + (uint32_t)length);
    return true;
}

bool fixed_byte_ring_try_pop(fixed_byte_ring_t *ring, uint8_t *value) {
    const uint32_t tail = (ring == NULL) ? 0U :
        FIXED_BYTE_RING_LOAD_RELAXED(&ring->tail);

    if ((ring == NULL) || (value == NULL) || (ring->storage == NULL) ||
        (tail == FIXED_BYTE_RING_LOAD_ACQUIRE(&ring->head))) {
        return false;
    }

    *value = ring->storage[tail % ring->capacity];
    FIXED_BYTE_RING_STORE_RELEASE(&ring->tail, tail + 1U);
    return true;
}
