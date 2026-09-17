#ifndef SPECTREOS_BLOCKDEV_H
#define SPECTREOS_BLOCKDEV_H

#include <stdint.h>

/*
 * ============================================================
 * BLOCK DEVICE ABSTRACTION
 *
 * Any storage backend (ATA disk, RAM disk, and in the future
 * USB mass storage, etc.) implements this small interface and
 * registers itself here. Callers that only need "a disk" -
 * such as the filesystem - can go through this layer instead
 * of depending on a specific driver.
 *
 * ata.c is the reference implementation: it registers itself
 * as device 0 during ata_init() so existing behavior (and the
 * existing ata_* call sites in fs.c) is unchanged, while new
 * drivers can be added purely by registering here.
 * ============================================================
 */

#define BLOCKDEV_MAX   4
#define BLOCKDEV_NAME_MAX 16

typedef int (*blockdev_present_fn)(void);
typedef uint32_t (*blockdev_sector_count_fn)(void);

typedef int (*blockdev_read_fn)(
    uint32_t lba,
    void* buffer
);

typedef int (*blockdev_write_fn)(
    uint32_t lba,
    const void* buffer
);

typedef struct
{
    char name[BLOCKDEV_NAME_MAX];

    blockdev_present_fn present;
    blockdev_sector_count_fn sector_count;
    blockdev_read_fn read_sector;
    blockdev_write_fn write_sector;

    uint32_t in_use;

} block_device_t;

/*
 * Resets the registry. Call once during boot before any
 * driver registers itself.
 */
void blockdev_init(void);

/*
 * Registers a device. Returns the device id (>= 0) on success,
 * or -1 if the registry is full or an argument is invalid.
 */
int blockdev_register(
    const char* name,
    blockdev_present_fn present,
    blockdev_sector_count_fn sector_count,
    blockdev_read_fn read_sector,
    blockdev_write_fn write_sector
);

uint32_t blockdev_count(void);

const block_device_t* blockdev_get(uint32_t id);

int blockdev_present(uint32_t id);

uint32_t blockdev_sector_count(uint32_t id);

int blockdev_read_sector(
    uint32_t id,
    uint32_t lba,
    void* buffer
);

int blockdev_write_sector(
    uint32_t id,
    uint32_t lba,
    const void* buffer
);

#endif
