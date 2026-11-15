#include <stdint.h>

#include "sha256.h"

/*
 * Standard FIPS 180-4 SHA-256. Self-contained: no libc calls,
 * safe to link into a freestanding kernel. Verified in this
 * project against the NIST test vectors for "" (empty string),
 * "abc", and the 56-byte multi-block message before this file
 * was ever compiled into the kernel image.
 */

static const uint32_t k[64] =
{
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

static inline uint32_t rotr(
    uint32_t x,
    uint32_t n
)
{
    return (x >> n) | (x << (32 - n));
}

static void sha256_transform(
    sha256_ctx_t* ctx,
    const uint8_t block[SHA256_BLOCK_SIZE]
)
{
    uint32_t w[64];

    for (uint32_t i = 0; i < 16; i++)
    {
        w[i] =
            ((uint32_t)block[i * 4 + 0] << 24) |
            ((uint32_t)block[i * 4 + 1] << 16) |
            ((uint32_t)block[i * 4 + 2] << 8) |
            ((uint32_t)block[i * 4 + 3]);
    }

    for (uint32_t i = 16; i < 64; i++)
    {
        uint32_t s0 =
            rotr(w[i - 15], 7) ^
            rotr(w[i - 15], 18) ^
            (w[i - 15] >> 3);

        uint32_t s1 =
            rotr(w[i - 2], 17) ^
            rotr(w[i - 2], 19) ^
            (w[i - 2] >> 10);

        w[i] =
            w[i - 16] + s0 +
            w[i - 7] + s1;
    }

    uint32_t a = ctx->state[0];
    uint32_t b = ctx->state[1];
    uint32_t c = ctx->state[2];
    uint32_t d = ctx->state[3];
    uint32_t e = ctx->state[4];
    uint32_t f = ctx->state[5];
    uint32_t g = ctx->state[6];
    uint32_t h = ctx->state[7];

    for (uint32_t i = 0; i < 64; i++)
    {
        uint32_t s1 =
            rotr(e, 6) ^
            rotr(e, 11) ^
            rotr(e, 25);

        uint32_t ch =
            (e & f) ^ ((~e) & g);

        uint32_t temp1 =
            h + s1 + ch + k[i] + w[i];

        uint32_t s0 =
            rotr(a, 2) ^
            rotr(a, 13) ^
            rotr(a, 22);

        uint32_t maj =
            (a & b) ^ (a & c) ^ (b & c);

        uint32_t temp2 =
            s0 + maj;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

void sha256_init(sha256_ctx_t* ctx)
{
    ctx->state[0] = 0x6a09e667;
    ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372;
    ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f;
    ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab;
    ctx->state[7] = 0x5be0cd19;

    ctx->bit_length = 0;
    ctx->buffer_length = 0;
}

void sha256_update(
    sha256_ctx_t* ctx,
    const void* data,
    uint32_t length
)
{
    const uint8_t* input =
        (const uint8_t*)data;

    ctx->bit_length +=
        (uint64_t)length * 8;

    uint32_t offset = 0;

    if (ctx->buffer_length > 0)
    {
        uint32_t needed =
            SHA256_BLOCK_SIZE -
            ctx->buffer_length;

        uint32_t take =
            (length < needed) ?
            length : needed;

        for (uint32_t i = 0; i < take; i++)
        {
            ctx->buffer[ctx->buffer_length + i] =
                input[i];
        }

        ctx->buffer_length += take;
        offset += take;

        if (ctx->buffer_length ==
            SHA256_BLOCK_SIZE)
        {
            sha256_transform(
                ctx,
                ctx->buffer
            );

            ctx->buffer_length = 0;
        }
    }

    while (length - offset >=
           SHA256_BLOCK_SIZE)
    {
        sha256_transform(
            ctx,
            &input[offset]
        );

        offset += SHA256_BLOCK_SIZE;
    }

    while (offset < length)
    {
        ctx->buffer[ctx->buffer_length] =
            input[offset];

        ctx->buffer_length++;
        offset++;
    }
}

void sha256_final(
    sha256_ctx_t* ctx,
    uint8_t out[SHA256_DIGEST_SIZE]
)
{
    uint64_t bit_length =
        ctx->bit_length;

    uint8_t pad = 0x80;

    sha256_update(ctx, &pad, 1);

    uint8_t zero = 0x00;

    while (ctx->buffer_length != 56)
    {
        sha256_update(ctx, &zero, 1);
    }

    uint8_t length_bytes[8];

    for (uint32_t i = 0; i < 8; i++)
    {
        length_bytes[i] =
            (uint8_t)(bit_length >>
                (56 - i * 8));
    }

    /*
     * Append directly - bypass sha256_update's own bit_length
     * accounting since we are appending the length field
     * itself, not message data.
     */
    for (uint32_t i = 0; i < 8; i++)
    {
        ctx->buffer[ctx->buffer_length] =
            length_bytes[i];

        ctx->buffer_length++;
    }

    sha256_transform(
        ctx,
        ctx->buffer
    );

    for (uint32_t i = 0; i < 8; i++)
    {
        out[i * 4 + 0] =
            (uint8_t)(ctx->state[i] >> 24);

        out[i * 4 + 1] =
            (uint8_t)(ctx->state[i] >> 16);

        out[i * 4 + 2] =
            (uint8_t)(ctx->state[i] >> 8);

        out[i * 4 + 3] =
            (uint8_t)(ctx->state[i]);
    }
}

void sha256(
    const void* data,
    uint32_t length,
    uint8_t out[SHA256_DIGEST_SIZE]
)
{
    sha256_ctx_t ctx;

    sha256_init(&ctx);
    sha256_update(&ctx, data, length);
    sha256_final(&ctx, out);
}
