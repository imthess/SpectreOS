#include <stdint.h>

#include "rng.h"
<<<<<<< HEAD
#include "sha256.h"
#include "secure_wipe.h"
=======
>>>>>>> origin/main

/*
 * ============================================================
 * ENTROPY SOURCE
 *
<<<<<<< HEAD
 * Design: a SHA-256 based pool plus a hash-counter generator.
 *
 *   - Events (keystroke timing, etc.) are folded into a 32-byte
 *     pool with SHA-256(pool || sample || TSC). Each event is
 *     credited at most one bit (see rng_add_entropy);
 *     the pool is considered SEEDED once RNG_SEED_THRESHOLD_BITS
 *     have been credited (or RDRAND exists).
 *   - Output block i = SHA-256("out" || pool || counter_i). After
 *     every request the pool is ratcheted (hashed forward), so a
 *     later compromise of the pool cannot reveal earlier output.
 *   - If RDRAND is present, its words are XORed into the output,
 *     so a weak or backdoored RDRAND alone cannot make the
 *     output predictable once the pool is seeded, and the pool
 *     alone cannot if RDRAND is good.
 *
 * Honest limits: the "bits credited" figure is a heuristic, not
 * a measurement. Timing jitter seen by a guest under QEMU/TCG is
 * host-dependent. Anything that truly needs strong keys should
 * check rng_is_seeded() first (identity creation does).
=======
 * Preferred: the RDRAND instruction (hardware entropy source on
 * most x86 CPUs since ~2012). Detected via CPUID.1:ECX.30.
 *
 * Fallback (no RDRAND): an xorshift32 PRNG reseeded from the
 * CPU timestamp counter (RDTSC) on every call. This is NOT
 * cryptographically secure entropy - RDTSC is far too
 * predictable (it is, after all, just an incrementing counter)
 * for anyone who can influence or observe roughly when identity
 * generation happened. rng_hardware_available() reports which
 * path is active precisely so callers (identity.c) can warn the
 * user rather than silently generating a weak identity.
 *
 * This project has no interrupt-timing entropy pool (keyboard
 * jitter, disk seek timing, etc.) yet - that would be the
 * natural next step for strengthening the fallback.
>>>>>>> origin/main
 * ============================================================
 */

static int hardware_rng = 0;
<<<<<<< HEAD

#define RNG_SEED_THRESHOLD_BITS 256u

static uint8_t pool[SHA256_DIGEST_SIZE];
static uint32_t credited_bits = 0;
static uint32_t output_counter = 0;
static uint32_t last_sample_tsc = 0;
static uint32_t last_sample_delta = 0;
=======
static uint32_t fallback_state = 0x9e3779b9u;
>>>>>>> origin/main

static inline void cpuid(
    uint32_t leaf,
    uint32_t* eax,
    uint32_t* ebx,
    uint32_t* ecx,
    uint32_t* edx
)
{
    __asm__ volatile (
        "cpuid"
        : "=a"(*eax), "=b"(*ebx),
          "=c"(*ecx), "=d"(*edx)
        : "a"(leaf), "c"(0)
        : "memory"
    );
}

static uint32_t read_tsc_low(void)
{
    uint32_t low;
    uint32_t high;

    __asm__ volatile (
        "rdtsc"
        : "=a"(low), "=d"(high)
    );

    (void)high;

    return low;
}

static int rdrand_supported(void)
{
    uint32_t eax, ebx, ecx, edx;

    cpuid(1, &eax, &ebx, &ecx, &edx);

    return (ecx & (1u << 30)) != 0;
}

<<<<<<< HEAD
static void pool_mix(const void* data, uint32_t length)
{
    sha256_ctx_t ctx;

    sha256_init(&ctx);
    sha256_update(&ctx, "mix", 3);
    sha256_update(&ctx, pool, sizeof(pool));
    sha256_update(&ctx, data, length);
    sha256_final(&ctx, pool);
}

static void pool_ratchet(void)
{
    sha256_ctx_t ctx;

    sha256_init(&ctx);
    sha256_update(&ctx, "ratchet", 7);
    sha256_update(&ctx, pool, sizeof(pool));
    sha256_final(&ctx, pool);
}

=======
>>>>>>> origin/main
/*
 * Returns 1 on success. RDRAND can transiently fail under heavy
 * contention; retry a bounded number of times before giving up
 * (falling back to the software path for that call only).
 */
static int rdrand32(uint32_t* out)
{
    for (int attempt = 0; attempt < 16; attempt++)
    {
        uint8_t ok;

        __asm__ volatile (
            "rdrand %0\n"
            "setc %1"
            : "=r"(*out), "=qm"(ok)
        );

        if (ok)
        {
            return 1;
        }
    }

    return 0;
}

<<<<<<< HEAD
void rng_add_entropy(uint32_t sample)
{
    uint32_t now = read_tsc_low();
    uint32_t delta = now - last_sample_tsc;

    struct { uint32_t sample, tsc, delta; } record = { sample, now, delta };

    pool_mix(&record, sizeof(record));

    /*
     * Credit ONE bit, and only when this inter-event gap differs
     * from the previous one (machine-regular events such as a held
     * key's autorepeat earn nothing). One bit per event is
     * deliberately stingy: even scripted input with fixed spacing
     * shows scheduling jitter in the low TSC bits, so any larger
     * figure would let automated input "seed" the generator. A
     * real person typing a signup form plus the entropy prompt
     * still clears the threshold in well under a minute.
     */
    if (last_sample_tsc != 0 &&
        delta != last_sample_delta &&
        credited_bits < 0xFFFFFF00u)
    {
        credited_bits += 1;
    }

    last_sample_delta = delta;
    last_sample_tsc = now;
=======
static uint32_t xorshift32(void)
{
    uint32_t x = fallback_state;

    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;

    fallback_state = x;

    return x ^ read_tsc_low();
>>>>>>> origin/main
}

void rng_init(void)
{
<<<<<<< HEAD
    hardware_rng = rdrand_supported();

    for (uint32_t i = 0; i < sizeof(pool); i++)
    {
        pool[i] = 0;
    }

    credited_bits = 0;
    output_counter = 0;

    /*
     * Boot-time baseline only (TSC at init). Credited as zero:
     * it separates boots but is not treated as secret.
     */
    uint32_t tsc = read_tsc_low();
    pool_mix(&tsc, sizeof(tsc));

    last_sample_tsc = tsc;
    last_sample_delta = 0;

    if (hardware_rng)
    {
        uint32_t word;

        for (uint32_t i = 0; i < 8; i++)
        {
            if (rdrand32(&word))
            {
                pool_mix(&word, sizeof(word));
            }
        }
=======
    hardware_rng =
        rdrand_supported();

    fallback_state =
        read_tsc_low() ^ 0x9e3779b9u;

    if (fallback_state == 0)
    {
        fallback_state = 0xdeadbeefu;
>>>>>>> origin/main
    }
}

int rng_hardware_available(void)
{
    return hardware_rng;
}

<<<<<<< HEAD
int rng_is_seeded(void)
{
    return hardware_rng ||
           credited_bits >= RNG_SEED_THRESHOLD_BITS;
}

uint32_t rng_entropy_bits(void)
{
    return credited_bits;
}

uint32_t rng_seed_threshold_bits(void)
{
    return RNG_SEED_THRESHOLD_BITS;
}

=======
>>>>>>> origin/main
void rng_get_bytes(
    uint8_t* buffer,
    uint32_t length
)
{
    uint32_t produced = 0;

    while (produced < length)
    {
<<<<<<< HEAD
        uint8_t block[SHA256_DIGEST_SIZE];
        sha256_ctx_t ctx;

        output_counter++;

        sha256_init(&ctx);
        sha256_update(&ctx, "out", 3);
        sha256_update(&ctx, pool, sizeof(pool));
        sha256_update(&ctx, &output_counter, sizeof(output_counter));
        sha256_final(&ctx, block);

        uint32_t chunk = length - produced;

        if (chunk > sizeof(block))
        {
            chunk = sizeof(block);
=======
        uint32_t word;

        if (hardware_rng &&
            rdrand32(&word))
        {
            /* used as-is */
        }
        else
        {
            word = xorshift32();
        }

        uint32_t chunk =
            length - produced;

        if (chunk > 4)
        {
            chunk = 4;
>>>>>>> origin/main
        }

        for (uint32_t i = 0; i < chunk; i++)
        {
<<<<<<< HEAD
            uint8_t b = block[i];

            if (hardware_rng && (i % 4) == 0)
            {
                uint32_t word;

                if (rdrand32(&word))
                {
                    for (uint32_t j = 0; j < 4 && i + j < chunk; j++)
                    {
                        block[i + j] ^= (uint8_t)(word >> (j * 8));
                    }

                    b = block[i];
                }
            }

            buffer[produced + i] = b;
        }

        secure_wipe(block, sizeof(block));

        produced += chunk;
    }

    pool_ratchet();
=======
            buffer[produced + i] =
                (uint8_t)(word >> (i * 8));
        }

        produced += chunk;
    }
>>>>>>> origin/main
}
