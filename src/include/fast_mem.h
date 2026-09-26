#ifndef SPECTREOS_FAST_MEM_H
#define SPECTREOS_FAST_MEM_H

#include <stdint.h>

/*
 * Drop-in replacements for the byte-at-a-time copy/zero loops
 * that were scattered across this project (fs.c's mem_copy,
 * ramdisk.c's sector loops, ...). Semantics are identical to a
 * plain byte loop - same result for any size or alignment - the
 * only difference is that the bulk of the work moves 4 bytes at
 * a time instead of 1, so a 512-byte sector copy is ~4x fewer
 * loop iterations. Any leftover 1-3 bytes (from a size that
 * isn't a multiple of 4, or misaligned pointers) are still
 * copied one byte at a time, so this is always correct
 * regardless of alignment - never only "correct when aligned".
 *
 * Verified in this project against the standard library's own
 * memcpy()/memset() across a range of sizes and offsets before
 * being wired into fs.c/ramdisk.c - see the project notes for
 * the comparison.
 */

void fast_mem_copy(
    void* destination,
    const void* source,
    uint32_t size
);

void fast_mem_set(
    void* destination,
    uint8_t value,
    uint32_t size
);

#endif
