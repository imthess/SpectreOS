#ifndef SPECTREOS_SHA256_H
#define SPECTREOS_SHA256_H

#include <stdint.h>

#define SHA256_BLOCK_SIZE  64
#define SHA256_DIGEST_SIZE 32

typedef struct
{
    uint32_t state[8];
    uint64_t bit_length;

    uint8_t buffer[SHA256_BLOCK_SIZE];
    uint32_t buffer_length;

} sha256_ctx_t;

void sha256_init(sha256_ctx_t* ctx);

void sha256_update(
    sha256_ctx_t* ctx,
    const void* data,
    uint32_t length
);

/*
 * Writes the 32-byte digest to `out` and leaves the context
 * unusable (call sha256_init again before reusing it).
 */
void sha256_final(
    sha256_ctx_t* ctx,
    uint8_t out[SHA256_DIGEST_SIZE]
);

/*
 * Convenience one-shot wrapper.
 */
void sha256(
    const void* data,
    uint32_t length,
    uint8_t out[SHA256_DIGEST_SIZE]
);

#endif
