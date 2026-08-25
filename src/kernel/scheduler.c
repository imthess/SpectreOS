
#include <stdint.h>

#include "scheduler.h"
#include "thread.h"

static int32_t scheduler_current = -1;

static uint64_t scheduler_switches = 0;


static int find_next_thread(void)
{
    if (thread_get_count() == 0)
    {
        return -1;
    }

    uint32_t start = 0;

    if (scheduler_current >= 0)
    {
        start =
            (uint32_t)scheduler_current + 1;
    }

    for (uint32_t offset = 0;
         offset < THREAD_MAX;
         offset++)
    {
        uint32_t index =
            (start + offset) % THREAD_MAX;

        thread_t* thread =
            thread_get(index);

        if (thread == 0)
        {
            continue;
        }

        if (
            thread->state == THREAD_READY ||
            thread->state == THREAD_RUNNING
        )
        {
            return (int)index;
        }
    }

    return -1;
}


void scheduler_init(void)
{
    scheduler_current = -1;
    scheduler_switches = 0;
}


uint32_t scheduler_tick(
    uint32_t current_esp
)
{
    if (thread_get_count() == 0)
    {
        return current_esp;
    }

    /*
     * First scheduling event.
     */
    if (scheduler_current < 0)
    {
        int next =
            find_next_thread();

        if (next < 0)
        {
            return current_esp;
        }

        scheduler_current =
            next;

        thread_set_current(
            (uint32_t)next
        );

        thread_set_state(
            (uint32_t)next,
            THREAD_RUNNING
        );

        thread_t* thread =
            thread_get((uint32_t)next);

        if (thread == 0)
        {
            return current_esp;
        }

        scheduler_switches++;

        return thread->context.esp;
    }


    /*
     * Save current thread context.
     */
    thread_t* current =
        thread_get(
            (uint32_t)scheduler_current
        );

    if (current != 0)
    {
        current->context.esp =
            current_esp;

        current->run_ticks++;

        if (current->state ==
            THREAD_RUNNING)
        {
            current->state =
                THREAD_READY;
        }
    }


    /*
     * Select next thread.
     */
    int next =
        find_next_thread();

    if (next < 0)
    {
        if (current != 0)
        {
            current->state =
                THREAD_RUNNING;
        }

        return current_esp;
    }


    scheduler_current =
        next;

    thread_set_current(
        (uint32_t)next
    );

    thread_set_state(
        (uint32_t)next,
        THREAD_RUNNING
    );

    thread_t* next_thread =
        thread_get(
            (uint32_t)next
        );

    if (next_thread == 0)
    {
        return current_esp;
    }

    scheduler_switches++;

    return next_thread->context.esp;
}


uint64_t scheduler_get_switches(void)
{
    return scheduler_switches;
}

