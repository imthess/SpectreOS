#include <stdint.h>

#include "ramdisk.h"
#include "blockdev.h"
#include "ata.h"
#include "fast_mem.h"

/*
 * Small fixed-size RAM disk. 512 sectors * 512 bytes = 256 KiB.
 * Kept modest since it lives in the kernel's static data and
 * this is a from-scratch freestanding kernel with no demand
 * paging for its own data segment.
 */
#define RAMDISK_SECTORS 512

static uint8_t storage[RAMDISK_SECTORS][ATA_SECTOR_SIZE];
static int ready = 0;

static int ramdisk_present(void)
{
    return ready;
}

static uint32_t ramdisk_sector_count(void)
{
    return RAMDISK_SECTORS;
}

static int ramdisk_read_sector(
    uint32_t lba,
    void* buffer
)
{
    if (!ready ||
        buffer == 0 ||
        lba >= RAMDISK_SECTORS)
    {
        return 0;
    }

    uint8_t* destination = (uint8_t*)buffer;
    const uint8_t* source = storage[lba];

    fast_mem_copy(destination, source, ATA_SECTOR_SIZE);

    return 1;
}

static int ramdisk_write_sector(
    uint32_t lba,
    const void* buffer
)
{
    if (!ready ||
        buffer == 0 ||
        lba >= RAMDISK_SECTORS)
    {
        return 0;
    }

    const uint8_t* source = (const uint8_t*)buffer;
    uint8_t* destination = storage[lba];

    fast_mem_copy(destination, source, ATA_SECTOR_SIZE);

    return 1;
}

int ramdisk_init(void)
{
    for (uint32_t i = 0; i < RAMDISK_SECTORS; i++)
    {
        fast_mem_set(storage[i], 0, ATA_SECTOR_SIZE);
    }

    ready = 1;

    return blockdev_register(
        "ram0",
        ramdisk_present,
        ramdisk_sector_count,
        ramdisk_read_sector,
        ramdisk_write_sector
    );
}
