#include <stdint.h>

#include "ata.h"

/*
 * Primary ATA / IDE controller.
 * SpectreOS currently uses the primary master.
 */

#define ATA_DATA        0x1F0
#define ATA_ERROR       0x1F1
#define ATA_SECCOUNT0   0x1F2
#define ATA_LBA0        0x1F3
#define ATA_LBA1        0x1F4
#define ATA_LBA2        0x1F5
#define ATA_DRIVE       0x1F6
#define ATA_STATUS      0x1F7
#define ATA_COMMAND     0x1F7
#define ATA_ALTSTATUS   0x3F6

#define ATA_CMD_IDENTIFY 0xEC
#define ATA_CMD_READ     0x20
#define ATA_CMD_WRITE    0x30

#define ATA_SR_BSY  0x80
#define ATA_SR_DRDY 0x40
#define ATA_SR_DF   0x20
#define ATA_SR_DRQ  0x08
#define ATA_SR_ERR  0x01

#define ATA_TIMEOUT 1000000UL

static int disk_ready = 0;
static uint32_t disk_sectors = 0;

static inline void outb(
    uint16_t port,
    uint8_t value
)
{
    __asm__ volatile (
        "outb %0, %1"
        :
        : "a"(value), "Nd"(port)
    );
}

static inline uint8_t inb(
    uint16_t port
)
{
    uint8_t value;

    __asm__ volatile (
        "inb %1, %0"
        : "=a"(value)
        : "Nd"(port)
    );

    return value;
}

static inline uint16_t inw(
    uint16_t port
)
{
    uint16_t value;

    __asm__ volatile (
        "inw %1, %0"
        : "=a"(value)
        : "Nd"(port)
    );

    return value;
}

static inline void insw(
    uint16_t port,
    void* address,
    uint32_t count
)
{
    __asm__ volatile (
        "cld\n\t"
        "rep insw"
        : "+D"(address), "+c"(count)
        : "d"(port)
        : "memory"
    );
}

static inline void outsw(
    uint16_t port,
    const void* address,
    uint32_t count
)
{
    __asm__ volatile (
        "cld\n\t"
        "rep outsw"
        : "+S"(address), "+c"(count)
        : "d"(port)
        : "memory"
    );
}

static void ata_delay(void)
{
    inb(ATA_ALTSTATUS);
    inb(ATA_ALTSTATUS);
    inb(ATA_ALTSTATUS);
    inb(ATA_ALTSTATUS);
}

static int ata_wait_not_busy(void)
{
    uint32_t timeout = ATA_TIMEOUT;

    while (timeout--)
    {
        uint8_t status = inb(ATA_STATUS);

        if (!(status & ATA_SR_BSY))
        {
            return 1;
        }
    }

    return 0;
}

static int ata_wait_drq(void)
{
    uint32_t timeout = ATA_TIMEOUT;

    while (timeout--)
    {
        uint8_t status = inb(ATA_STATUS);

        if (status & ATA_SR_ERR)
        {
            return 0;
        }

        if (status & ATA_SR_DF)
        {
            return 0;
        }

        if (!(status & ATA_SR_BSY) &&
            (status & ATA_SR_DRQ))
        {
            return 1;
        }
    }

    return 0;
}

static int ata_select(uint32_t lba)
{
    if (lba >= 0x10000000UL)
    {
        return 0;
    }

    outb(
        ATA_DRIVE,
        (uint8_t)(
            0xE0 |
            ((lba >> 24) & 0x0F)
        )
    );

    ata_delay();

    if (!ata_wait_not_busy())
    {
        return 0;
    }

    outb(ATA_SECCOUNT0, 1);

    outb(
        ATA_LBA0,
        (uint8_t)(lba & 0xFF)
    );

    outb(
        ATA_LBA1,
        (uint8_t)((lba >> 8) & 0xFF)
    );

    outb(
        ATA_LBA2,
        (uint8_t)((lba >> 16) & 0xFF)
    );

    return 1;
}

int ata_init(void)
{
    uint16_t identify[256];

    disk_ready = 0;
    disk_sectors = 0;

    /*
     * Select primary master.
     */
    outb(ATA_DRIVE, 0xE0);

    ata_delay();

    /*
     * IDENTIFY requires these registers
     * to be zero.
     */
    outb(ATA_SECCOUNT0, 0);
    outb(ATA_LBA0, 0);
    outb(ATA_LBA1, 0);
    outb(ATA_LBA2, 0);

    outb(
        ATA_COMMAND,
        ATA_CMD_IDENTIFY
    );

    uint8_t status = inb(ATA_STATUS);

    /*
     * No device.
     */
    if (status == 0)
    {
        return 0;
    }

    if (!ata_wait_not_busy())
    {
        return 0;
    }

    status = inb(ATA_STATUS);

    if (status & ATA_SR_ERR)
    {
        return 0;
    }

    if (status & ATA_SR_DF)
    {
        return 0;
    }

    if (!(status & ATA_SR_DRQ))
    {
        return 0;
    }

    /*
     * Read IDENTIFY data.
     */
    for (uint32_t i = 0; i < 256; i++)
    {
        identify[i] = inw(ATA_DATA);
    }

    /*
     * IDENTIFY words 60-61:
     * number of 28-bit LBA sectors.
     */
    disk_sectors =
        ((uint32_t)identify[61] << 16) |
        (uint32_t)identify[60];

    if (disk_sectors == 0)
    {
        return 0;
    }

    disk_ready = 1;

    return 1;
}

int ata_present(void)
{
    return disk_ready;
}

uint32_t ata_sector_count(void)
{
    return disk_sectors;
}

int ata_read_sector(
    uint32_t lba,
    void* buffer
)
{
    if (!disk_ready ||
        buffer == 0)
    {
        return 0;
    }

    if (lba >= disk_sectors)
    {
        return 0;
    }

    if (!ata_select(lba))
    {
        return 0;
    }

    outb(
        ATA_COMMAND,
        ATA_CMD_READ
    );

    if (!ata_wait_drq())
    {
        return 0;
    }

    /*
     * 256 words = 512 bytes.
     */
    insw(
        ATA_DATA,
        buffer,
        256
    );

    uint8_t status = inb(ATA_STATUS);

    if (status & ATA_SR_ERR)
    {
        return 0;
    }

    if (status & ATA_SR_DF)
    {
        return 0;
    }

    return 1;
}

int ata_write_sector(
    uint32_t lba,
    const void* buffer
)
{
    if (!disk_ready ||
        buffer == 0)
    {
        return 0;
    }

    if (lba >= disk_sectors)
    {
        return 0;
    }

    if (!ata_select(lba))
    {
        return 0;
    }

    outb(
        ATA_COMMAND,
        ATA_CMD_WRITE
    );

    if (!ata_wait_drq())
    {
        return 0;
    }

    /*
     * 256 words = 512 bytes.
     */
    outsw(
        ATA_DATA,
        buffer,
        256
    );

    if (!ata_wait_not_busy())
    {
        return 0;
    }

    uint8_t status = inb(ATA_STATUS);

    if (status & ATA_SR_ERR)
    {
        return 0;
    }

    if (status & ATA_SR_DF)
    {
        return 0;
    }

    return 1;
}
