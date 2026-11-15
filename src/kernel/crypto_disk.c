#include <stdint.h>

#include "crypto_disk.h"
#include "blockdev.h"
#include "aes256.h"
#include "ata.h"
#include "rng.h"
#include "secure_wipe.h"

#define AES_BLOCKS_PER_SECTOR \
    (ATA_SECTOR_SIZE / AES256_BLOCK_SIZE)

typedef struct
{
    aes256_ctx_t aes;
    uint32_t backing_device_id;
    uint8_t nonce[8];
    int in_use;

} crypto_slot_t;

static crypto_slot_t slots[CRYPTO_DISK_MAX_SLOTS];

/*
 * Encrypts (or, identically, decrypts - CTR mode is symmetric)
 * one sector in place between `in` and `out` (which may be the
 * same buffer).
 */
static void crypto_xor_sector(
    const crypto_slot_t* slot,
    uint32_t lba,
    const uint8_t* in,
    uint8_t* out
)
{
    for (uint32_t block = 0;
         block < AES_BLOCKS_PER_SECTOR;
         block++)
    {
        uint64_t counter =
            (uint64_t)lba *
                AES_BLOCKS_PER_SECTOR +
            block;

        uint8_t counter_block[16];

        for (uint32_t i = 0; i < 8; i++)
        {
            counter_block[i] =
                slot->nonce[i];
        }

        for (uint32_t i = 0; i < 8; i++)
        {
            counter_block[8 + i] =
                (uint8_t)(counter >>
                    ((7 - i) * 8));
        }

        uint8_t keystream[16];

        aes256_encrypt_block(
            &slot->aes,
            counter_block,
            keystream
        );

        const uint8_t* src =
            &in[block * AES256_BLOCK_SIZE];

        uint8_t* dst =
            &out[block * AES256_BLOCK_SIZE];

        for (uint32_t i = 0;
             i < AES256_BLOCK_SIZE;
             i++)
        {
            dst[i] =
                src[i] ^ keystream[i];
        }
    }
}

/*
 * ------------------------------------------------------------
 * Slot 0 trampolines
 * ------------------------------------------------------------
 */

static int crypto0_present(void)
{
    if (!slots[0].in_use)
    {
        return 0;
    }

    return blockdev_present(
        slots[0].backing_device_id
    );
}

static uint32_t crypto0_sector_count(void)
{
    if (!slots[0].in_use)
    {
        return 0;
    }

    return blockdev_sector_count(
        slots[0].backing_device_id
    );
}

static int crypto0_read_sector(
    uint32_t lba,
    void* buffer
)
{
    if (!slots[0].in_use)
    {
        return 0;
    }

    uint8_t cipher[ATA_SECTOR_SIZE];

    if (!blockdev_read_sector(
            slots[0].backing_device_id,
            lba,
            cipher))
    {
        return 0;
    }

    crypto_xor_sector(
        &slots[0],
        lba,
        cipher,
        (uint8_t*)buffer
    );

    return 1;
}

static int crypto0_write_sector(
    uint32_t lba,
    const void* buffer
)
{
    if (!slots[0].in_use)
    {
        return 0;
    }

    uint8_t cipher[ATA_SECTOR_SIZE];

    crypto_xor_sector(
        &slots[0],
        lba,
        (const uint8_t*)buffer,
        cipher
    );

    return blockdev_write_sector(
        slots[0].backing_device_id,
        lba,
        cipher
    );
}

/*
 * ------------------------------------------------------------
 * Slot 1 trampolines (identical shape, distinct static state -
 * see the note in crypto_disk.h on why this isn't parameterized)
 * ------------------------------------------------------------
 */

static int crypto1_present(void)
{
    if (!slots[1].in_use)
    {
        return 0;
    }

    return blockdev_present(
        slots[1].backing_device_id
    );
}

static uint32_t crypto1_sector_count(void)
{
    if (!slots[1].in_use)
    {
        return 0;
    }

    return blockdev_sector_count(
        slots[1].backing_device_id
    );
}

static int crypto1_read_sector(
    uint32_t lba,
    void* buffer
)
{
    if (!slots[1].in_use)
    {
        return 0;
    }

    uint8_t cipher[ATA_SECTOR_SIZE];

    if (!blockdev_read_sector(
            slots[1].backing_device_id,
            lba,
            cipher))
    {
        return 0;
    }

    crypto_xor_sector(
        &slots[1],
        lba,
        cipher,
        (uint8_t*)buffer
    );

    return 1;
}

static int crypto1_write_sector(
    uint32_t lba,
    const void* buffer
)
{
    if (!slots[1].in_use)
    {
        return 0;
    }

    uint8_t cipher[ATA_SECTOR_SIZE];

    crypto_xor_sector(
        &slots[1],
        lba,
        (const uint8_t*)buffer,
        cipher
    );

    return blockdev_write_sector(
        slots[1].backing_device_id,
        lba,
        cipher
    );
}

/*
 * Shared implementation. `nonce` of 0 means "generate a fresh
 * random one" (crypto_disk_create(), used for throwaway disks like
 * the boot-time RAM-disk self-test, where nothing needs to survive
 * a reboot). A non-zero `nonce` is used as-is
 * (crypto_disk_create_with_nonce(), required for any disk whose
 * contents must still decrypt after a reboot: a randomly
 * regenerated nonce on the next boot would produce a completely
 * different keystream and turn everything already written into
 * garbage - which is exactly what happened the first time this was
 * tried against a persistent filesystem, and was caught by
 * simulating a reboot against a fake disk image before it ever
 * touched real data).
 */
static int crypto_disk_create_internal(
    const char* name,
    uint32_t backing_device_id,
    const uint8_t key[32],
    const uint8_t* nonce
)
{
    if (name == 0 || key == 0)
    {
        return -1;
    }

    int slot_index = -1;

    for (int i = 0; i < CRYPTO_DISK_MAX_SLOTS; i++)
    {
        if (!slots[i].in_use)
        {
            slot_index = i;
            break;
        }
    }

    if (slot_index < 0)
    {
        return -1;
    }

    crypto_slot_t* slot =
        &slots[slot_index];

    aes256_init(&slot->aes, key);

    slot->backing_device_id =
        backing_device_id;

    if (nonce != 0)
    {
        for (uint32_t i = 0; i < sizeof(slot->nonce); i++)
        {
            slot->nonce[i] = nonce[i];
        }
    }
    else
    {
        rng_get_bytes(
            slot->nonce,
            sizeof(slot->nonce)
        );
    }

    slot->in_use = 1;

    if (slot_index == 0)
    {
        return blockdev_register(
            name,
            crypto0_present,
            crypto0_sector_count,
            crypto0_read_sector,
            crypto0_write_sector
        );
    }
    else
    {
        return blockdev_register(
            name,
            crypto1_present,
            crypto1_sector_count,
            crypto1_read_sector,
            crypto1_write_sector
        );
    }
}

int crypto_disk_create(
    const char* name,
    uint32_t backing_device_id,
    const uint8_t key[32]
)
{
    return crypto_disk_create_internal(
        name,
        backing_device_id,
        key,
        0
    );
}

int crypto_disk_create_with_nonce(
    const char* name,
    uint32_t backing_device_id,
    const uint8_t key[32],
    const uint8_t nonce[8]
)
{
    if (nonce == 0)
    {
        return -1;
    }

    return crypto_disk_create_internal(
        name,
        backing_device_id,
        key,
        nonce
    );
}

int crypto_disk_self_test(
    uint32_t crypto_device_id,
    uint32_t backing_device_id,
    uint32_t scratch_lba
)
{
    uint8_t original[ATA_SECTOR_SIZE];
    uint8_t pattern[ATA_SECTOR_SIZE];
    uint8_t readback[ATA_SECTOR_SIZE];
    uint8_t raw_on_disk[ATA_SECTOR_SIZE];

    int ok = 1;

    /*
     * Preserve whatever was already there.
     */
    if (!blockdev_read_sector(
            backing_device_id,
            scratch_lba,
            original))
    {
        return 0;
    }

    for (uint32_t i = 0; i < ATA_SECTOR_SIZE; i++)
    {
        pattern[i] =
            (uint8_t)(i * 37 + 11);
    }

    if (!blockdev_write_sector(
            crypto_device_id,
            scratch_lba,
            pattern))
    {
        ok = 0;
    }

    if (ok &&
        !blockdev_read_sector(
            crypto_device_id,
            scratch_lba,
            readback))
    {
        ok = 0;
    }

    if (ok)
    {
        for (uint32_t i = 0; i < ATA_SECTOR_SIZE; i++)
        {
            if (readback[i] != pattern[i])
            {
                ok = 0;
                break;
            }
        }
    }

    /*
     * Confirm the bytes actually stored on the backing device
     * are NOT the plaintext pattern - i.e. that encryption is
     * really happening, not a pass-through.
     */
    if (ok &&
        !blockdev_read_sector(
            backing_device_id,
            scratch_lba,
            raw_on_disk))
    {
        ok = 0;
    }

    if (ok)
    {
        int differs = 0;

        for (uint32_t i = 0; i < ATA_SECTOR_SIZE; i++)
        {
            if (raw_on_disk[i] != pattern[i])
            {
                differs = 1;
                break;
            }
        }

        if (!differs)
        {
            ok = 0;
        }
    }

    /*
     * Restore the original contents either way.
     */
    blockdev_write_sector(
        backing_device_id,
        scratch_lba,
        original
    );

    secure_wipe(pattern, ATA_SECTOR_SIZE);
    secure_wipe(readback, ATA_SECTOR_SIZE);

    return ok;
}
