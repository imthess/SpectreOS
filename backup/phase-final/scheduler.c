#include <stdint.h>

#include "scheduler.h"
#include "thread.h"

static int32_t scheduler_current = -1;

static uint32_t scheduler_switches = 0;

static scheduler_policy_t scheduler_policy =
    SCHEDULER_ROUND_ROBIN;


/*
 * ============================================================
 * ROUND-ROBIN
 * ============================================================
 */

static int32_t find_round_robin(void)
{
    int32_t current =
        scheduler_current;

    for (uint32_t offset = 1;
         offset <= THREAD_MAX;
         offset++)
    {
        uint32_t index;

        if (current < 0)
        {
            index = offset - 1;
        }
        else
        {
            index =
                ((uint32_t)current + offset)
                % THREAD_MAX;
        }

        thread_t* thread =
            thread_get(index);

        if (thread != 0 &&
            thread->state == THREAD_READY)
        {
            return (int32_t)index;
        }
    }

    return -1;
}


/*
 * ============================================================
 * FCFS
 *
 * Select the READY thread with the lowest thread ID.
 * ============================================================
 */

static int32_t find_fcfs(void)
{
    for (uint32_t index = 0;
         index < THREAD_MAX;
         index++)
    {
        thread_t* thread =
            thread_get(index);

        if (thread != 0 &&
            thread->state == THREAD_READY)
        {
            return (int32_t)index;
        }
    }

    return -1;
}


/*
 * ============================================================
 * PRIORITY
 *
 * Lower thread ID currently represents higher priority.
 *
 * This keeps the existing thread structure compatible while
 * providing a deterministic priority scheduling policy.
 * ============================================================
 */

static int32_t find_priority(void)
{
    int32_t selected = -1;

    for (uint32_t index = 0;
         index < THREAD_MAX;
         index++)
    {
        thread_t* thread =
            thread_get(index);

        if (thread == 0)
        {
            continue;
        }

        if (thread->state != THREAD_READY)
        {
            continue;
        }

        if (selected < 0)
        {
            selected = (int32_t)index;
            continue;
        }

        /*
         * Smaller ID = higher priority.
         */
        if (thread->id <
            thread_get((uint32_t)selected)->id)
        {
            selected = (int32_t)index;
        }
    }

    return selected;
}


/*
 * ============================================================
 * POLICY DISPATCH
 * ============================================================
 */

static int32_t find_next_thread(void)
{
    switch (scheduler_policy)
    {
        case SCHEDULER_FCFS:
            return find_fcfs();

        case SCHEDULER_PRIORITY:
            return find_priority();

        case SCHEDULER_ROUND_ROBIN:
        default:
            return find_round_robin();
    }
}


/*
 * ============================================================
 * INITIALIZATION
 * ============================================================
 */

void scheduler_init(void)
{
    scheduler_current = -1;

    scheduler_switches = 0;

    scheduler_policy =
        SCHEDULER_ROUND_ROBIN;
}


/*
 * ============================================================
 * TIMER SCHEDULING
 * ============================================================
 */

uint32_t scheduler_tick(uint32_t current_esp)
{
    /*
     * Save the interrupted thread context.
     */
    if (scheduler_current >= 0)
    {
        thread_t* current =
            thread_get(
                (uint32_t)scheduler_current
            );

        if (current != 0)
        {
            current->saved_esp =
                current_esp;

            if (current->state ==
                THREAD_RUNNING)
            {
                current->state =
                    THREAD_READY;
            }
        }
    }

    /*
     * Select the next runnable thread.
     */
    int32_t next =
        find_next_thread();

    /*
     * Nothing runnable.
     */
    if (next < 0)
    {
        return current_esp;
    }

    thread_t* selected =
        thread_get(
            (uint32_t)next
        );

    if (selected == 0)
    {
        return current_esp;
    }

    /*
     * Count actual thread changes.
     */
    if (next != scheduler_current)
    {
        scheduler_switches++;

        thread_record_switch(
            (uint32_t)next
        );
    }

    scheduler_current =
        next;

    selected->state =
        THREAD_RUNNING;

    thread_record_run(
        (uint32_t)next
    );

    return selected->saved_esp;
}


/*
 * ============================================================
 * POLICY CONTROL
 * ============================================================
 */

void scheduler_set_policy(
    scheduler_policy_t policy
)
{
    if (policy >
        SCHEDULER_PRIORITY)
    {
        return;
    }

    scheduler_policy =
        policy;
}

scheduler_policy_t scheduler_get_policy(void)
{
    return scheduler_policy;
}


/*
 * ============================================================
 * STATISTICS
 * ============================================================
 */

uint32_t scheduler_get_switches(void)
{
    return scheduler_switches;
}
