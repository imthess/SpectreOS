#include <stdint.h>
#include "pic.h"
#include "keyboard.h"
#include "gdt.h"
#include "idt.h"
#include "pmm.h"
#include "syscall.h"
#include "shell.h"
#include "terminal.h"
#include "paging.h"
#include "memory.h"
#include "thread.h"
#include "pit.h"


static void test_thread(void)
{
    while (1)
    {
        /*
         * This thread will eventually run
         * under the scheduler.
         */
        __asm__ volatile ("hlt");
    }
}

void kernel_main(
    uint32_t multiboot_magic,
    uint32_t multiboot_info
)
{
    (void)multiboot_info;

    terminal_clear();

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
     * Initialize memory information from
     * the actual Multiboot structure.
     */
    memory_init(multiboot_info);

    terminal_write(
        "Memory detection: OK\n"
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
 * Initialize PIT at 100 Hz.
 *
 * This gives approximately one timer interrupt
 * every 10 milliseconds.
 */
pit_init(100);

    /*
 * Initialize physical memory manager.
 */
pmm_init(multiboot_info);

/*
 * Initialize virtual memory.
 */
paging_init();

terminal_write(
    "Paging: OK\n"
);

terminal_write(
    "Memory manager: OK\n"
);

/*
 * Initialize the keyboard.
 */
keyboard_init();

/*
 * Enable IRQ1 (keyboard).
 */
pic_unmask_irq(0);
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

    
    thread_init();

terminal_write(
    "Threading: OK\n"
);

scheduler_init();

terminal_write(
    "Scheduler: OK\n"
);

int thread_id =
    thread_create(test_thread);

if (thread_id > 0)
{
    terminal_write(
        "Thread creation: OK\n"
    );
}
else
{
    terminal_write(
        "Thread creation: FAILED\n"
    );
}

    shell_init();

    while (1)
    {
        __asm__ volatile ("hlt");
    }
}
