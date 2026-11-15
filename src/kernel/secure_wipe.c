#include <stdint.h>

#include "secure_wipe.h"

void secure_wipe(
    void* buffer,
    uint32_t length
)
{
    volatile uint8_t* p =
        (volatile uint8_t*)buffer;

    for (uint32_t i = 0; i < length; i++)
    {
        p[i] = 0;
    }
}
