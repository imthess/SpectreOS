#ifndef SPECTREOS_NET_BYTEORDER_H
#define SPECTREOS_NET_BYTEORDER_H

#include <stdint.h>

/*
 * Every protocol header in this stack is built/parsed byte by
 * byte through these helpers rather than by casting to a
 * uint16_t or uint32_t pointer and dereferencing it directly -
 * that would depend on both the compiler's struct packing and
 * the CPU's endianness matching network byte order, neither of
 * which this project wants to assume.
 */

static inline uint16_t net_read16(const uint8_t* p)
{
    return (uint16_t)(
        ((uint16_t)p[0] << 8) |
        (uint16_t)p[1]
    );
}

static inline void net_write16(
    uint8_t* p,
    uint16_t value
)
{
    p[0] = (uint8_t)(value >> 8);
    p[1] = (uint8_t)(value);
}

static inline uint32_t net_read32(const uint8_t* p)
{
    return
        ((uint32_t)p[0] << 24) |
        ((uint32_t)p[1] << 16) |
        ((uint32_t)p[2] << 8) |
        ((uint32_t)p[3]);
}

static inline void net_write32(
    uint8_t* p,
    uint32_t value
)
{
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)(value);
}

#endif
