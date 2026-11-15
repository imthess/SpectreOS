#include <stdint.h>

#include "fast_mem.h"

void fast_mem_copy(
    void* destination,
    const void* source,
    uint32_t size
)
{
    uint8_t* d = (uint8_t*)destination;
    const uint8_t* s = (const uint8_t*)source;

    /*
     * Word-copy only once both pointers share the same
     * alignment relative to a 4-byte boundary - copying 4 bytes
     * at a time from a source and destination that are offset
     * from each other by 1-3 bytes would require unaligned
     * 32-bit accesses, which are slower (and on some non-x86
     * targets, illegal) rather than faster. Falling back to a
     * byte loop in that case is still correct, just not sped
     * up - a safe degradation, not a bug.
     */
    uint32_t src_align =
        (uint32_t)(uintptr_t)s & 3u;

    uint32_t dst_align =
        (uint32_t)(uintptr_t)d & 3u;

    if (src_align == dst_align)
    {
        while (((uint32_t)(uintptr_t)d & 3u) != 0 &&
               size > 0)
        {
            *d++ = *s++;
            size--;
        }

        uint32_t* d32 = (uint32_t*)d;
        const uint32_t* s32 = (const uint32_t*)s;

        while (size >= 4)
        {
            *d32++ = *s32++;
            size -= 4;
        }

        d = (uint8_t*)d32;
        s = (const uint8_t*)s32;
    }

    while (size > 0)
    {
        *d++ = *s++;
        size--;
    }
}

void fast_mem_set(
    void* destination,
    uint8_t value,
    uint32_t size
)
{
    uint8_t* d = (uint8_t*)destination;

    while (((uint32_t)(uintptr_t)d & 3u) != 0 &&
           size > 0)
    {
        *d++ = value;
        size--;
    }

    uint32_t word =
        ((uint32_t)value << 24) |
        ((uint32_t)value << 16) |
        ((uint32_t)value << 8) |
        (uint32_t)value;

    uint32_t* d32 = (uint32_t*)d;

    while (size >= 4)
    {
        *d32++ = word;
        size -= 4;
    }

    d = (uint8_t*)d32;

    while (size > 0)
    {
        *d++ = value;
        size--;
    }
}
