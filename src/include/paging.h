#ifndef SPECTREOS_PAGING_H
#define SPECTREOS_PAGING_H

#include <stdint.h>

void paging_init(void);

void paging_enable(void);

uint32_t paging_get_directory(void);

/* Page-table entry flag bits (x86 4 KiB pages). */
#define PAGING_PRESENT 0x001u
#define PAGING_WRITE   0x002u
#define PAGING_USER    0x004u

/*
 * Walks the live page directory/table for `address` and returns
 * the effective permission bits (PAGING_PRESENT | PAGING_WRITE |
 * PAGING_USER) for the page that contains it. Returns 0 if the
 * page is not mapped. "Effective" means the AND of the directory
 * and table entries, matching what the MMU enforces.
 */
uint32_t paging_page_flags(uint32_t address);

/*
 * Returns 1 only if every page overlapping [address, address+length)
 * is mapped with at least the requested permissions and the range
 * does not wrap around the 32-bit address space. A zero-length
 * range is valid.
 */
int paging_range_ok(
    uint32_t address,
    uint32_t length,
    uint32_t required_flags
);

#endif
