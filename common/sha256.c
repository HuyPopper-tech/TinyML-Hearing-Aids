/**
 * @file sha256.c
 * @brief Fixed-buffer SHA-256 implementation used before model interpreter creation.
 * @author Bui Minh Huy
 * @date 2026-07-28
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "common/sha256.h"

typedef struct
{
    uint32_t state[8];
    uint64_t bit_length;
    uint8_t block[64];
    size_t used;
} sha256_context_t;

static const uint32_t SHA256_ROUND_CONSTANTS[64] = {
    0x428a2f98UL, 0x71374491UL, 0xb5c0fbcfUL, 0xe9b5dba5UL, 0x3956c25bUL, 0x59f111f1UL, 0x923f82a4UL, 0xab1c5ed5UL,
    0xd807aa98UL, 0x12835b01UL, 0x243185beUL, 0x550c7dc3UL, 0x72be5d74UL, 0x80deb1feUL, 0x9bdc06a7UL, 0xc19bf174UL,
    0xe49b69c1UL, 0xefbe4786UL, 0x0fc19dc6UL, 0x240ca1ccUL, 0x2de92c6fUL, 0x4a7484aaUL, 0x5cb0a9dcUL, 0x76f988daUL,
    0x983e5152UL, 0xa831c66dUL, 0xb00327c8UL, 0xbf597fc7UL, 0xc6e00bf3UL, 0xd5a79147UL, 0x06ca6351UL, 0x14292967UL,
    0x27b70a85UL, 0x2e1b2138UL, 0x4d2c6dfcUL, 0x53380d13UL, 0x650a7354UL, 0x766a0abbUL, 0x81c2c92eUL, 0x92722c85UL,
    0xa2bfe8a1UL, 0xa81a664bUL, 0xc24b8b70UL, 0xc76c51a3UL, 0xd192e819UL, 0xd6990624UL, 0xf40e3585UL, 0x106aa070UL,
    0x19a4c116UL, 0x1e376c08UL, 0x2748774cUL, 0x34b0bcb5UL, 0x391c0cb3UL, 0x4ed8aa4aUL, 0x5b9cca4fUL, 0x682e6ff3UL,
    0x748f82eeUL, 0x78a5636fUL, 0x84c87814UL, 0x8cc70208UL, 0x90befffaUL, 0xa4506cebUL, 0xbef9a3f7UL, 0xc67178f2UL};

static uint32_t rotate_right(uint32_t value, uint32_t bits)
{
    return (value >> bits) | (value << (32U - bits));
}

static void sha256_transform(sha256_context_t *context, const uint8_t block[64])
{
    uint32_t words[64];
    uint32_t working[8];
    uint32_t temporary_1;
    uint32_t temporary_2;
    uint32_t sigma_0;
    uint32_t sigma_1;
    uint32_t index;

    for (index = 0U; index < 16U; ++index)
    {
        words[index] = ((uint32_t)block[index * 4U] << 24U) | ((uint32_t)block[index * 4U + 1U] << 16U) |
                       ((uint32_t)block[index * 4U + 2U] << 8U) | (uint32_t)block[index * 4U + 3U];
    }
    for (index = 16U; index < 64U; ++index)
    {
        sigma_0 = rotate_right(words[index - 15U], 7U) ^ rotate_right(words[index - 15U], 18U) ^ (words[index - 15U] >> 3U);
        sigma_1 = rotate_right(words[index - 2U], 17U) ^ rotate_right(words[index - 2U], 19U) ^ (words[index - 2U] >> 10U);
        words[index] = words[index - 16U] + sigma_0 + words[index - 7U] + sigma_1;
    }
    (void)memcpy(working, context->state, sizeof(working));
    for (index = 0U; index < 64U; ++index)
    {
        temporary_1 = working[7] + (rotate_right(working[4], 6U) ^ rotate_right(working[4], 11U) ^ rotate_right(working[4], 25U)) +
                      ((working[4] & working[5]) ^ ((~working[4]) & working[6])) + SHA256_ROUND_CONSTANTS[index] + words[index];
        temporary_2 = (rotate_right(working[0], 2U) ^ rotate_right(working[0], 13U) ^ rotate_right(working[0], 22U)) +
                      ((working[0] & working[1]) ^ (working[0] & working[2]) ^ (working[1] & working[2]));
        working[7] = working[6];
        working[6] = working[5];
        working[5] = working[4];
        working[4] = working[3] + temporary_1;
        working[3] = working[2];
        working[2] = working[1];
        working[1] = working[0];
        working[0] = temporary_1 + temporary_2;
    }
    for (index = 0U; index < 8U; ++index)
    {
        context->state[index] += working[index];
    }
}

void sha256_compute(const uint8_t *data, size_t length, uint8_t digest[SHA256_DIGEST_BYTES])
{
    sha256_context_t context = {
        {0x6a09e667UL, 0xbb67ae85UL, 0x3c6ef372UL, 0xa54ff53aUL,
         0x510e527fUL, 0x9b05688cUL, 0x1f83d9abUL, 0x5be0cd19UL},
        0U,
        {0U},
        0U};
    uint32_t index;

    while (length > 0U)
    {
        const size_t copied = ((64U - context.used) < length) ? (64U - context.used) : length;
        (void)memcpy(&context.block[context.used], data, copied);
        context.used += copied;
        data += copied;
        length -= copied;
        if (context.used == sizeof(context.block))
        {
            sha256_transform(&context, context.block);
            context.bit_length += 512U;
            context.used = 0U;
        }
    }

    context.bit_length += (uint64_t)context.used * 8U;
    context.block[context.used++] = 0x80U;
    if (context.used > 56U)
    {
        (void)memset(&context.block[context.used], 0, sizeof(context.block) - context.used);
        sha256_transform(&context, context.block);
        context.used = 0U;
    }
    (void)memset(&context.block[context.used], 0, 56U - context.used);
    for (index = 0U; index < 8U; ++index)
    {
        context.block[63U - index] = (uint8_t)(context.bit_length >> (index * 8U));
    }
    sha256_transform(&context, context.block);
    for (index = 0U; index < 8U; ++index)
    {
        digest[index * 4U] = (uint8_t)(context.state[index] >> 24U);
        digest[index * 4U + 1U] = (uint8_t)(context.state[index] >> 16U);
        digest[index * 4U + 2U] = (uint8_t)(context.state[index] >> 8U);
        digest[index * 4U + 3U] = (uint8_t)context.state[index];
    }
}
