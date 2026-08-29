#include <stdint.h>

#include "hardware.h"
#include "memory.h"
#include "gdt.h"
#include "idt.h"
#include "pic.h"
#include "pit.h"
#include "pmm.h"
#include "paging.h"
#include "keyboard.h"
#include "ata.h"
#include "fs.h"
#include "thread.h"
#include "scheduler.h"
#include "syscall.h"
#include "shell.h"
#include "terminal.h"

static void halt_forever(void)
{
    __asm__ volatile ("cli");

    while (1)
    {
        __asm__ volatile ("hlt");
    }
}

void kernel_main(
    uint32_t multiboot_magic,
    uint32_t multiboot_info
)
{
    /*
     * No hardware IRQs while the kernel is being initialized.
     */
    __asm__ volatile ("cli");

    terminal_clear();

    hardware_init();

    terminal_write(
        "SpectreOS kernel starting...\n"
    );

    if (multiboot_magic != 0x2BADB002)
    {
        terminal_write(
            "ERROR: Invalid Multiboot magic.\n"
        );

        halt_forever();
    }

    terminal_write(
        "Multiboot: OK\n"
    );

    /*
     * --------------------------------------------------------
     * MEMORY
     * --------------------------------------------------------
     */
    memory_init(
        multiboot_info
    );

    terminal_write(
        "Memory detection: OK\n"
    );

    /*
     * --------------------------------------------------------
     * CPU / INTERRUPT FOUNDATION
     * --------------------------------------------------------
     */
    gdt_init();

    terminal_write(
        "GDT: OK\n"
    );

    idt_init();

    terminal_write(
        "IDT: OK\n"
    );

    pic_remap();

    pit_init(
        100
    );

    /*
     * --------------------------------------------------------
     * PHYSICAL + VIRTUAL MEMORY
     * --------------------------------------------------------
     */
    pmm_init(
        multiboot_info
    );

    paging_init();

    terminal_write(
        "Paging: OK\n"
    );

    terminal_write(
        "Memory manager: OK\n"
    );

    /*
     * --------------------------------------------------------
     * ATA STORAGE
     * --------------------------------------------------------
     *
     * Must happen before filesystem initialization.
     */
    if (!ata_init())
    {
        terminal_write(
            "ATA: FAILED\n"
        );

        halt_forever();
    }

    terminal_write(
        "ATA: OK\n"
    );

    /*
     * --------------------------------------------------------
     * FILESYSTEM
     * --------------------------------------------------------
     *
     * Loads the persistent filesystem from the ATA disk.
     * If the disk is blank, fs_init() creates the filesystem.
     */
    if (!fs_init())
    {
        terminal_write(
            "Filesystem: FAILED\n"
        );

        halt_forever();
    }

    terminal_write(
        "Filesystem: OK\n"
    );

    /*
     * --------------------------------------------------------
     * KEYBOARD
     * --------------------------------------------------------
     */
    keyboard_init();

    /*
     * --------------------------------------------------------
     * THREADING
     * --------------------------------------------------------
     *
     * Initialize the subsystem only.
     *
     * Do NOT create the previous experimental test threads
     * here. Their unfinished context-switch path was causing
     * the invalid EIP=0x00000003 exception.
     */
    thread_init();

    terminal_write(
        "Threading: OK\n"
    );

    /*
     * --------------------------------------------------------
     * SCHEDULER
     * --------------------------------------------------------
     *
     * Initialize scheduler state, but don't force an
     * experimental context switch from the PIT yet.
     */
    scheduler_init();

    terminal_write(
        "Scheduler: OK\n"
    );

    /*
     * --------------------------------------------------------
     * SYSTEM CALLS
     * --------------------------------------------------------
     *
     * IDT/syscall infrastructure has already been installed
     * by idt_init() and the syscall implementation is linked.
     */
    terminal_write(
        "Interrupts: OK\n"
    );

    terminal_write(
        "System calls: OK\n"
    );

    /*
     * --------------------------------------------------------
     * SHELL
     * --------------------------------------------------------
     *
     * Initialize the shell BEFORE enabling interrupts.
     */
    shell_init();

    /*
     * --------------------------------------------------------
     * INTERRUPTS
     * --------------------------------------------------------
     *
     * Keyboard and timer are enabled only after every
     * subsystem above is ready.
     */
    pic_unmask_irq(0);
    pic_unmask_irq(1);

    __asm__ volatile ("sti");

    /*
     * Bootstrap kernel remains active.
     *
     * PIT currently provides timer interrupts and the
     * keyboard provides shell input. The scheduler remains
     * initialized but no unsafe test context-switch is forced.
     */
    while (1)
    {
        __asm__ volatile ("hlt");
    }
}
