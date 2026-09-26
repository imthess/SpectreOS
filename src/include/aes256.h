#ifndef SPECTREOS_AES256_H
#define SPECTREOS_AES256_H

#include <stdint.h>

#define AES256_KEY_SIZE   32
#define AES256_BLOCK_SIZE 16
#define AES256_ROUNDS     14

typedef struct
{
    /* 15 round keys of 16 bytes each (Nr + 1). */
    uint8_t round_keys[(AES256_ROUNDS + 1) * AES256_BLOCK_SIZE];

} aes256_ctx_t;

void aes256_init(
    aes256_ctx_t* ctx,
    const uint8_t key[AES256_KEY_SIZE]
);

/*
 * Encrypts exactly one 16-byte block in place-compatible
 * fashion (in and out may be the same buffer).
 */
void aes256_encrypt_block(
    const aes256_ctx_t* ctx,
    const uint8_t in[AES256_BLOCK_SIZE],
    uint8_t out[AES256_BLOCK_SIZE]
);

#endif
