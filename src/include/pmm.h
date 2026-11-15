#ifndef SPECTREOS_PMM_H
#define SPECTREOS_PMM_H

#include <stdint.h>

#define PMM_FRAME_SIZE 4096
#define PMM_MAX_MEMORY (128 * 1024 * 1024)
#define PMM_MAX_FRAMES (PMM_MAX_MEMORY / PMM_FRAME_SIZE)

void pmm_init(uint32_t multiboot_info);

uint32_t pmm_alloc_frame(void);
void pmm_free_frame(uint32_t address);

uint32_t pmm_get_total_frames(void);
uint32_t pmm_get_used_frames(void);
uint32_t pmm_get_free_frames(void);

#endif
