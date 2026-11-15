#ifndef SPECTREOS_CRYPTO_DISK_H
#define SPECTREOS_CRYPTO_DISK_H

#include <stdint.h>

/*
 * ============================================================
 * TRANSPARENT DISK ENCRYPTION (AES-256-CTR)
 *
 * Wraps an existing block device (see blockdev.h) with AES-256
 * in counter mode: every sector is encrypted/decrypted with a
 * keystream = AES(key, counter), counter = lba * 32 + block,
 * where "block" is the AES-block index within that 512-byte
 * sector (512 / 16 = 32 AES blocks per sector). Every sector on
 * a given disk therefore uses a distinct part of the keystream -
 * no two sectors, and no two 16-byte blocks anywhere on the
 * disk, ever reuse the same counter value under the same key.
 * Reusing a counter is the one mistake that breaks CTR mode
 * outright, so this is the property that matters most here.
 *
 * This is a from-scratch, self-verified implementation (see
 * aes256.c/sha256.c/pbkdf2.c for the primitives and how they
 * were checked against known-answer tests). It has NOT been
 * through independent security review. Treat it as "real
 * cryptography, unaudited" rather than production-grade.
 *
 * blockdev.h has no per-device context pointer, so a fixed
 * small number of encrypted-disk "slots" are pre-wired here
 * with their own static state, rather than changing that
 * interface (which would mean touching the already-working
 * ata.c/ramdisk.c registrations from the previous pass).
 * ============================================================
 */

#define CRYPTO_DISK_MAX_SLOTS 2

/*
 * Wraps `backing_device_id` (a device id from blockdev.h) with
 * AES-256-CTR using `key` (32 bytes). Registers the encrypted
 * view as a new block device and returns its id, or -1 if no
 * slot is free or an argument is invalid.
 *
 * The key is copied into the AES round-key schedule immediately
 * and the input buffer is then wiped by the caller's own
 * responsibility (see secure_wipe.h) - this function does not
 * wipe the caller's buffer for them, since the caller may still
 * need it (e.g. to also open a second disk with the same key).
 */
int crypto_disk_create(
    const char* name,
    uint32_t backing_device_id,
    const uint8_t key[32]
);

/*
 * Same as crypto_disk_create(), but with a caller-supplied 8-byte
 * nonce instead of a freshly generated random one. REQUIRED for
 * any disk whose contents must still decrypt after a reboot -
 * crypto_disk_create()'s random nonce is never saved anywhere, so
 * the next boot would generate a different one, derive a
 * completely different keystream, and read back only garbage. The
 * caller is responsible for persisting the nonce (fs.c keeps it in
 * the plaintext superblock) and passing the identical value every
 * time the same disk is mounted.
 *
 * HONEST LIMIT OF THIS DESIGN, worth knowing before relying on it:
 * because the nonce and key are fixed for the life of the disk,
 * every rewrite of a given sector reuses the exact same keystream.
 * Someone who obtains two snapshots of the raw disk taken at
 * different times can XOR them to learn the XOR of the old and new
 * plaintext of any sector that changed between them. That is an
 * inherent property of using plain CTR mode for stored data (real
 * disk-encryption schemes like XTS exist partly to avoid it), not
 * a bug in this implementation of CTR - closing it properly means
 * switching modes or adding per-write counters, neither done here.
 */
int crypto_disk_create_with_nonce(
    const char* name,
    uint32_t backing_device_id,
    const uint8_t key[32],
    const uint8_t nonce[8]
);

/*
 * Runs a self-contained round-trip test on a scratch sector of
 * the given crypto disk: writes a known pattern, reads it back
 * through the encrypted device (must match), and also reads the
 * same sector directly from the backing device to confirm the
 * on-disk bytes are NOT the plaintext (must differ). Returns 1
 * if both checks pass, 0 otherwise. Restores whatever was on
 * that sector before returning.
 */
int crypto_disk_self_test(
    uint32_t crypto_device_id,
    uint32_t backing_device_id,
    uint32_t scratch_lba
);

#endif
