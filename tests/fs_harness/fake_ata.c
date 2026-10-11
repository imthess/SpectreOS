/* Fake ATA backed by a regular file, so persistence across separate
 * process runs simulates a real reboot with a real disk image. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "ata.h"
#include "blockdev.h"

#define FAKE_SECTORS 16384u   /* 8 MiB - plenty for the test */

static FILE* img = NULL;
static int ready = 0;

int ata_present(void) { return ready; }
uint32_t ata_sector_count(void) { return FAKE_SECTORS; }

int ata_read_sector(uint32_t lba, void* buffer)
{
    if (!ready || lba >= FAKE_SECTORS) return 0;
    if (fseek(img, (long)lba * 512, SEEK_SET) != 0) return 0;
    return fread(buffer, 512, 1, img) == 1;
}

int ata_write_sector(uint32_t lba, const void* buffer)
{
    if (!ready || lba >= FAKE_SECTORS) return 0;
    if (fseek(img, (long)lba * 512, SEEK_SET) != 0) return 0;
    if (fwrite(buffer, 512, 1, img) != 1) return 0;
    fflush(img);
    return 1;
}

int ata_init(void)
{
    const char* path = getenv("FAKE_DISK");
    if (!path) { fprintf(stderr, "FAKE_DISK not set\n"); return 0; }

    img = fopen(path, "r+b");
    if (!img) {
        /* create a blank, zero-filled image, like a fresh disk.img */
        img = fopen(path, "w+b");
        if (!img) return 0;
        uint8_t zero[512] = {0};
        for (uint32_t i = 0; i < FAKE_SECTORS; i++) fwrite(zero, 512, 1, img);
        fflush(img);
    }

    ready = 1;

    /* Same registration the real ata.c does. */
    blockdev_register("ata0", ata_present, ata_sector_count,
                      ata_read_sector, ata_write_sector);
    return 1;
}
