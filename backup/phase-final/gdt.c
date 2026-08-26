#include <stdint.h>
#include "gdt.h"

struct gdt_entry
{
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_middle;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed));

struct gdt_ptr
{
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

static struct gdt_entry gdt[5];
static struct gdt_ptr gdt_pointer;

static void gdt_set_gate(
    int index,
    uint32_t base,
    uint32_t limit,
    uint8_t access,
    uint8_t granularity
)
{
    gdt[index].base_low =
        (uint16_t)(base & 0xFFFF);

    gdt[index].base_middle =
        (uint8_t)((base >> 16) & 0xFF);

    gdt[index].base_high =
        (uint8_t)((base >> 24) & 0xFF);

    gdt[index].limit_low =
        (uint16_t)(limit & 0xFFFF);

    gdt[index].granularity =
        (uint8_t)((limit >> 16) & 0x0F);

    gdt[index].granularity |=
        granularity & 0xF0;

    gdt[index].access = access;
}

extern void gdt_flush(uint32_t);

void gdt_init(void)
{
    gdt_pointer.limit =
        sizeof(gdt) - 1;

    gdt_pointer.base =
        (uint32_t)&gdt;

    /*
     * Null descriptor
     */
    gdt_set_gate(
        0,
        0,
        0,
        0,
        0
    );

    /*
     * Kernel code segment
     *
     * Access:
     * 0x9A = present + ring 0 + executable + readable
     */
    gdt_set_gate(
        1,
        0,
        0xFFFFFFFF,
        0x9A,
        0xCF
    );

    /*
     * Kernel data segment
     *
     * Access:
     * 0x92 = present + ring 0 + writable
     */
    gdt_set_gate(
        2,
        0,
        0xFFFFFFFF,
        0x92,
        0xCF
    );

    /*
     * User code segment
     *
     * Access:
     * 0xFA = present + ring 3 + executable + readable
     */
    gdt_set_gate(
        3,
        0,
        0xFFFFFFFF,
        0xFA,
        0xCF
    );

    /*
     * User data segment
     *
     * Access:
     * 0xF2 = present + ring 3 + writable
     */
    gdt_set_gate(
        4,
        0,
        0xFFFFFFFF,
        0xF2,
        0xCF
    );

    gdt_flush((uint32_t)&gdt_pointer);
}
