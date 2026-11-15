#include <stdint.h>

#include "hmac_sha256.h"
#include "sha256.h"

void hmac_sha256(
    const uint8_t* key,
    uint32_t key_length,
    const uint8_t* message,
    uint32_t message_length,
    uint8_t out[SHA256_DIGEST_SIZE]
)
{
    uint8_t key_block[SHA256_BLOCK_SIZE];
    uint8_t shortened_key[SHA256_DIGEST_SIZE];

    for (uint32_t i = 0; i < SHA256_BLOCK_SIZE; i++)
    {
        key_block[i] = 0;
    }

    if (key_length > SHA256_BLOCK_SIZE)
    {
        sha256(key, key_length, shortened_key);

        for (uint32_t i = 0; i < SHA256_DIGEST_SIZE; i++)
        {
            key_block[i] = shortened_key[i];
        }
    }
    else
    {
        for (uint32_t i = 0; i < key_length; i++)
        {
            key_block[i] = key[i];
        }
    }

    uint8_t ipad[SHA256_BLOCK_SIZE];
    uint8_t opad[SHA256_BLOCK_SIZE];

    for (uint32_t i = 0; i < SHA256_BLOCK_SIZE; i++)
    {
        ipad[i] = key_block[i] ^ 0x36;
        opad[i] = key_block[i] ^ 0x5c;
    }

    /*
     * inner = SHA256(ipad || message)
     */
    sha256_ctx_t ctx;

    sha256_init(&ctx);
    sha256_update(&ctx, ipad, SHA256_BLOCK_SIZE);
    sha256_update(&ctx, message, message_length);

    uint8_t inner[SHA256_DIGEST_SIZE];

    sha256_final(&ctx, inner);

    /*
     * result = SHA256(opad || inner)
     */
    sha256_init(&ctx);
    sha256_update(&ctx, opad, SHA256_BLOCK_SIZE);
    sha256_update(&ctx, inner, SHA256_DIGEST_SIZE);
    sha256_final(&ctx, out);

    /*
     * Wipe transient key material. volatile prevents the
     * compiler from optimizing this away as a dead store.
     */
    volatile uint8_t* wipe;

    wipe = key_block;
    for (uint32_t i = 0; i < SHA256_BLOCK_SIZE; i++)
    {
        wipe[i] = 0;
    }

    wipe = shortened_key;
    for (uint32_t i = 0; i < SHA256_DIGEST_SIZE; i++)
    {
        wipe[i] = 0;
    }
}
