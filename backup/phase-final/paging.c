#include <stdint.h>

#include "paging.h"

/*
 * Page directory:
 *
 * 1024 entries × 4 bytes = 4096 bytes
 */
static uint32_t page_directory[1024]
    __attribute__((aligned(4096)));


/*
 * First page table:
 *
 * 1024 entries × 4 bytes = 4096 bytes
 *
 * Each entry represents one 4 KiB page.
 *
 * 1024 × 4096 = 4 MiB
 */
static uint32_t first_page_table[1024]
    __attribute__((aligned(4096)));


/*
 * Page flags.
 */
#define PAGE_PRESENT  0x001
#define PAGE_WRITE    0x002


void paging_init(void)
{
    /*
     * Clear the page directory.
     */
    for (uint32_t i = 0;
         i < 1024;
         i++)
    {
        page_directory[i] = 0;
    }


    /*
     * Create an identity mapping for
     * the first 4 MiB.
     *
     * Virtual address == physical address.
     */
    for (uint32_t i = 0;
         i < 1024;
         i++)
    {
        first_page_table[i] =
            (i * 4096)
            | PAGE_PRESENT
            | PAGE_WRITE;
    }


    /*
     * Page directory entry 0 points
     * to the first page table.
     */
    page_directory[0] =
        ((uint32_t)first_page_table)
        | PAGE_PRESENT
        | PAGE_WRITE;


    paging_enable();
}


void paging_enable(void)
{
    uint32_t directory =
        (uint32_t)page_directory;


    /*
     * Load CR3 with the page-directory
     * physical address.
     *
     * Identity mapping means the virtual
     * and physical address are currently
     * the same.
     */
    __asm__ volatile (
        "mov %0, %%cr3"
        :
        : "r"(directory)
        : "memory"
    );


    /*
     * Enable paging in CR0.
     */
    uint32_t cr0;

    __asm__ volatile (
        "mov %%cr0, %0"
        : "=r"(cr0)
    );


    cr0 |= 0x80000000;


    __asm__ volatile (
        "mov %0, %%cr0"
        :
        : "r"(cr0)
        : "memory"
    );
}


uint32_t paging_get_directory(void)
{
    return (uint32_t)page_directory;
}
