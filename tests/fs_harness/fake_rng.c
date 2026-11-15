#include <stdio.h>
#include <stdint.h>
#include "rng.h"
void rng_init(void) {}
int rng_hardware_available(void) { return 1; }
void rng_get_bytes(uint8_t* buffer, uint32_t length)
{
    FILE* f = fopen("/dev/urandom", "rb");
    if (f) { fread(buffer, 1, length, f); fclose(f); }
}
