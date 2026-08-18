#include <stdint.h>
#include <shell.h>
#include "pic.h"
#include "keyboard.h"
#include "gdt.h"
#include "idt.h"
#include "syscall.h"
#include "shell.h"
#include "terminal.h"

static void syscall_test(void)
{
    const char* message =
        "Hello from a SpectreOS system call!";

    terminal_write(
        "Before syscall\n"
    );

    spectre_write(message);

    terminal_write(
        "\nAfter syscall\n"
    );
}

void kernel_main(
    uint32_t multiboot_magic,
    uint32_t multiboot_info
)
{
    (void)multiboot_info;

    terminal_clear();
    shell_init();

    terminal_write(
        "SpectreOS kernel starting...\n"
    );

    if (multiboot_magic != 0x2BADB002)
    {
        terminal_write(
            "ERROR: Invalid Multiboot magic.\n"
        );

        while (1)
        {
            __asm__ volatile (
                "cli; hlt"
            );
        }
    }

    terminal_write(
        "Multiboot: OK\n"
    );

    /*
     * Initialize our own GDT.
     */
    gdt_init();

    terminal_write(
        "GDT: OK\n"
    );

    /*
     * Initialize the Interrupt Descriptor Table.
     */
    idt_init();

    terminal_write(
        "IDT: OK\n"
    );

    pic_remap();

/*
 * Initialize the keyboard.
 */
keyboard_init();

/*
 * Enable IRQ1 (keyboard).
 */
pic_unmask_irq(1);

/*
 * Enable CPU hardware interrupts.
 */
__asm__ volatile ("sti");

terminal_write(
    "Interrupts: OK\n"
);

    terminal_write(
        "System calls: OK\n"
    );

    shell_init();

    while (1)
    {
        __asm__ volatile ("hlt");
    }
}
