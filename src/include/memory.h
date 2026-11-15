#ifndef SPECTREOS_MEMORY_H
#define SPECTREOS_MEMORY_H

#include <stdint.h>

typedef struct
{
    uint32_t lower_memory_kb;
    uint32_t upper_memory_kb;
    uint32_t total_memory_kb;
    uint32_t total_memory_mb;

    uint8_t  multiboot_memory_available;
} memory_info_t;

void memory_init(uint32_t multiboot_info_address);

uint32_t memory_get_info(memory_info_t* info);

#endif
