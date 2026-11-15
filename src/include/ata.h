#ifndef SPECTREOS_ATA_H
#define SPECTREOS_ATA_H

#include <stdint.h>

#define ATA_SECTOR_SIZE 512

int ata_init(void);
int ata_present(void);
uint32_t ata_sector_count(void);

int ata_read_sector(
    uint32_t lba,
    void* buffer
);

int ata_write_sector(
    uint32_t lba,
    const void* buffer
);

#endif
