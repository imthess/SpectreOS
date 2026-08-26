#ifndef SPECTREOS_PIT_H
#define SPECTREOS_PIT_H

#include <stdint.h>

void pit_init(uint32_t frequency);
uint32_t pit_get_ticks(void);
void pit_tick(void);

#endif