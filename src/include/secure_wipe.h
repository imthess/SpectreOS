#ifndef SPECTREOS_SECURE_WIPE_H
#define SPECTREOS_SECURE_WIPE_H

#include <stdint.h>

/*
 * Zeroes a buffer through a volatile pointer so the compiler
 * cannot optimize the write away as a dead store (a plain
 * memset on a buffer that is never read again is legal for the
 * compiler to delete entirely). Used everywhere a password,
 * key, or other secret is about to go out of scope.
 */
void secure_wipe(
    void* buffer,
    uint32_t length
);

#endif
