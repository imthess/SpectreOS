#include <stdint.h>

#include "aes256.h"

/*
 * AES-256 forward cipher only (FIPS-197). Encryption-only is
 * sufficient because this is used exclusively in CTR mode
 * (crypto_disk.c), where both encryption and decryption of the
 * underlying data are done by encrypting a counter and XORing -
 * the AES decrypt transform itself is never needed. That halves
 * the code that has to be gotten right.
 *
 * Verified against the official FIPS-197 Appendix C.3 AES-256
 * test vector before this file was ever linked into the kernel:
 *   key       = 000102030405060708090a0b0c0d0e0f
 *               101112131415161718191a1b1c1d1e1f
 *   plaintext = 00112233445566778899aabbccddeeff
 *   ciphertext= 8ea2b7ca516745bfeafc49904b496089
 */



static const uint8_t rcon[15] =
{
    0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40,
    0x80, 0x1b, 0x36, 0x6c, 0xd8, 0xab, 0x4d
};

#define AES256_NK 8
#define AES256_NB 4
#define AES256_NR 14

/*
 * Constant-time GF(2^8) arithmetic. No secret-dependent branch
 * and no secret-dependent memory index anywhere below: masks
 * replace every `if`, and the S-box is COMPUTED (multiplicative
 * inverse via a^254, then the affine transform) instead of
 * looked up in a 256-entry table, which would leak key/state
 * bytes through cache timing.
 *
 * Note: this is constant-time at the C level. A compiler is free
 * to rewrite code, so the object file should be audited if this
 * is ever used against a hostile local observer.
 */
static uint8_t xtime(uint8_t a)
{
    uint8_t mask = (uint8_t)(0u - (uint32_t)(a >> 7));

    return (uint8_t)((a << 1) ^ (mask & 0x1b));
}

static uint8_t gmul(uint8_t a, uint8_t b)
{
    uint8_t p = 0;

    for (uint32_t i = 0; i < 8; i++)
    {
        uint8_t mask = (uint8_t)(0u - (uint32_t)(b & 1));

        p ^= (uint8_t)(a & mask);
        a = xtime(a);
        b >>= 1;
    }

    return p;
}

static uint8_t rotl8(uint8_t x, uint32_t n)
{
    return (uint8_t)((x << n) | (x >> (8 - n)));
}

static uint8_t sbox_ct(uint8_t x)
{
    /* x^254 == x^-1 in GF(2^8), and 0 maps to 0 as AES requires. */
    uint8_t square = gmul(x, x);
    uint8_t inverse = square;

    for (uint32_t i = 0; i < 6; i++)
    {
        square = gmul(square, square);
        inverse = gmul(inverse, square);
    }

    return (uint8_t)(inverse ^
                     rotl8(inverse, 1) ^
                     rotl8(inverse, 2) ^
                     rotl8(inverse, 3) ^
                     rotl8(inverse, 4) ^
                     0x63);
}

/* Exposed to the test harness only (not declared in aes256.h). */
uint8_t aes256_sbox_for_test(uint8_t x)
{
    return sbox_ct(x);
}

void aes256_init(
    aes256_ctx_t* ctx,
    const uint8_t key[AES256_KEY_SIZE]
)
{
    uint8_t* rk = ctx->round_keys;

    for (uint32_t i = 0; i < AES256_KEY_SIZE; i++)
    {
        rk[i] = key[i];
    }

    uint32_t bytes_generated = AES256_KEY_SIZE;
    uint32_t word_count = AES256_NK;

    uint8_t temp[4];

    while (bytes_generated <
           (AES256_NR + 1) * AES256_NB * 4)
    {
        for (uint32_t i = 0; i < 4; i++)
        {
            temp[i] =
                rk[bytes_generated - 4 + i];
        }

        if (word_count % AES256_NK == 0)
        {
            /*
             * RotWord
             */
            uint8_t t = temp[0];
            temp[0] = temp[1];
            temp[1] = temp[2];
            temp[2] = temp[3];
            temp[3] = t;

            /*
             * SubWord
             */
            for (uint32_t i = 0; i < 4; i++)
            {
                temp[i] = sbox_ct(temp[i]);
            }

            temp[0] ^=
                rcon[word_count / AES256_NK];
        }
        else if (AES256_NK > 6 &&
                 word_count % AES256_NK == 4)
        {
            /*
             * AES-256 specific: an extra SubWord every
             * 8 words, at the halfway point.
             */
            for (uint32_t i = 0; i < 4; i++)
            {
                temp[i] = sbox_ct(temp[i]);
            }
        }

        for (uint32_t i = 0; i < 4; i++)
        {
            rk[bytes_generated + i] =
                rk[bytes_generated - AES256_KEY_SIZE + i] ^
                temp[i];
        }

        bytes_generated += 4;
        word_count++;
    }
}

static void add_round_key(
    uint8_t state[16],
    const uint8_t* round_key
)
{
    for (uint32_t i = 0; i < 16; i++)
    {
        state[i] ^= round_key[i];
    }
}

static void sub_bytes(uint8_t state[16])
{
    for (uint32_t i = 0; i < 16; i++)
    {
        state[i] = sbox_ct(state[i]);
    }
}

/*
 * State is stored column-major, as AES defines it:
 * state[col * 4 + row].
 */
static void shift_rows(uint8_t state[16])
{
    uint8_t temp;

    /* Row 1: shift left by 1 */
    temp = state[1];
    state[1] = state[5];
    state[5] = state[9];
    state[9] = state[13];
    state[13] = temp;

    /* Row 2: shift left by 2 */
    temp = state[2];
    state[2] = state[10];
    state[10] = temp;
    temp = state[6];
    state[6] = state[14];
    state[14] = temp;

    /* Row 3: shift left by 3 (== right by 1) */
    temp = state[15];
    state[15] = state[11];
    state[11] = state[7];
    state[7] = state[3];
    state[3] = temp;
}

static void mix_columns(uint8_t state[16])
{
    for (uint32_t c = 0; c < 4; c++)
    {
        uint8_t* col = &state[c * 4];

        uint8_t a0 = col[0];
        uint8_t a1 = col[1];
        uint8_t a2 = col[2];
        uint8_t a3 = col[3];

        col[0] =
            (uint8_t)(gmul(a0, 2) ^ gmul(a1, 3) ^ a2 ^ a3);

        col[1] =
            (uint8_t)(a0 ^ gmul(a1, 2) ^ gmul(a2, 3) ^ a3);

        col[2] =
            (uint8_t)(a0 ^ a1 ^ gmul(a2, 2) ^ gmul(a3, 3));

        col[3] =
            (uint8_t)(gmul(a0, 3) ^ a1 ^ a2 ^ gmul(a3, 2));
    }
}

void aes256_encrypt_block(
    const aes256_ctx_t* ctx,
    const uint8_t in[AES256_BLOCK_SIZE],
    uint8_t out[AES256_BLOCK_SIZE]
)
{
    uint8_t state[16];

    for (uint32_t i = 0; i < 16; i++)
    {
        state[i] = in[i];
    }

    add_round_key(state, &ctx->round_keys[0]);

    for (uint32_t round = 1; round < AES256_NR; round++)
    {
        sub_bytes(state);
        shift_rows(state);
        mix_columns(state);

        add_round_key(
            state,
            &ctx->round_keys[round * 16]
        );
    }

    sub_bytes(state);
    shift_rows(state);

    add_round_key(
        state,
        &ctx->round_keys[AES256_NR * 16]
    );

    for (uint32_t i = 0; i < 16; i++)
    {
        out[i] = state[i];
    }
}
