#include <stdint.h>

#include "checksum.h"

uint32_t checksum_add(
    uint32_t sum,
    const void* data,
    uint32_t length
)
{
    const uint8_t* bytes =
        (const uint8_t*)data;

    uint32_t i = 0;

    while (i + 1 < length)
    {
        uint16_t word =
            ((uint16_t)bytes[i] << 8) |
            (uint16_t)bytes[i + 1];

        sum += word;

        i += 2;
    }

    /*
     * Odd trailing byte: treated as the high byte of a word
     * whose low byte is zero, per RFC 1071.
     */
    if (i < length)
    {
        sum += (uint32_t)bytes[i] << 8;
    }

    return sum;
}

uint16_t checksum_finish(uint32_t sum)
{
    while (sum >> 16)
    {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    return (uint16_t)(~sum);
}

uint16_t internet_checksum(
    const void* data,
    uint32_t length
)
{
    return checksum_finish(
        checksum_add(0, data, length)
    );
}
