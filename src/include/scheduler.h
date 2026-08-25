
#ifndef SPECTREOS_SCHEDULER_H
#define SPECTREOS_SCHEDULER_H

#include <stdint.h>

void scheduler_init(void);

uint32_t scheduler_tick(
    uint32_t current_esp
);

uint64_t scheduler_get_switches(void);

#endif

