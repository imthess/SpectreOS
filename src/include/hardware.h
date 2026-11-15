#ifndef SPECTREOS_HARDWARE_H
#define SPECTREOS_HARDWARE_H

#include <stdint.h>

typedef struct
{
    uint32_t available;

    uint32_t family;
    uint32_t model;
    uint32_t stepping;

    char vendor[13];
    char brand[49];

} cpu_info_t;

void hardware_init(void);

void hardware_get_cpu_info(
    cpu_info_t* info
);

int hardware_available(void);

uint32_t hardware_get_cpuid_max(void);

#endif
