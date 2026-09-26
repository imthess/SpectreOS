#ifndef SPECTREOS_RNG_H
#define SPECTREOS_RNG_H

#include <stdint.h>

void rng_init(void);

/*
 * Returns 1 if the hardware RDRAND instruction is available and
 * being used as the entropy source, 0 if the (much weaker)
 * software fallback is in use. Callers that need to warn the
 * user about weak entropy (e.g. during identity generation)
 * should check this.
 */
int rng_hardware_available(void);

void rng_get_bytes(
    uint8_t* buffer,
    uint32_t length
);

#endif
