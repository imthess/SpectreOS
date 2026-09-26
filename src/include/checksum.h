#ifndef SPECTREOS_CHECKSUM_H
#define SPECTREOS_CHECKSUM_H

#include <stdint.h>

/*
 * RFC 1071 Internet checksum, split into add/finish so callers
 * (UDP, TCP) can fold a pseudo-header, the protocol header, and
 * the payload into one running sum before finishing it - the
 * standard technique for checksums that cover more than one
 * contiguous buffer.
 *
 * `data` is treated as a sequence of 16-bit big-endian words,
 * matching how the bytes actually sit in a network packet - so
 * callers pass raw packet bytes here, not host-order integers.
 *
 * Verified against the well-known Wikipedia IPv4-header example
 * (20-byte header -> checksum 0xb1e6) before being wired into
 * ip.c.
 */
uint32_t checksum_add(
    uint32_t sum,
    const void* data,
    uint32_t length
);

uint16_t checksum_finish(uint32_t sum);

/*
 * Convenience one-shot wrapper equivalent to
 * checksum_finish(checksum_add(0, data, length)).
 */
uint16_t internet_checksum(
    const void* data,
    uint32_t length
);

#endif
