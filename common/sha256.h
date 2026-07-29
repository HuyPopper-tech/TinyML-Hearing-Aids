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

/** Compute the SHA-256 digest of a fixed byte buffer. */
void sha256_compute(const uint8_t *data, size_t length, uint8_t digest[SHA256_DIGEST_BYTES]);

#ifdef __cplusplus
}
#endif
