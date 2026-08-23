#include <stdint.h>

#include "pmm.h"

/*
 * Multiboot 0 memory information.
 */
typedef struct
{
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint8_t  syms[16];
    uint32_t mmap_length;
    uint32_t mmap_addr;
} multiboot_info_t;


/*
 * Multiboot memory-map entry.
 */
typedef struct
{
    uint32_t size;

    uint32_t addr_low;
    uint32_t addr_high;

    uint32_t length_low;
    uint32_t length_high;

    uint32_t type;
} multiboot_mmap_entry_t;


/*
 * One bit represents one 4 KiB frame.
 *
 * 0 = free
 * 1 = used
 */
static uint32_t frame_bitmap[
    PMM_MAX_FRAMES / 32
];

static uint32_t total_frames;
static uint32_t used_frames;


/*
 * Mark frame as used.
 */
static void frame_set(uint32_t frame)
{
    frame_bitmap[frame / 32] |=
        (1U << (frame % 32));
}


/*
 * Mark frame as free.
 */
static void frame_clear(uint32_t frame)
{
    frame_bitmap[frame / 32] &=
        ~(1U << (frame % 32));
}


/*
 * Check frame status.
 */
static int frame_test(uint32_t frame)
{
    return (
        frame_bitmap[frame / 32] &
        (1U << (frame % 32))
    ) != 0;
}


/*
 * Reserve a physical address range.
 *
 * The range is rounded to complete 4 KiB frames.
 */
static void reserve_range(
    uint32_t start,
    uint32_t length
)
{
    if (length == 0)
    {
        return;
    }

    uint32_t end =
        start + length;

    /*
     * Prevent overflow.
     */
    if (end < start)
    {
        end = PMM_MAX_MEMORY;
    }

    if (start >= PMM_MAX_MEMORY)
    {
        return;
    }

    if (end > PMM_MAX_MEMORY)
    {
        end = PMM_MAX_MEMORY;
    }

    uint32_t first_frame =
        start / PMM_FRAME_SIZE;

    uint32_t last_frame =
        (end + PMM_FRAME_SIZE - 1) /
        PMM_FRAME_SIZE;

    if (last_frame > PMM_MAX_FRAMES)
    {
        last_frame = PMM_MAX_FRAMES;
    }

    for (uint32_t frame = first_frame;
         frame < last_frame;
         frame++)
    {
        if (!frame_test(frame))
        {
            frame_set(frame);
            used_frames++;
        }
    }
}


/*
 * Mark an available Multiboot memory region as free.
 */
static void release_range(
    uint32_t start,
    uint32_t length
)
{
    if (length == 0)
    {
        return;
    }

    if (start >= PMM_MAX_MEMORY)
    {
        return;
    }

    uint32_t end =
        start + length;

    if (end < start)
    {
        end = PMM_MAX_MEMORY;
    }

    if (end > PMM_MAX_MEMORY)
    {
        end = PMM_MAX_MEMORY;
    }

    uint32_t first_frame =
        (start + PMM_FRAME_SIZE - 1) /
        PMM_FRAME_SIZE;

    uint32_t last_frame =
        end / PMM_FRAME_SIZE;

    if (last_frame > PMM_MAX_FRAMES)
    {
        last_frame = PMM_MAX_FRAMES;
    }

    for (uint32_t frame = first_frame;
         frame < last_frame;
         frame++)
    {
        if (frame_test(frame))
        {
            frame_clear(frame);

            if (used_frames > 0)
            {
                used_frames--;
            }
        }
    }
}


void pmm_init(uint32_t multiboot_info)
{
    /*
     * Initially mark every frame as used.
     *
     * This is safer than assuming RAM is available.
     */
    for (uint32_t i = 0;
         i < PMM_MAX_FRAMES / 32;
         i++)
    {
        frame_bitmap[i] = 0xFFFFFFFF;
    }

    total_frames =
        PMM_MAX_FRAMES;

    used_frames =
        PMM_MAX_FRAMES;


    /*
     * Release memory according to the Multiboot
     * memory map.
     */
    multiboot_info_t* info =
        (multiboot_info_t*)multiboot_info;

    /*
     * Bit 6 of flags indicates that the
     * memory-map fields are valid.
     */
    if (!(info->flags & (1 << 6)))
    {
        /*
         * No memory map.
         *
         * Keep everything reserved.
         */
        return;
    }


    uint32_t current =
        info->mmap_addr;

    uint32_t end =
        info->mmap_addr +
        info->mmap_length;


    while (current < end)
    {
        multiboot_mmap_entry_t*
            entry =
            (multiboot_mmap_entry_t*)current;


        /*
         * We only use entries whose memory type
         * is 1 = available RAM.
         */
        if (entry->type == 1)
        {
            /*
             * This implementation tracks physical
             * addresses below 128 MiB.
             *
             * Multiboot's 64-bit address is represented
             * by low/high fields.
             */
            if (entry->addr_high == 0)
            {
                release_range(
                    entry->addr_low,
                    entry->length_low
                );
            }
        }


        current +=
            entry->size +
            sizeof(entry->size);
    }


    /*
     * Reserve the first 1 MiB.
     *
     * This prevents allocation of BIOS,
     * bootloader and low-memory hardware areas.
     */
    reserve_range(
        0,
        1024 * 1024
    );
}


uint32_t pmm_alloc_frame(void)
{
    for (uint32_t frame = 0;
         frame < total_frames;
         frame++)
    {
        if (!frame_test(frame))
        {
            frame_set(frame);

            used_frames++;

            return frame *
                   PMM_FRAME_SIZE;
        }
    }

    return 0xFFFFFFFF;
}


void pmm_free_frame(uint32_t address)
{
    if (address == 0xFFFFFFFF)
    {
        return;
    }

    if (address % PMM_FRAME_SIZE != 0)
    {
        return;
    }

    uint32_t frame =
        address / PMM_FRAME_SIZE;

    if (frame >= total_frames)
    {
        return;
    }

    /*
     * First 1 MiB is permanently reserved.
     */
    if (frame <
        (1024 * 1024 / PMM_FRAME_SIZE))
    {
        return;
    }

    if (frame_test(frame))
    {
        frame_clear(frame);

        if (used_frames > 0)
        {
            used_frames--;
        }
    }
}


uint32_t pmm_get_total_frames(void)
{
    return total_frames;
}


uint32_t pmm_get_used_frames(void)
{
    return used_frames;
}


uint32_t pmm_get_free_frames(void)
{
    return total_frames - used_frames;
}
