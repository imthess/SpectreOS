#ifndef SPECTREOS_PAGING_H
#define SPECTREOS_PAGING_H

#include <stdint.h>

void paging_init(void);

void paging_enable(void);

uint32_t paging_get_directory(void);

#endif
