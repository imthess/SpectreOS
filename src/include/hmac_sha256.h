#ifndef SPECTREOS_HMAC_SHA256_H
#define SPECTREOS_HMAC_SHA256_H

#include <stdint.h>
#include "sha256.h"

/*
 * RFC 2104 HMAC, instantiated with SHA-256.
 */
void hmac_sha256(
    const uint8_t* key,
    uint32_t key_length,
    const uint8_t* message,
    uint32_t message_length,
    uint8_t out[SHA256_DIGEST_SIZE]
);

#endif
