#include <stdint.h>

#include "memory.h"

/*
 * Multiboot v1 information structure.
 *
 * We only use the first fields required
 * for basic memory detection.
 *
 * flags:
 *   bit 0 = memory information is valid
 *
 * mem_lower:
 *   lower memory in KB
 *
 * mem_upper:
 *   upper memory in KB
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

    uint32_t syms[4];

    uint32_t mmap_length;
    uint32_t mmap_addr;

} multiboot_info_t;


/*
 * Cached hardware memory information.
 */
static memory_info_t memory_info;


/*
 * Initialize the SpectreOS memory information
 * subsystem using the actual Multiboot data.
 */
void memory_init(uint32_t multiboot_info_address)
{
    multiboot_info_t* mbi =
        (multiboot_info_t*)multiboot_info_address;

    /*
     * Clear our structure.
     */
    memory_info.lower_memory_kb = 0;
    memory_info.upper_memory_kb = 0;
    memory_info.total_memory_kb = 0;
    memory_info.total_memory_mb = 0;
    memory_info.multiboot_memory_available = 0;

    if (mbi == 0)
    {
        return;
    }

    /*
     * Multiboot flag 0 tells us whether
     * mem_lower and mem_upper are valid.
     */
    if ((mbi->flags & 0x01) == 0)
    {
        return;
    }

    memory_info.lower_memory_kb =
        mbi->mem_lower;

    memory_info.upper_memory_kb =
        mbi->mem_upper;

    /*
     * mem_lower and mem_upper are both
     * expressed in KiB.
     *
     * mem_upper describes memory starting
     * at the 1 MiB boundary.
     *
     * Therefore total memory is:
     *
     * lower memory + upper memory + 1 MiB
     */
    memory_info.total_memory_kb =
        mbi->mem_lower +
        mbi->mem_upper +
        1024;

    memory_info.total_memory_mb =
        memory_info.total_memory_kb /
        1024;

    memory_info.multiboot_memory_available = 1;
}


/*
 * Copy the detected memory information
 * into the caller's structure.
 */
uint32_t memory_get_info(
    memory_info_t* info
)
{
    if (info == 0)
    {
        return (uint32_t)-1;
    }

    *info = memory_info;

    return 0;
}
