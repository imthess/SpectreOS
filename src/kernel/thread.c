#include <stdint.h>

#include "thread.h"

#define INITIAL_EFLAGS        0x202
#define KERNEL_CODE_SELECTOR  0x08

static thread_t threads[THREAD_MAX];

static uint32_t thread_count = 0;
static int32_t current_thread = -1;

static void build_initial_stack(
    thread_t* thread,
    void (*entry)(void)
)
{
    uint32_t* sp =
        (uint32_t*)&thread->stack[THREAD_STACK_SIZE];

    *--sp = INITIAL_EFLAGS;
    *--sp = KERNEL_CODE_SELECTOR;
    *--sp = (uint32_t)entry;

    *--sp = 0;
    *--sp = 32;

    *--sp = 0;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;

    thread->saved_esp =
        (uint32_t)sp;
}

void thread_init(void)
{
    for (uint32_t i = 0;
         i < THREAD_MAX;
         i++)
    {
        threads[i].id = 0;
        threads[i].state = THREAD_UNUSED;
        threads[i].saved_esp = 0;
        threads[i].switches = 0;
        threads[i].runs = 0;
    }

    /*
     * Thread 0 is the bootstrap kernel thread.
     */
    threads[0].id = 1;
    threads[0].state = THREAD_RUNNING;

    thread_count = 1;
    current_thread = 0;
}

int thread_create(void (*entry)(void))
{
    if (entry == 0)
    {
        return -1;
    }

    for (uint32_t i = 1;
         i < THREAD_MAX;
         i++)
    {
        if (threads[i].state ==
            THREAD_UNUSED)
        {
            thread_t* thread =
                &threads[i];

            thread->id = i + 1;
            thread->state = THREAD_READY;
            thread->switches = 0;
            thread->runs = 0;

            build_initial_stack(
                thread,
                entry
            );

            thread_count++;

            return (int)thread->id;
        }
    }

    return -1;
}

void thread_exit(void)
{
    if (current_thread >= 0)
    {
        threads[current_thread].state =
            THREAD_TERMINATED;
    }

    /*
     * Do not continue executing a terminated
     * thread.
     *
     * The PIT interrupt will select another
     * runnable thread.
     */
    while (1)
    {
        __asm__ volatile ("hlt");
    }
}

thread_t* thread_get(uint32_t index)
{
    if (index >= THREAD_MAX)
    {
        return 0;
    }

    if (threads[index].state ==
        THREAD_UNUSED)
    {
        return 0;
    }

    return &threads[index];
}

thread_t* thread_get_current(void)
{
    if (current_thread < 0)
    {
        return 0;
    }

    return &threads[current_thread];
}

int32_t thread_get_current_index(void)
{
    return current_thread;
}

void thread_set_current(int32_t index)
{
    if (index < 0 ||
        index >= THREAD_MAX)
    {
        return;
    }

    current_thread = index;
}

void thread_set_state(
    uint32_t index,
    thread_state_t state
)
{
    if (index >= THREAD_MAX)
    {
        return;
    }

    if (threads[index].state ==
        THREAD_UNUSED)
    {
        return;
    }

    threads[index].state = state;
}

uint32_t thread_get_count(void)
{
    return thread_count;
}

uint32_t thread_get_switches(
    uint32_t index
)
{
    if (index >= THREAD_MAX)
    {
        return 0;
    }

    return threads[index].switches;
}

uint32_t thread_get_runs(
    uint32_t index
)
{
    if (index >= THREAD_MAX)
    {
        return 0;
    }

    return threads[index].runs;
}


void thread_block_current(void)
{
    if (current_thread < 0)
    {
        return;
    }

    if (threads[current_thread].state ==
        THREAD_RUNNING)
    {
        threads[current_thread].state =
            THREAD_BLOCKED;
    }
}

void thread_unblock(uint32_t index)
{
    if (index >= THREAD_MAX)
    {
        return;
    }

    if (threads[index].state ==
        THREAD_BLOCKED)
    {
        threads[index].state =
            THREAD_READY;
    }
}

void thread_mark_running(uint32_t index)
{
    if (index >= THREAD_MAX)
    {
        return;
    }

    if (threads[index].state ==
        THREAD_READY)
    {
        threads[index].state =
            THREAD_RUNNING;
    }
}


thread_state_t thread_get_state(
    uint32_t index
)
{
    if (index >= THREAD_MAX)
    {
        return THREAD_UNUSED;
    }

    return threads[index].state;
}


/*
 * ============================================================
 * THREAD BLOCKING / WAKEUP
 * ============================================================
 */


void thread_wake(uint32_t thread_id)
{
    if (thread_id == 0)
    {
        return;
    }

    uint32_t index = thread_id - 1;

    if (index >= THREAD_MAX)
    {
        return;
    }

    if (threads[index].state == THREAD_BLOCKED)
    {
        threads[index].state = THREAD_READY;
    }
}

void thread_wake_one(void)
{
    for (uint32_t i = 0;
         i < THREAD_MAX;
         i++)
    {
        if (threads[i].state == THREAD_BLOCKED)
        {
            threads[i].state = THREAD_READY;
            return;
        }
    }
}


void thread_record_run(uint32_t index)
{
    if (index >= THREAD_MAX)
    {
        return;
    }

    if (threads[index].state == THREAD_UNUSED)
    {
        return;
    }

    threads[index].runs++;
}

void thread_record_switch(uint32_t index)
{
    if (index >= THREAD_MAX)
    {
        return;
    }

    if (threads[index].state == THREAD_UNUSED)
    {
        return;
    }

    threads[index].switches++;
}
