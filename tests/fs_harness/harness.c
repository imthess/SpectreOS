#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "fs.h"
#include "ata.h"
#include "blockdev.h"
#ifndef LEGACY_FS
#include "ramdisk.h"
#include "crypto_disk.h"
#include "rng.h"
#endif

#define SECRET "SECRET-PLAINTEXT-MARKER-0123456789"

static int fail(const char* msg) { printf("FAIL: %s\n", msg); return 1; }

int main(int argc, char** argv)
{
    if (argc < 2) return fail("usage: harness <mode>");
    const char* mode = argv[1];

    blockdev_init();
    if (!ata_init()) return fail("ata_init");

#ifndef LEGACY_FS
    if (getenv("PRECREATE_CENC"))
    {
        /* Mirror kernel.c: ramdisk + random-nonce cenc0 + self-test,
         * all BEFORE fs_init(), so the fs claims crypto slot 1. */
        int ram_id = ramdisk_init();
        if (ram_id < 0) return fail("ramdisk_init");
        uint8_t k[32]; rng_get_bytes(k, 32);
        int cid = crypto_disk_create("cenc0", (uint32_t)ram_id, k);
        if (cid < 0) return fail("cenc0 create");
        if (!crypto_disk_self_test((uint32_t)cid, (uint32_t)ram_id, 1))
            return fail("cenc0 self-test");
        printf("boot self-test (cenc0) passed; blockdevs now=%u\n", blockdev_count());
    }
#endif

    if (!fs_init()) return fail("fs_init");
#ifndef LEGACY_FS
    printf("blockdevs after fs_init=%u\n", blockdev_count());
#endif

#ifndef LEGACY_FS
    printf("encrypted=%d\n", fs_is_encrypted());
#endif

    if (strcmp(mode, "format_and_write") == 0)
    {
        if (!fs_create("hello.txt")) return fail("create");
        int fd = fs_open("hello.txt");
        if (fd < 0) return fail("open");
        int n = fs_write(fd, SECRET, (uint32_t)strlen(SECRET));
        if (n != (int)strlen(SECRET)) return fail("write length");
        fs_close(fd);
        printf("OK wrote %d bytes\n", n);
        return 0;
    }

    if (strcmp(mode, "verify") == 0)
    {
        if (!fs_exists("hello.txt")) return fail("hello.txt missing after remount");
        int fd = fs_open("hello.txt");
        if (fd < 0) return fail("open after remount");
        char buf[128]; memset(buf, 0, sizeof(buf));
        int n = fs_read(fd, buf, sizeof(buf) - 1);
        fs_close(fd);
        if (n != (int)strlen(SECRET)) { printf("read length %d\n", n); return fail("read length mismatch"); }
        if (memcmp(buf, SECRET, strlen(SECRET)) != 0) return fail("content mismatch");
        printf("OK read back '%s'\n", buf);
        return 0;
    }

    if (strcmp(mode, "write_second") == 0)
    {
        if (!fs_create("second.txt")) return fail("create second");
        int fd = fs_open("second.txt");
        if (fd < 0) return fail("open second");
        const char* t = "second file content";
        if (fs_write(fd, t, (uint32_t)strlen(t)) != (int)strlen(t)) return fail("write second");
        fs_close(fd);
        printf("OK wrote second\n");
        return 0;
    }

    if (strcmp(mode, "verify_second") == 0)
    {
        int fd = fs_open("second.txt");
        if (fd < 0) return fail("open second");
        char buf[64]; memset(buf, 0, sizeof(buf));
        int n = fs_read(fd, buf, sizeof(buf) - 1);
        fs_close(fd);
        if (n != 19 || strcmp(buf, "second file content") != 0) return fail("second content");
        printf("OK second file intact\n");
        return 0;
    }

    if (strcmp(mode, "big_write") == 0)
    {
        static uint8_t big[5000];
        for (int i = 0; i < 5000; i++) big[i] = (uint8_t)((i * 7 + 3) % 251);
        if (!fs_create("big.bin")) return fail("create big");
        int fd = fs_open("big.bin");
        if (fd < 0) return fail("open big");
        if (fs_write(fd, big, 5000) != 5000) return fail("write big");
        fs_close(fd);
        printf("OK wrote 5000 bytes (spans multiple sectors)\n");
        return 0;
    }

    if (strcmp(mode, "big_verify") == 0)
    {
        static uint8_t big[5000], got[5000];
        for (int i = 0; i < 5000; i++) big[i] = (uint8_t)((i * 7 + 3) % 251);
        int fd = fs_open("big.bin");
        if (fd < 0) return fail("open big");
        int n = fs_read(fd, got, 5000);
        fs_close(fd);
        if (n != 5000) { printf("read %d\n", n); return fail("big length"); }
        if (memcmp(big, got, 5000) != 0) return fail("big content mismatch");
        printf("OK 5000-byte multi-sector file intact\n");
        return 0;
    }

    if (strcmp(mode, "delete_hello") == 0)
    {
        if (!fs_delete("hello.txt")) return fail("delete");
        if (fs_exists("hello.txt")) return fail("still exists after delete");
        printf("OK deleted\n");
        return 0;
    }

    if (strcmp(mode, "verify_deleted") == 0)
    {
        if (fs_exists("hello.txt")) return fail("hello.txt reappeared");
        printf("OK stays deleted across remount\n");
        return 0;
    }

    return fail("unknown mode");
}
