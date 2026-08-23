#ifndef SPECTREOS_HARDWARE_H
#define SPECTREOS_HARDWARE_H

#include <stdint.h>

typedef struct
{
    char vendor[13];
    char brand[49];

    uint32_t eax_max_basic;
    uint32_t eax_max_extended;

    uint32_t family;
    uint32_t model;
    uint32_t stepping;

    uint32_t features_ecx;
    uint32_t features_edx;
} cpu_info_t;

void hardware_cpu_detect(cpu_info_t* info);

#endif
