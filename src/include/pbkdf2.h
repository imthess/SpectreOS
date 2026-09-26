#ifndef SPECTREOS_PBKDF2_H
#define SPECTREOS_PBKDF2_H

#include <stdint.h>

/*
 * PBKDF2 (RFC 8018) instantiated with HMAC-SHA256. Used to turn
 * a login password into a fixed-length key: for the account
 * database it becomes the stored verifier, and for the disk
 * encryption layer it becomes the AES key. A password by itself
 * is never used directly as a key or stored directly on disk.
 */
void pbkdf2_hmac_sha256(
    const uint8_t* password,
    uint32_t password_length,
    const uint8_t* salt,
    uint32_t salt_length,
    uint32_t iterations,
    uint8_t* out,
    uint32_t out_length
);

#endif
