#include <stdint.h>

#include "rng.h"

/*
 * ============================================================
 * ENTROPY SOURCE
 *
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
 * ============================================================
 */

static int hardware_rng = 0;
static uint32_t fallback_state = 0x9e3779b9u;

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

static uint32_t xorshift32(void)
{
    uint32_t x = fallback_state;

    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;

    fallback_state = x;

    return x ^ read_tsc_low();
}

void rng_init(void)
{
    hardware_rng =
        rdrand_supported();

    fallback_state =
        read_tsc_low() ^ 0x9e3779b9u;

    if (fallback_state == 0)
    {
        fallback_state = 0xdeadbeefu;
    }
}

int rng_hardware_available(void)
{
    return hardware_rng;
}

void rng_get_bytes(
    uint8_t* buffer,
    uint32_t length
)
{
    uint32_t produced = 0;

    while (produced < length)
    {
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
        }

        for (uint32_t i = 0; i < chunk; i++)
        {
            buffer[produced + i] =
                (uint8_t)(word >> (i * 8));
        }

        produced += chunk;
    }
}
