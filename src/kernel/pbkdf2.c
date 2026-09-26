#include <stdint.h>

#include "pbkdf2.h"
#include "hmac_sha256.h"
#include "sha256.h"

/*
 * Callers in this project only ever pass small, fixed-size
 * salts (16 bytes), so a bounded stack buffer avoids needing
 * a heap allocator here. If a longer salt is ever passed it is
 * truncated rather than overflowing.
 */
#define PBKDF2_MAX_SALT 64

static void xor_block(
    uint8_t* dest,
    const uint8_t* src,
    uint32_t length
)
{
    for (uint32_t i = 0; i < length; i++)
    {
        dest[i] ^= src[i];
    }
}

void pbkdf2_hmac_sha256(
    const uint8_t* password,
    uint32_t password_length,
    const uint8_t* salt,
    uint32_t salt_length,
    uint32_t iterations,
    uint8_t* out,
    uint32_t out_length
)
{
    if (salt_length > PBKDF2_MAX_SALT)
    {
        salt_length = PBKDF2_MAX_SALT;
    }

    if (iterations == 0)
    {
        iterations = 1;
    }

    uint8_t salt_block[PBKDF2_MAX_SALT + 4];

    for (uint32_t i = 0; i < salt_length; i++)
    {
        salt_block[i] = salt[i];
    }

    uint32_t produced = 0;
    uint32_t block_index = 1;

    while (produced < out_length)
    {
        salt_block[salt_length + 0] =
            (uint8_t)(block_index >> 24);
        salt_block[salt_length + 1] =
            (uint8_t)(block_index >> 16);
        salt_block[salt_length + 2] =
            (uint8_t)(block_index >> 8);
        salt_block[salt_length + 3] =
            (uint8_t)(block_index);

        uint8_t u[SHA256_DIGEST_SIZE];
        uint8_t t[SHA256_DIGEST_SIZE];

        hmac_sha256(
            password,
            password_length,
            salt_block,
            salt_length + 4,
            u
        );

        for (uint32_t i = 0; i < SHA256_DIGEST_SIZE; i++)
        {
            t[i] = u[i];
        }

        for (uint32_t iteration = 1;
             iteration < iterations;
             iteration++)
        {
            uint8_t next[SHA256_DIGEST_SIZE];

            hmac_sha256(
                password,
                password_length,
                u,
                SHA256_DIGEST_SIZE,
                next
            );

            for (uint32_t i = 0; i < SHA256_DIGEST_SIZE; i++)
            {
                u[i] = next[i];
            }

            xor_block(t, u, SHA256_DIGEST_SIZE);
        }

        uint32_t remaining =
            out_length - produced;

        uint32_t chunk =
            (remaining < SHA256_DIGEST_SIZE) ?
            remaining : SHA256_DIGEST_SIZE;

        for (uint32_t i = 0; i < chunk; i++)
        {
            out[produced + i] = t[i];
        }

        produced += chunk;
        block_index++;
    }
}
