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

/*
 * Folds an event into the entropy pool (e.g. keystroke scancode).
 * Cheap enough to call from an interrupt handler. Credits a small
 * number of bits only when the event's timing looks irregular.
 */
void rng_add_entropy(uint32_t sample);

/*
 * 1 if the generator can be trusted for key material: either
 * RDRAND exists or enough event entropy has been credited.
 * Callers creating long-lived secrets must check this and gather
 * more entropy (see auth.c) when it returns 0.
 */
int rng_is_seeded(void);

/* Progress reporting for entropy-collection UIs. */
uint32_t rng_entropy_bits(void);
uint32_t rng_seed_threshold_bits(void);

void rng_get_bytes(
    uint8_t* buffer,
    uint32_t length
);

#endif
