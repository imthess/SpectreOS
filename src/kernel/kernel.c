
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
#include "pit.h"
#include "memory.h"
#include "thread.h"
#include "scheduler.h"


static volatile uint32_t thread_a_counter = 0;
static volatile uint32_t thread_b_counter = 0;


static void test_thread_a(void)
{
    while (1)
    {
        thread_a_counter++;

        for (volatile uint32_t i = 0;
             i < 100000;
             i++)
        {
        }
    }
}


static void test_thread_b(void)
{
    while (1)
    {
        thread_b_counter++;

        for (volatile uint32_t i = 0;
             i < 100000;
             i++)
        {
        }
    }
}


static void scheduler_status(void)
{
    uint32_t ticks =
        pit_get_ticks();

    uint32_t seconds =
        ticks / 100;

    uint32_t milliseconds =
        (ticks % 100) * 10;

    terminal_write(
        "\nScheduler status:\n"
    );

    terminal_write(
        "  PIT ticks: "
    );

    terminal_write_uint(
        ticks
    );

    terminal_write(
        "\n  Time: "
    );

    terminal_write_uint(
        seconds
    );

    terminal_write(
        "."
    );

    if (milliseconds < 10)
    {
        terminal_write("0");
    }

    terminal_write_uint(
        milliseconds
    );

    terminal_write(
        " seconds\n"
    );
}

void kernel_main(
    uint32_t multiboot_magic,
    uint32_t multiboot_info
)
{
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


    memory_init(
        multiboot_info
    );

    terminal_write(
        "Memory detection: OK\n"
    );


    gdt_init();

    terminal_write(
        "GDT: OK\n"
    );


    idt_init();

    terminal_write(
        "IDT: OK\n"
    );


    pic_remap();


    pit_init(100);

    pic_unmask_irq(0);


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


    keyboard_init();

    pic_unmask_irq(1);


    thread_init();

    terminal_write(
        "Threading: OK\n"
    );


    scheduler_init();

    terminal_write(
        "Scheduler: OK\n"
    );


    int thread_a =
        thread_create(
            test_thread_a
        );

    int thread_b =
        thread_create(
            test_thread_b
        );


    if (
        thread_a > 0 &&
        thread_b > 0
    )
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


    __asm__ volatile ("sti");


    terminal_write(
        "Interrupts: OK\n"
    );

    terminal_write(
        "System calls: OK\n"
    );

    terminal_write(
        "Round-Robin scheduler: ACTIVE\n"
    );


    shell_init();


    while (1)
    {
        scheduler_status();

        __asm__ volatile ("hlt");
    }
}

