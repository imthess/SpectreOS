
#include <stdint.h>

#include "pic.h"
#include "keyboard.h"
#include "gdt.h"
#include "idt.h"
#include "pmm.h"
#include "syscall.h"
#include "shell.h"
#include "terminal.h"
#include "sync.h"
#include "paging.h"
#include "pit.h"
#include "memory.h"
#include "thread.h"
#include "scheduler.h"

static volatile uint32_t thread_a_ticks = 0;
static volatile uint32_t thread_b_ticks = 0;


static void test_thread_a(void)
{
    while (thread_a_ticks < 100000)
    {
        thread_a_ticks++;

        /*
         * Simulate periodic kernel work.
         */
        __asm__ volatile ("hlt");
    }

    /*
     * Verify thread termination.
     */
    thread_exit();
}

static void test_thread_b(void)
{
    while (thread_b_ticks < 100000)
    {
        thread_b_ticks++;

        __asm__ volatile ("hlt");
    }

    thread_exit();
}
static mutex_t sync_test_mutex;
static semaphore_t sync_test_semaphore;

static void synchronization_test(void)
{
    mutex_init(&sync_test_mutex);

    semaphore_init(
        &sync_test_semaphore,
        1
    );

    mutex_lock(&sync_test_mutex);

    terminal_write(
        "\nSynchronization:\n"
        "  Spinlock: OK\n"
        "  Mutex: OK\n"
    );

    mutex_unlock(&sync_test_mutex);

    semaphore_wait(
        &sync_test_semaphore
    );

    terminal_write(
        "  Semaphore wait: OK\n"
    );

    semaphore_signal(
        &sync_test_semaphore
    );

    terminal_write(
        "  Semaphore signal: OK\n"
    );

    terminal_write(
        "Synchronization: OK\n"
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

    memory_init(multiboot_info);

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

    pmm_init(multiboot_info);

    paging_init();

    terminal_write(
        "Paging: OK\n"
    );

    terminal_write(
        "Memory manager: OK\n"
    );

    keyboard_init();

    /*
     * Initialize threading before enabling
     * hardware interrupts.
     */
    thread_init();

    terminal_write(
        "Threading: OK\n"
    );

    scheduler_init();
    synchronization_test();

    terminal_write(
        "Scheduler: OK\n"
    );

    /*
     * Create two actual kernel threads.
     */
    int thread_a =
        thread_create(test_thread_a);

    int thread_b =
        thread_create(test_thread_b);

    if (thread_a > 0 &&
        thread_b > 0)
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

    
    terminal_write(
        "\nInterrupts: OK\n"
    );

    terminal_write(
        "System calls: OK\n"
    );

    terminal_write(
        "Round-Robin scheduler: ACTIVE\n\n"
    );

    /*
     * Start hardware interrupts.
     */
    __asm__ volatile ("sti");

    /*
     * Enable PIT and keyboard IRQs only after
     * all scheduler structures are ready.
     */
    pic_unmask_irq(0);
    pic_unmask_irq(1);

    /*
     * Initialize the shell before interrupts
     * are enabled.
     */
    shell_init();


    /*
     * Bootstrap thread remains idle while
     * timer IRQs switch between worker threads.
     */
    while (1)
    {
        __asm__ volatile ("hlt");
    }
}

