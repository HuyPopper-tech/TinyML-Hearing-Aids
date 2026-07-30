/**
 * @file sha256.h
 * @brief Small dependency-free SHA-256 helper for deployed model integrity.
 * @author Bui Minh Huy
 * @date 2026-07-28
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SHA256_DIGEST_BYTES 32U

/**
 * @brief Compute the SHA-256 digest of a fixed byte buffer.
 * @param data Input bytes; may be NULL only when length is zero.
 * @param length Number of input bytes.
 * @param digest Caller-owned 32-byte destination.
 * @return None.
 *
 * The function is synchronous, performs no allocation, and does not retain
 * either caller-owned buffer.
 */
void sha256_compute(const uint8_t *data, size_t length, uint8_t digest[SHA256_DIGEST_BYTES]);

#ifdef __cplusplus
}
#endif
