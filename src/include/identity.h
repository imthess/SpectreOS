#ifndef SPECTREOS_IDENTITY_H
#define SPECTREOS_IDENTITY_H

#include <stdint.h>
#include "sha256.h"

/*
 * ============================================================
 * CRYPTOGRAPHIC IDENTITY
 *
 * From the original concept: "Every SpectreOS user would have
 * their own unique cryptographically generated identity,
 * inspired by the concept of cryptocurrency addresses rather
 * than conventional operating-system user IDs."
 *
 * HONEST SCOPE NOTE - read before treating this as equivalent
 * to a Bitcoin-style address or an SSH/PGP keypair:
 *
 * This is a hash-based identity, not an elliptic-curve keypair.
 * At generation time a 32-byte random seed is drawn from
 * rng.h, and the public identity fingerprint is:
 *
 *     identity_id = SHA256(seed || "SPECTREOS-ID" || username)
 *
 * This has real precedent - Bitcoin addresses are themselves a
 * hash of a public key, and Tor v2 .onion addresses were a hash
 * of an RSA key - hash-based identity is a legitimate,
 * lightweight construction. It gives you a fingerprint that is:
 *   - unique and effectively unforgeable to *derive without the
 *     seed* (reversing SHA-256 is infeasible), and
 *   - deterministic proof-of-possession via identity_prove()
 *     (HMAC-SHA256 keyed on the seed).
 *
 * What it deliberately is NOT (yet): a true asymmetric keypair.
 * identity_prove() is a symmetric proof - anyone who can check
 * it must already share the seed (or be given the proof by
 * someone who does), unlike an EC/RSA signature which anyone can
 * verify from a *public* key alone while the private key stays
 * secret. That is the missing piece for genuine peer-to-peer
 * identity verification (needed once networking/communication
 * exists). Implementing elliptic-curve signing correctly from
 * scratch, with no reference implementation to test against in
 * this environment, was judged too high-risk to ship silently -
 * better to be explicit about the gap than to hand you unaudited
 * EC arithmetic labeled as done.
 *
 * Only one identity is currently supported per machine (the
 * account created during first-run setup - see auth.h). The
 * original concept calls for one identity per user; extending
 * this to multiple local identities is straightforward future
 * work (one identity file per username instead of a single
 * fixed file) but is not implemented in this pass.
 * ============================================================
 */

#define IDENTITY_SEED_SIZE     32
#define IDENTITY_ID_SIZE       SHA256_DIGEST_SIZE
#define IDENTITY_USERNAME_MAX  32
#define IDENTITY_FILE_NAME     "identity.dat"

/*
 * Returns 1 if an identity has already been generated and
 * persisted on this disk, 0 otherwise.
 */
int identity_exists(void);

/*
 * Loads the persisted identity into memory. Returns 1 on
 * success, 0 if none exists or the file is corrupt.
 */
int identity_load(void);

/*
 * Generates a brand new identity for `username`, persists it to
 * disk, and loads it as the active in-memory identity. Returns
 * 1 on success, 0 on failure (e.g. disk full, username too
 * long). Overwrites any previous identity - only call this
 * during first-run setup.
 */
int identity_generate(const char* username);

/*
 * 1 if an identity is currently loaded in memory (via
 * identity_load() or identity_generate()).
 */
int identity_is_loaded(void);

const char* identity_get_username(void);

/*
 * Writes the identity fingerprint as lowercase hex into `out`,
 * which must be at least (IDENTITY_ID_SIZE * 2 + 1) bytes.
 */
void identity_get_fingerprint_hex(
    char* out,
    uint32_t out_size
);

/*
 * Proof of possession of the identity seed: HMAC-SHA256(seed,
 * challenge). See the scope note above - this is a symmetric
 * proof, not a public-key signature.
 */
void identity_prove(
    const uint8_t* challenge,
    uint32_t challenge_length,
    uint8_t out_proof[IDENTITY_ID_SIZE]
);

#endif
