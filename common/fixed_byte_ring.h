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
    uint32_t head;
    uint32_t tail;
} fixed_byte_ring_t;

/**
 * @brief Initialize a caller-owned fixed byte ring.
 * @param ring Ring to initialize; ignored when NULL.
 * @param storage Caller-owned storage retained until the ring is no longer used.
 * @param capacity Number of bytes in storage; zero capacity rejects all messages.
 *
 * Call before the foreground producer or ISR consumer accesses the ring. This
 * resets any queued data and has no allocation or ownership transfer.
 */
void fixed_byte_ring_init(fixed_byte_ring_t *ring, uint8_t *storage, size_t capacity);
/**
 * @brief Atomically enqueue an entire message from foreground context.
 * @param ring Initialized SPSC ring whose producer is the caller.
 * @param message Bytes to copy; NULL is valid only for a zero-length message.
 * @param length Number of bytes to enqueue.
 * @return True when every byte is copied and published; false when invalid or full.
 *
 * Non-blocking and non-reentrant for the producer. The caller retains message
 * ownership; the bytes are copied before the release publication of head.
 */
bool fixed_byte_ring_try_enqueue(
    fixed_byte_ring_t *ring, const uint8_t *message, size_t length);
/**
 * @brief Pop one byte from the ISR consumer side of the ring.
 * @param ring Initialized SPSC ring whose consumer is the caller.
 * @param value Destination for the popped byte.
 * @return True when a byte was copied; false when invalid or empty.
 *
 * Non-blocking and non-reentrant for the consumer. The release publication of
 * tail follows the read, so foreground may reuse the slot only afterward.
 */
bool fixed_byte_ring_try_pop(fixed_byte_ring_t *ring, uint8_t *value);
/**
 * @brief Return a snapshot of queued bytes.
 * @param ring Initialized SPSC ring, or NULL.
 * @return A best-effort queued-byte count, or zero for NULL.
 *
 * This diagnostic snapshot is safe from either execution context but can change
 * immediately after it returns; it is not a reservation or synchronization API.
 */
size_t fixed_byte_ring_count(const fixed_byte_ring_t *ring);

#ifdef __cplusplus
}
#endif
