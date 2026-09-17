#include <stdint.h>

#include "blockdev.h"

static block_device_t devices[BLOCKDEV_MAX];
static uint32_t device_count = 0;

static void string_copy_bounded(
    char* destination,
    const char* source,
    uint32_t max_length
)
{
    uint32_t i = 0;

    while (source[i] != '\0' &&
           i < (max_length - 1))
    {
        destination[i] = source[i];
        i++;
    }

    destination[i] = '\0';
}

void blockdev_init(void)
{
    for (uint32_t i = 0; i < BLOCKDEV_MAX; i++)
    {
        devices[i].name[0] = '\0';
        devices[i].present = 0;
        devices[i].sector_count = 0;
        devices[i].read_sector = 0;
        devices[i].write_sector = 0;
        devices[i].in_use = 0;
    }

    device_count = 0;
}

int blockdev_register(
    const char* name,
    blockdev_present_fn present,
    blockdev_sector_count_fn sector_count,
    blockdev_read_fn read_sector,
    blockdev_write_fn write_sector
)
{
    if (name == 0 ||
        present == 0 ||
        sector_count == 0 ||
        read_sector == 0 ||
        write_sector == 0)
    {
        return -1;
    }

    if (device_count >= BLOCKDEV_MAX)
    {
        return -1;
    }

    uint32_t id = device_count;

    string_copy_bounded(
        devices[id].name,
        name,
        BLOCKDEV_NAME_MAX
    );

    devices[id].present = present;
    devices[id].sector_count = sector_count;
    devices[id].read_sector = read_sector;
    devices[id].write_sector = write_sector;
    devices[id].in_use = 1;

    device_count++;

    return (int)id;
}

uint32_t blockdev_count(void)
{
    return device_count;
}

const block_device_t* blockdev_get(uint32_t id)
{
    if (id >= BLOCKDEV_MAX ||
        !devices[id].in_use)
    {
        return 0;
    }

    return &devices[id];
}

int blockdev_present(uint32_t id)
{
    const block_device_t* device =
        blockdev_get(id);

    if (device == 0)
    {
        return 0;
    }

    return device->present();
}

uint32_t blockdev_sector_count(uint32_t id)
{
    const block_device_t* device =
        blockdev_get(id);

    if (device == 0)
    {
        return 0;
    }

    return device->sector_count();
}

int blockdev_read_sector(
    uint32_t id,
    uint32_t lba,
    void* buffer
)
{
    const block_device_t* device =
        blockdev_get(id);

    if (device == 0)
    {
        return 0;
    }

    return device->read_sector(lba, buffer);
}

int blockdev_write_sector(
    uint32_t id,
    uint32_t lba,
    const void* buffer
)
{
    const block_device_t* device =
        blockdev_get(id);

    if (device == 0)
    {
        return 0;
    }

    return device->write_sector(lba, buffer);
}
