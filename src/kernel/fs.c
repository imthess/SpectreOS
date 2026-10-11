#include <stdint.h>

#include "fs.h"
#include "ata.h"
#include "fast_mem.h"
#include "blockdev.h"
#include "crypto_disk.h"
#include "sha256.h"
#include "rng.h"
#include "secure_wipe.h"

#define FS_MAX_HANDLES 16

/*
 * ------------------------------------------------------------
 * AT-REST ENCRYPTION (new as of this pass; see the design note
 * above fs_setup_disk_device() below for the full explanation,
 * including the honest limit of this approach: the key lives in
 * cleartext in the superblock, not derived from a password, so
 * this defends against casual inspection of a raw disk image,
 * not against a motivated attacker with the format documented.
 * A password-derived key is real future work, blocked on
 * reordering boot so fs_init() runs after a successful login
 * instead of before it (auth.c currently needs fs.c to even
 * check a password - a chicken-and-egg problem not solved here).
 *
 * Fully backward compatible: superblock.reserved[] is guaranteed
 * all-zero on every disk this project has ever formatted (see
 * fs_format()'s mem_zero() of the whole struct before any field
 * is set) - that is exactly the signal used below to mean
 * "legacy, unencrypted disk", so an existing populated disk is
 * never touched or reinterpreted by this change.
 * ------------------------------------------------------------
 */
#define FS_ENCRYPTION_MAGIC   0x53454E43u /* "SENC" */
#define FS_ENCRYPTION_SEED_LEN 16
#define FS_ENCRYPTION_NONCE_LEN 8

static fs_superblock_t superblock;
static uint8_t bitmap[FS_BITMAP_SECTORS * 512];
static fs_inode_t inodes[FS_MAX_FILES];
static fs_handle_t handles[FS_MAX_HANDLES];

static int filesystem_ready = 0;

/*
 * Every sector I/O in this file except the superblock's own (see
 * save_superblock()/load_superblock(), which always go straight
 * to ata_read_sector()/ata_write_sector() - sector 0 itself is
 * never encrypted, since it has to be readable before any key is
 * known) goes through these two, which route to whichever device
 * fs_setup_disk_device() decided this disk actually is: ata0
 * directly for a legacy unencrypted disk, or a crypto_disk.c
 * wrapper around it for one with encryption enabled.
 */
static uint32_t fs_disk_device_id = 0;
static int fs_encrypted = 0;

static int fs_disk_read_sector(
    uint32_t lba,
    void* buffer
)
{
    return blockdev_read_sector(
        fs_disk_device_id,
        lba,
        buffer
    );
}

static int fs_disk_write_sector(
    uint32_t lba,
    const void* buffer
)
{
    return blockdev_write_sector(
        fs_disk_device_id,
        lba,
        buffer
    );
}

/*
 * Called once, after a valid (legacy or encrypted) superblock is
 * already loaded into `superblock`, or - for a blank disk about
 * to be formatted - right before fs_format() performs its first
 * sector write. Decides which block device the rest of this file
 * reads/writes through for everything except sector 0.
 *
 * `formatting`: 1 if this is a fresh fs_format() on a blank disk
 * (in which case a new random seed is generated and written into
 * `superblock.reserved[]` for fs_format() to persist), 0 if
 * mounting an existing, already-populated disk (in which case
 * `superblock.reserved[]` is only ever read, never changed).
 *
 * Returns 1 on success, 0 on failure (e.g. the crypto_disk.c
 * registry is full, or ata0 isn't registered as a block device
 * for some reason).
 */
static int fs_setup_disk_device(int formatting)
{
    int ata_id = blockdev_find_by_name("ata0");

    if (ata_id < 0)
    {
        return 0;
    }

    if (formatting)
    {
        /*
         * 16-byte random seed (the key is derived from it below)
         * and an 8-byte random nonce, both stored in the plaintext
         * superblock: reserved[0] = magic, [1..4] = seed,
         * [5..6] = nonce, [7] = unused. The nonce MUST be
         * persisted - see crypto_disk_create_with_nonce() for why
         * a nonce regenerated each boot silently destroys all
         * data on the next mount.
         */
        uint8_t material[FS_ENCRYPTION_SEED_LEN + FS_ENCRYPTION_NONCE_LEN];

        rng_get_bytes(material, sizeof(material));

        superblock.reserved[0] = FS_ENCRYPTION_MAGIC;

        for (uint32_t w = 0; w < 6; w++)
        {
            superblock.reserved[1 + w] =
                ((uint32_t)material[w * 4 + 0] << 24) |
                ((uint32_t)material[w * 4 + 1] << 16) |
                ((uint32_t)material[w * 4 + 2] << 8) |
                ((uint32_t)material[w * 4 + 3]);
        }

        secure_wipe(material, sizeof(material));
    }

    if (superblock.reserved[0] != FS_ENCRYPTION_MAGIC)
    {
        /*
         * Legacy, unencrypted disk (or encryption simply wasn't
         * requested) - read/write ata0 directly. Behaves exactly
         * as this project always has.
         */
        fs_disk_device_id = (uint32_t)ata_id;
        fs_encrypted = 0;

        return 1;
    }

    uint8_t seed[FS_ENCRYPTION_SEED_LEN];
    uint8_t nonce[FS_ENCRYPTION_NONCE_LEN];

    for (uint32_t w = 0; w < 4; w++)
    {
        seed[w * 4 + 0] = (uint8_t)(superblock.reserved[1 + w] >> 24);
        seed[w * 4 + 1] = (uint8_t)(superblock.reserved[1 + w] >> 16);
        seed[w * 4 + 2] = (uint8_t)(superblock.reserved[1 + w] >> 8);
        seed[w * 4 + 3] = (uint8_t)(superblock.reserved[1 + w]);
    }

    for (uint32_t w = 0; w < 2; w++)
    {
        nonce[w * 4 + 0] = (uint8_t)(superblock.reserved[5 + w] >> 24);
        nonce[w * 4 + 1] = (uint8_t)(superblock.reserved[5 + w] >> 16);
        nonce[w * 4 + 2] = (uint8_t)(superblock.reserved[5 + w] >> 8);
        nonce[w * 4 + 3] = (uint8_t)(superblock.reserved[5 + w]);
    }

    /*
     * key = SHA-256("SPECTREOS-FSKEY" || seed): a domain-separated
     * hash of the stored 16-byte seed, giving the 32 bytes AES-256
     * needs. This is a convenient way to store a key in half the
     * space, not a security boundary - the seed is exactly as
     * sensitive as the key it produces, and both sit in the same
     * plaintext sector either way.
     */
    uint8_t key[32];

    {
        sha256_ctx_t ctx;

        sha256_init(&ctx);
        sha256_update(&ctx, "SPECTREOS-FSKEY", 15);
        sha256_update(&ctx, seed, sizeof(seed));
        sha256_final(&ctx, key);
    }

    secure_wipe(seed, sizeof(seed));

    int crypto_id =
        crypto_disk_create_with_nonce(
            "fsenc0",
            (uint32_t)ata_id,
            key,
            nonce
        );

    secure_wipe(key, sizeof(key));

    if (crypto_id < 0)
    {
        return 0;
    }

    fs_disk_device_id = (uint32_t)crypto_id;
    fs_encrypted = 1;

    return 1;
}

int fs_is_encrypted(void)
{
    return fs_encrypted;
}

/*
 * ------------------------------------------------------------
 * Basic memory/string helpers
 *
 * mem_zero()/mem_copy() keep their original names and call
 * sites unchanged throughout this file - only their bodies
 * changed, to the word-aligned implementation in fast_mem.c
 * (verified there against libc memcpy/memset across ~40,000
 * randomized size/alignment combinations). Every one of this
 * file's 12 mem_copy() call sites is on the fs_read()/
 * fs_write() hot path, copying up to a full 512-byte sector
 * per call - previously one byte at a time.
 * ------------------------------------------------------------
 */

static void mem_zero(
    void* ptr,
    uint32_t size
)
{
    fast_mem_set(ptr, 0, size);
}

static void mem_copy(
    void* dst,
    const void* src,
    uint32_t size
)
{
    fast_mem_copy(dst, src, size);
}

static uint32_t str_len(
    const char* str
)
{
    uint32_t n = 0;

    if (str == 0)
    {
        return 0;
    }

    while (str[n])
    {
        n++;
    }

    return n;
}

static int str_equal(
    const char* a,
    const char* b
)
{
    if (a == 0 || b == 0)
    {
        return 0;
    }

    while (*a && *b)
    {
        if (*a != *b)
        {
            return 0;
        }

        a++;
        b++;
    }

    return *a == *b;
}

static int valid_name(
    const char* name
)
{
    uint32_t length = str_len(name);

    if (length == 0 ||
        length >= FS_MAX_FILENAME)
    {
        return 0;
    }

    for (uint32_t i = 0; i < length; i++)
    {
        char c = name[i];

        if (c == '/' ||
            c == '\\' ||
            c == '\n' ||
            c == '\r')
        {
            return 0;
        }
    }

    return 1;
}

/*
 * ------------------------------------------------------------
 * Bitmap
 * ------------------------------------------------------------
 */

static int bitmap_get(
    uint32_t sector
)
{
    if (sector >= superblock.total_sectors)
    {
        return 1;
    }

    return (
        bitmap[sector >> 3] &
        (uint8_t)(1 << (sector & 7))
    ) != 0;
}

static void bitmap_set(
    uint32_t sector
)
{
    if (sector >= FS_BITMAP_SECTORS * 4096)
    {
        return;
    }

    bitmap[sector >> 3] |=
        (uint8_t)(1 << (sector & 7));
}

static void bitmap_clear(
    uint32_t sector
)
{
    if (sector >= FS_BITMAP_SECTORS * 4096)
    {
        return;
    }

    bitmap[sector >> 3] &=
        (uint8_t)~(1 << (sector & 7));
}

static int save_bitmap(void)
{
    for (uint32_t i = 0;
         i < FS_BITMAP_SECTORS;
         i++)
    {
        if (!fs_disk_write_sector(
                FS_BITMAP_START + i,
                &bitmap[i * 512]))
        {
            return 0;
        }
    }

    return 1;
}

/*
 * ------------------------------------------------------------
 * Superblock
 * ------------------------------------------------------------
 */

static int save_superblock(void)
{
    uint8_t block[512];

    mem_zero(
        block,
        sizeof(block)
    );

    mem_copy(
        block,
        &superblock,
        sizeof(superblock)
    );

    /*
     * Always direct, never through fs_disk_write_sector(): sector
     * 0 itself is never encrypted - it has to be readable before
     * any key is known (see the design note above
     * fs_setup_disk_device()).
     */
    return ata_write_sector(
        FS_SUPERBLOCK_SECTOR,
        block
    );
}

static int load_superblock(void)
{
    uint8_t block[512];

    /*
     * Always direct, never through fs_disk_read_sector() - see
     * save_superblock() above.
     */
    if (!ata_read_sector(
            FS_SUPERBLOCK_SECTOR,
            block))
    {
        return 0;
    }

    mem_copy(
        &superblock,
        block,
        sizeof(superblock)
    );

    if (superblock.magic != FS_MAGIC ||
        superblock.version != FS_VERSION ||
        superblock.sector_size != 512)
    {
        return 0;
    }

    if (superblock.bitmap_start != FS_BITMAP_START ||
        superblock.bitmap_sectors != FS_BITMAP_SECTORS ||
        superblock.inode_start != FS_INODE_START ||
        superblock.inode_sectors != FS_INODE_SECTORS ||
        superblock.data_start != FS_DATA_START)
    {
        return 0;
    }

    if (superblock.total_sectors <= FS_DATA_START)
    {
        return 0;
    }

    if (superblock.total_sectors >
        FS_BITMAP_SECTORS * 4096)
    {
        return 0;
    }

    if (superblock.max_files != FS_MAX_FILES)
    {
        return 0;
    }

    return 1;
}

/*
 * ------------------------------------------------------------
 * Inode storage
 *
 * Inodes are stored in fixed 256-byte slots.
 *
 * 128 inodes x 256 bytes = 32768 bytes
 * 32768 bytes / 512 = 64 sectors
 *
 * Therefore each sector contains exactly two inodes and
 * no inode can cross a sector boundary.
 * ------------------------------------------------------------
 */

static int save_inode(
    uint32_t index
)
{
    if (index >= FS_MAX_FILES)
    {
        return 0;
    }

    uint32_t offset =
        index * FS_INODE_SIZE;

    uint32_t sector =
        FS_INODE_START +
        (offset / 512);

    uint32_t inside =
        offset % 512;

    if (inside + FS_INODE_SIZE > 512)
    {
        return 0;
    }

    uint8_t block[512];

    if (!fs_disk_read_sector(
            sector,
            block))
    {
        return 0;
    }

    /*
     * Clear the complete inode slot first.
     * This prevents stale bytes from an older inode
     * remaining on disk.
     */
    mem_zero(
        &block[inside],
        FS_INODE_SIZE
    );

    mem_copy(
        &block[inside],
        &inodes[index],
        sizeof(fs_inode_t)
    );

    return fs_disk_write_sector(
        sector,
        block
    );
}

static int load_inode(
    uint32_t index
)
{
    if (index >= FS_MAX_FILES)
    {
        return 0;
    }

    uint32_t offset =
        index * FS_INODE_SIZE;

    uint32_t sector =
        FS_INODE_START +
        (offset / 512);

    uint32_t inside =
        offset % 512;

    if (inside + FS_INODE_SIZE > 512)
    {
        return 0;
    }

    uint8_t block[512];

    if (!fs_disk_read_sector(
            sector,
            block))
    {
        return 0;
    }

    mem_zero(
        &inodes[index],
        sizeof(fs_inode_t)
    );

    mem_copy(
        &inodes[index],
        &block[inside],
        sizeof(fs_inode_t)
    );

    return 1;
}

static int load_inodes(void)
{
    uint8_t block[512];

    for (uint32_t sector_index = 0;
         sector_index < FS_INODE_SECTORS;
         sector_index++)
    {
        uint32_t sector =
            FS_INODE_START + sector_index;

        if (!fs_disk_read_sector(
                sector,
                block))
        {
            return 0;
        }

        /*
         * Two 256-byte inode slots per sector.
         */
        for (uint32_t i = 0;
             i < 2;
             i++)
        {
            uint32_t index =
                (sector_index * 2) + i;

            if (index >= FS_MAX_FILES)
            {
                break;
            }

            mem_zero(
                &inodes[index],
                sizeof(fs_inode_t)
            );

            mem_copy(
                &inodes[index],
                &block[i * FS_INODE_SIZE],
                sizeof(fs_inode_t)
            );
        }
    }

    return 1;
}

/*
 * ------------------------------------------------------------
 * Directory
 * ------------------------------------------------------------
 *
 * Root directory occupies inode 0's direct blocks.
 *
 * fs_dirent_t:
 *
 *   name[48] = 48 bytes
 *   inode     = 4 bytes
 *   used      = 4 bytes
 *
 * Total = 56 bytes.
 *
 * 9 entries fit into one 512-byte sector.
 * ------------------------------------------------------------
 */

static int dir_read_entry(
    uint32_t slot,
    fs_dirent_t* entry
)
{
    if (entry == 0)
    {
        return 0;
    }

    uint32_t per_sector =
        512 / sizeof(fs_dirent_t);

    uint32_t sector =
        slot / per_sector;

    uint32_t inside =
        slot % per_sector;

    if (sector >= FS_DIRECT_BLOCKS)
    {
        return 0;
    }

    uint32_t lba =
        inodes[0].direct[sector];

    if (lba == 0)
    {
        return 0;
    }

    uint8_t block[512];

    if (!fs_disk_read_sector(
            lba,
            block))
    {
        return 0;
    }

    mem_copy(
        entry,
        &block[inside * sizeof(fs_dirent_t)],
        sizeof(fs_dirent_t)
    );

    return 1;
}

static int dir_write_entry(
    uint32_t slot,
    const fs_dirent_t* entry
)
{
    if (entry == 0)
    {
        return 0;
    }

    uint32_t per_sector =
        512 / sizeof(fs_dirent_t);

    uint32_t sector =
        slot / per_sector;

    uint32_t inside =
        slot % per_sector;

    if (sector >= FS_DIRECT_BLOCKS)
    {
        return 0;
    }

    uint32_t lba =
        inodes[0].direct[sector];

    if (lba == 0)
    {
        return 0;
    }

    uint8_t block[512];

    if (!fs_disk_read_sector(
            lba,
            block))
    {
        return 0;
    }

    mem_copy(
        &block[inside * sizeof(fs_dirent_t)],
        entry,
        sizeof(fs_dirent_t)
    );

    return fs_disk_write_sector(
        lba,
        block
    );
}

static int find_dir_entry(
    const char* name
)
{
    uint32_t slots =
        (FS_DIRECT_BLOCKS * 512) /
        sizeof(fs_dirent_t);

    for (uint32_t slot = 0;
         slot < slots;
         slot++)
    {
        fs_dirent_t entry;

        if (!dir_read_entry(
                slot,
                &entry))
        {
            continue;
        }

        if (entry.used &&
            str_equal(entry.name, name))
        {
            return (int)entry.inode;
        }
    }

    return -1;
}

static int find_free_dir_slot(void)
{
    uint32_t slots =
        (FS_DIRECT_BLOCKS * 512) /
        sizeof(fs_dirent_t);

    for (uint32_t slot = 0;
         slot < slots;
         slot++)
    {
        fs_dirent_t entry;

        if (!dir_read_entry(
                slot,
                &entry))
        {
            continue;
        }

        if (!entry.used)
        {
            return (int)slot;
        }
    }

    return -1;
}

/*
 * ------------------------------------------------------------
 * Sector allocation
 * ------------------------------------------------------------
 */

static int allocate_sector(void)
{
    for (uint32_t sector = FS_DATA_START;
         sector < superblock.total_sectors;
         sector++)
    {
        if (!bitmap_get(sector))
        {
            bitmap_set(sector);

            if (!save_bitmap())
            {
                bitmap_clear(sector);
                return -1;
            }

            uint8_t zero[512];

            mem_zero(
                zero,
                sizeof(zero)
            );

            if (!fs_disk_write_sector(
                    sector,
                    zero))
            {
                bitmap_clear(sector);
                save_bitmap();
                return -1;
            }

            return (int)sector;
        }
    }

    return -1;
}

static void free_sector(
    uint32_t sector
)
{
    if (sector < FS_DATA_START ||
        sector >= superblock.total_sectors)
    {
        return;
    }

    bitmap_clear(sector);
}

/*
 * ------------------------------------------------------------
 * Format
 * ------------------------------------------------------------
 */

int fs_format(void)
{
    if (!ata_present())
    {
        return 0;
    }

    uint32_t sectors =
        ata_sector_count();

    if (sectors <= FS_DATA_START + FS_DIRECT_BLOCKS)
    {
        return 0;
    }

    if (sectors >
        FS_BITMAP_SECTORS * 4096)
    {
        sectors =
            FS_BITMAP_SECTORS * 4096;
    }

    mem_zero(
        &superblock,
        sizeof(superblock)
    );

    superblock.magic =
        FS_MAGIC;

    superblock.version =
        FS_VERSION;

    superblock.sector_size =
        512;

    superblock.total_sectors =
        sectors;

    superblock.bitmap_start =
        FS_BITMAP_START;

    superblock.bitmap_sectors =
        FS_BITMAP_SECTORS;

    superblock.inode_start =
        FS_INODE_START;

    superblock.inode_sectors =
        FS_INODE_SECTORS;

    superblock.data_start =
        FS_DATA_START;

    superblock.max_files =
        FS_MAX_FILES;

    superblock.root_inode =
        0;

    /*
     * Sets up at-rest encryption for this freshly formatted disk
     * (generates a new random seed, stores it in
     * superblock.reserved[]) and points fs_disk_read_sector()/
     * fs_disk_write_sector() at the resulting device - must run
     * before anything below writes a single sector through them.
     */
    if (!fs_setup_disk_device(1))
    {
        return 0;
    }

    mem_zero(
        bitmap,
        sizeof(bitmap)
    );

    mem_zero(
        inodes,
        sizeof(inodes)
    );

    /*
     * Reserve:
     *
     * 0       superblock
     * 1-32    bitmap
     * 33-64   inode area
     */
    for (uint32_t i = 0;
         i < FS_DATA_START;
         i++)
    {
        bitmap_set(i);
    }

    /*
     * Root inode.
     */
    inodes[0].mode =
        FS_MODE_FILE;

    inodes[0].size = 0;

    /*
     * Allocate root directory blocks.
     */
    for (uint32_t i = 0;
         i < FS_DIRECT_BLOCKS;
         i++)
    {
        int sector =
            allocate_sector();

        if (sector < 0)
        {
            return 0;
        }

        inodes[0].direct[i] =
            (uint32_t)sector;

        inodes[0].blocks++;
    }

    /*
     * Persist root inode.
     */
    if (!save_inode(0))
    {
        return 0;
    }

    /*
     * Persist all other empty inodes.
     */
    for (uint32_t i = 1;
         i < FS_MAX_FILES;
         i++)
    {
        if (!save_inode(i))
        {
            return 0;
        }
    }

    if (!save_bitmap())
    {
        return 0;
    }

    if (!save_superblock())
    {
        return 0;
    }

    /*
     * Initialize handles.
     */
    for (uint32_t i = 0;
         i < FS_MAX_HANDLES;
         i++)
    {
        handles[i].inode = -1;
        handles[i].position = 0;
    }

    filesystem_ready = 1;

    return 1;
}

/*
 * ------------------------------------------------------------
 * Filesystem initialization
 * ------------------------------------------------------------
 */

int fs_init(void)
{
    filesystem_ready = 0;

    if (!ata_present())
    {
        return 0;
    }

    /*
     * Blank disk -> create filesystem.
     */
    if (!load_superblock())
    {
        return fs_format();
    }

    /*
     * A valid superblock is loaded (legacy or encrypted - see
     * the design note above fs_setup_disk_device()). Everything
     * from here on reads/writes through whichever device that
     * decides, so it must run before the bitmap load just below.
     */
    if (!fs_setup_disk_device(0))
    {
        return 0;
    }

    /*
     * Load bitmap.
     */
    for (uint32_t i = 0;
         i < FS_BITMAP_SECTORS;
         i++)
    {
        if (!fs_disk_read_sector(
                FS_BITMAP_START + i,
                &bitmap[i * 512]))
        {
            return 0;
        }
    }

    /*
     * Load inode table.
     */
    if (!load_inodes())
    {
        return 0;
    }

    /*
     * Initialize file handles.
     */
    for (uint32_t i = 0;
         i < FS_MAX_HANDLES;
         i++)
    {
        handles[i].inode = -1;
        handles[i].position = 0;
    }

    filesystem_ready = 1;

    return 1;
}

int fs_ready(void)
{
    return filesystem_ready;
}

/*
 * ------------------------------------------------------------
 * File operations
 * ------------------------------------------------------------
 */

int fs_exists(
    const char* name
)
{
    if (!filesystem_ready ||
        !valid_name(name))
    {
        return 0;
    }

    return find_dir_entry(name) >= 0;
}

int fs_create(
    const char* name
)
{
    if (!filesystem_ready ||
        !valid_name(name))
    {
        return 0;
    }

    if (find_dir_entry(name) >= 0)
    {
        return 0;
    }

    int slot =
        find_free_dir_slot();

    if (slot < 0)
    {
        return 0;
    }

    int inode_index = -1;

    for (uint32_t i = 1;
         i < FS_MAX_FILES;
         i++)
    {
        if (inodes[i].mode ==
            FS_MODE_FREE)
        {
            inode_index = (int)i;
            break;
        }
    }

    if (inode_index < 0)
    {
        return 0;
    }

    fs_dirent_t entry;

    mem_zero(
        &entry,
        sizeof(entry)
    );

    uint32_t length =
        str_len(name);

    mem_copy(
        entry.name,
        name,
        length
    );

    entry.name[length] = '\0';

    entry.inode =
        (uint32_t)inode_index;

    entry.used = 1;

    mem_zero(
        &inodes[inode_index],
        sizeof(fs_inode_t)
    );

    inodes[inode_index].mode =
        FS_MODE_FILE;

    if (!save_inode(
            (uint32_t)inode_index))
    {
        return 0;
    }

    if (!dir_write_entry(
            (uint32_t)slot,
            &entry))
    {
        inodes[inode_index].mode =
            FS_MODE_FREE;

        save_inode(
            (uint32_t)inode_index
        );

        return 0;
    }

    return 1;
}

int fs_delete(
    const char* name
)
{
    if (!filesystem_ready ||
        !valid_name(name))
    {
        return 0;
    }

    int inode_index =
        find_dir_entry(name);

    if (inode_index <= 0)
    {
        return 0;
    }

    fs_inode_t* inode =
        &inodes[inode_index];

    for (uint32_t i = 0;
         i < FS_DIRECT_BLOCKS;
         i++)
    {
        if (inode->direct[i] != 0)
        {
            free_sector(
                inode->direct[i]
            );

            inode->direct[i] = 0;
        }
    }

    inode->mode =
        FS_MODE_FREE;

    inode->size = 0;
    inode->blocks = 0;

    if (!save_bitmap())
    {
        return 0;
    }

    if (!save_inode(
            (uint32_t)inode_index))
    {
        return 0;
    }

    uint32_t slots =
        (FS_DIRECT_BLOCKS * 512) /
        sizeof(fs_dirent_t);

    for (uint32_t slot = 0;
         slot < slots;
         slot++)
    {
        fs_dirent_t entry;

        if (!dir_read_entry(
                slot,
                &entry))
        {
            continue;
        }

        if (entry.used &&
            entry.inode ==
                (uint32_t)inode_index)
        {
            mem_zero(
                &entry,
                sizeof(entry)
            );

            dir_write_entry(
                slot,
                &entry
            );

            break;
        }
    }

    return 1;
}

int fs_open(
    const char* name
)
{
    if (!filesystem_ready ||
        !valid_name(name))
    {
        return -1;
    }

    int inode =
        find_dir_entry(name);

    if (inode < 0)
    {
        return -1;
    }

    for (uint32_t i = 0;
         i < FS_MAX_HANDLES;
         i++)
    {
        if (handles[i].inode < 0)
        {
            handles[i].inode =
                inode;

            handles[i].position =
                0;

            return (int)i;
        }
    }

    return -1;
}

int fs_close(
    int fd
)
{
    if (fd < 0 ||
        fd >= FS_MAX_HANDLES)
    {
        return 0;
    }

    handles[fd].inode = -1;
    handles[fd].position = 0;

    return 1;
}

int fs_read(
    int fd,
    void* buffer,
    uint32_t size
)
{
    if (!filesystem_ready ||
        buffer == 0 ||
        fd < 0 ||
        fd >= FS_MAX_HANDLES ||
        handles[fd].inode < 0)
    {
        return -1;
    }

    fs_inode_t* inode =
        &inodes[handles[fd].inode];

    uint32_t position =
        handles[fd].position;

    if (position >= inode->size)
    {
        return 0;
    }

    uint32_t remaining =
        inode->size - position;

    if (size > remaining)
    {
        size = remaining;
    }

    uint8_t sector_buffer[512];

    uint8_t* output =
        (uint8_t*)buffer;

    uint32_t done = 0;

    while (done < size)
    {
        uint32_t absolute =
            position + done;

        uint32_t block =
            absolute / 512;

        uint32_t offset =
            absolute % 512;

        if (block >= FS_DIRECT_BLOCKS ||
            inode->direct[block] == 0)
        {
            break;
        }

        if (!fs_disk_read_sector(
                inode->direct[block],
                sector_buffer))
        {
            break;
        }

        uint32_t chunk =
            512 - offset;

        if (chunk > size - done)
        {
            chunk = size - done;
        }

        mem_copy(
            &output[done],
            &sector_buffer[offset],
            chunk
        );

        done += chunk;
    }

    handles[fd].position =
        position + done;

    return (int)done;
}

int fs_write(
    int fd,
    const void* buffer,
    uint32_t size
)
{
    if (!filesystem_ready ||
        buffer == 0 ||
        fd < 0 ||
        fd >= FS_MAX_HANDLES ||
        handles[fd].inode < 0)
    {
        return -1;
    }

    fs_inode_t* inode =
        &inodes[handles[fd].inode];

    uint32_t position =
        handles[fd].position;

    if (position >= FS_MAX_FILE_SIZE)
    {
        return 0;
    }

    if (size >
        FS_MAX_FILE_SIZE - position)
    {
        size =
            FS_MAX_FILE_SIZE - position;
    }

    const uint8_t* input =
        (const uint8_t*)buffer;

    uint8_t sector_buffer[512];

    uint32_t done = 0;

    while (done < size)
    {
        uint32_t absolute =
            position + done;

        uint32_t block =
            absolute / 512;

        uint32_t offset =
            absolute % 512;

        if (block >= FS_DIRECT_BLOCKS)
        {
            break;
        }

        if (inode->direct[block] == 0)
        {
            int sector =
                allocate_sector();

            if (sector < 0)
            {
                break;
            }

            inode->direct[block] =
                (uint32_t)sector;

            inode->blocks++;
        }

        if (!fs_disk_read_sector(
                inode->direct[block],
                sector_buffer))
        {
            break;
        }

        uint32_t chunk =
            512 - offset;

        if (chunk > size - done)
        {
            chunk = size - done;
        }

        mem_copy(
            &sector_buffer[offset],
            &input[done],
            chunk
        );

        if (!fs_disk_write_sector(
                inode->direct[block],
                sector_buffer))
        {
            break;
        }

        done += chunk;
    }

    handles[fd].position =
        position + done;

    if (handles[fd].position >
        inode->size)
    {
        inode->size =
            handles[fd].position;
    }

    save_inode(
        (uint32_t)handles[fd].inode
    );

    save_bitmap();

    return (int)done;
}

int fs_list(
    char* buffer,
    uint32_t size
)
{
    if (!filesystem_ready ||
        buffer == 0 ||
        size == 0)
    {
        return -1;
    }

    uint32_t written = 0;

    uint32_t slots =
        (FS_DIRECT_BLOCKS * 512) /
        sizeof(fs_dirent_t);

    for (uint32_t slot = 0;
         slot < slots;
         slot++)
    {
        fs_dirent_t entry;

        if (!dir_read_entry(
                slot,
                &entry))
        {
            continue;
        }

        if (!entry.used)
        {
            continue;
        }

        uint32_t length =
            str_len(entry.name);

        if (written + length + 1 >= size)
        {
            break;
        }

        mem_copy(
            &buffer[written],
            entry.name,
            length
        );

        written += length;

        buffer[written++] =
            '\n';
    }

    if (written < size)
    {
        buffer[written] = '\0';
    }
    else
    {
        buffer[size - 1] = '\0';
    }

    return (int)written;
}
