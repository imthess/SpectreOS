
#include <stdint.h>

#include "keyboard.h"
#include "interrupts.h"
#include "pic.h"
#include "pit.h"
#include "scheduler.h"

static volatile uint64_t timer_ticks = 0;

/*
 * ============================================================
 * PANIC SCREEN
 *
 * Writes directly to VGA text memory rather than going through
 * terminal.c - by the time we get here something has already
 * gone wrong, so this stays dependency-free on purpose.
 * ============================================================
 */

#define PANIC_ATTR   0x4F00u   /* white on red */
#define PANIC_COLS   80

static const char* exception_names[32] =
{
    "Divide-by-zero error",
    "Debug",
    "Non-maskable interrupt",
    "Breakpoint",
    "Overflow",
    "Bound range exceeded",
    "Invalid opcode",
    "Device not available",
    "Double fault",
    "Coprocessor segment overrun",
    "Invalid TSS",
    "Segment not present",
    "Stack-segment fault",
    "General protection fault",
    "Page fault",
    "Reserved",
    "x87 floating-point exception",
    "Alignment check",
    "Machine check",
    "SIMD floating-point exception",
    "Virtualization exception",
    "Control protection exception",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved",
    "Hypervisor injection exception",
    "VMM communication exception",
    "Security exception"
};

static void panic_put_string(
    uint32_t row,
    uint32_t col,
    const char* text
)
{
    volatile uint16_t* video =
        (volatile uint16_t*)0xB8000;

    uint32_t position = row * PANIC_COLS + col;

    for (uint32_t i = 0; text[i] != '\0'; i++)
    {
        video[position + i] =
            PANIC_ATTR | (uint8_t)text[i];
    }
}

static void panic_put_hex32(
    uint32_t row,
    uint32_t col,
    uint32_t value
)
{
    static const char digits[] = "0123456789ABCDEF";

    volatile uint16_t* video =
        (volatile uint16_t*)0xB8000;

    uint32_t position = row * PANIC_COLS + col;

    video[position + 0] = PANIC_ATTR | '0';
    video[position + 1] = PANIC_ATTR | 'x';

    for (uint32_t i = 0; i < 8; i++)
    {
        uint32_t shift = (7 - i) * 4;

        video[position + 2 + i] =
            PANIC_ATTR |
            (uint8_t)digits[(value >> shift) & 0xF];
    }
}

static inline uint32_t read_cr2(void)
{
    uint32_t value;

    __asm__ volatile (
        "mov %%cr2, %0"
        : "=r"(value)
    );

    return value;
}

void exception_handler(registers_t* regs)
{
    __asm__ volatile ("cli");

    /*
     * Clear the screen to the panic color first so nothing
     * from the previous shell contents bleeds through.
     */
    volatile uint16_t* video =
        (volatile uint16_t*)0xB8000;

    for (uint32_t i = 0; i < 80 * 25; i++)
    {
        video[i] = PANIC_ATTR | ' ';
    }

    panic_put_string(
        0, 0,
        "*** SPECTREOS KERNEL PANIC ***"
    );

    const char* name =
        (regs->int_no < 32) ?
        exception_names[regs->int_no] :
        "Unknown exception";

    panic_put_string(2, 0, "Exception : ");
    panic_put_string(2, 12, name);

    panic_put_string(3, 0, "Vector    : ");
    panic_put_hex32(3, 12, regs->int_no);

    panic_put_string(4, 0, "Error code: ");
    panic_put_hex32(4, 12, regs->err_code);

    /*
     * Page faults are far more useful with the faulting
     * address, which the CPU leaves in CR2.
     */
    if (regs->int_no == 14)
    {
        panic_put_string(5, 0, "Fault addr: ");
        panic_put_hex32(5, 12, read_cr2());
    }

    panic_put_string(7, 0, "EIP=");
    panic_put_hex32(7, 4, regs->eip);
    panic_put_string(7, 20, "CS=");
    panic_put_hex32(7, 23, regs->cs);
    panic_put_string(7, 39, "EFLAGS=");
    panic_put_hex32(7, 46, regs->eflags);

    panic_put_string(8, 0, "EAX=");
    panic_put_hex32(8, 4, regs->eax);
    panic_put_string(8, 20, "EBX=");
    panic_put_hex32(8, 24, regs->ebx);
    panic_put_string(8, 40, "ECX=");
    panic_put_hex32(8, 44, regs->ecx);
    panic_put_string(8, 60, "EDX=");
    panic_put_hex32(8, 64, regs->edx);

    panic_put_string(9, 0, "ESI=");
    panic_put_hex32(9, 4, regs->esi);
    panic_put_string(9, 20, "EDI=");
    panic_put_hex32(9, 24, regs->edi);
    panic_put_string(9, 40, "EBP=");
    panic_put_hex32(9, 44, regs->ebp);
    panic_put_string(9, 60, "ESP=");
    panic_put_hex32(9, 64, regs->esp);

    panic_put_string(
        11, 0,
        "System halted."
    );

    while (1)
    {
        __asm__ volatile ("hlt");
    }
}

uint32_t irq_handler(registers_t* regs)
{
    uint32_t irq =
        regs->int_no - 32;

    uint32_t new_esp =
        (uint32_t)regs;

    switch (irq)
    {
        case 0:

            timer_ticks++;

            pit_tick();

            new_esp =
                scheduler_tick(
                    (uint32_t)regs
                );

            break;

        case 1:

            keyboard_handler();

            break;

        default:

            break;
    }

    pic_send_eoi(
        (uint8_t)irq
    );

    return new_esp;
}

