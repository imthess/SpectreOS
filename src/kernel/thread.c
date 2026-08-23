#include <stdint.h>

#include "thread.h"

static thread_t threads[THREAD_MAX];

static uint32_t thread_count = 0;
static int32_t current_thread = -1;

void thread_init(void)
{
    for (uint32_t i = 0; i < THREAD_MAX; i++)
    {
        threads[i].id = 0;
        threads[i].state = THREAD_UNUSED;

        threads[i].context.eip = 0;
        threads[i].context.eflags = 0;
        threads[i].context.esp = 0;
        threads[i].context.ebp = 0;
    }

    thread_count = 0;
    current_thread = -1;
}

int thread_create(void (*entry)(void))
{
    if (entry == 0)
    {
        return -1;
    }

    if (thread_count >= THREAD_MAX)
    {
        return -1;
    }

    uint32_t slot = 0;

    while (slot < THREAD_MAX &&
           threads[slot].state != THREAD_UNUSED)
    {
        slot++;
    }

    if (slot >= THREAD_MAX)
    {
        return -1;
    }

    thread_t* thread = &threads[slot];

    thread->id = slot + 1;

    thread->state = THREAD_READY;

    /*
     * Entry point where this thread will begin
     * execution once the scheduler switches to it.
     */
    thread->context.eip =
        (uint32_t)entry;

    /*
     * IF = 1.
     *
     * This allows hardware interrupts once the
     * thread begins executing.
     */
    thread->context.eflags =
        0x202;

    /*
     * Each thread gets its own kernel stack.
     *
     * x86 stacks grow downward, therefore ESP
     * starts near the end of the stack array.
     */
    thread->context.esp =
        (uint32_t)&thread->stack[
            THREAD_STACK_SIZE - 16
        ];

    thread->context.ebp =
        thread->context.esp;

    thread_count++;

    return (int)thread->id;
}

thread_t* thread_get_current(void)
{
    if (current_thread < 0)
    {
        return 0;
    }

    return &threads[current_thread];
}

void thread_yield(void)
{
    /*
     * Actual context switching will be added
     * in the scheduler phase.
     */
}

uint32_t thread_get_count(void)
{
    return thread_count;
}