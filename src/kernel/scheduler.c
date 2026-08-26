#include <stdint.h>

#include "scheduler.h"
#include "thread.h"

static int32_t scheduler_current = -1;
static scheduler_policy_t scheduler_policy =
    SCHEDULER_ROUND_ROBIN;

static uint32_t scheduler_switches = 0;

/*
 * Round-Robin:
 * Select the next READY thread after current.
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
 * Priority:
 * Highest priority number wins.
 */
static int32_t find_priority(void)
{
    int32_t best = -1;
    uint32_t best_priority = 0;

    for (uint32_t i = 0;
         i < THREAD_MAX;
         i++)
    {
        thread_t* thread =
            thread_get(i);

        if (thread == 0 ||
            thread->state != THREAD_READY)
        {
            continue;
        }

        if (best < 0 ||
            thread->priority > best_priority)
        {
            best = (int32_t)i;
            best_priority =
                thread->priority;
        }
    }

    return best;
}

/*
 * FCFS:
 * Select the READY thread with the
 * earliest creation order.
 */
static int32_t find_fcfs(void)
{
    int32_t best = -1;
    uint32_t best_order = 0;

    for (uint32_t i = 0;
         i < THREAD_MAX;
         i++)
    {
        thread_t* thread =
            thread_get(i);

        if (thread == 0 ||
            thread->state != THREAD_READY)
        {
            continue;
        }

        if (best < 0 ||
            thread->creation_order <
            best_order)
        {
            best = (int32_t)i;
            best_order =
                thread->creation_order;
        }
    }

    return best;
}

static int32_t find_next_thread(void)
{
    switch (scheduler_policy)
    {
        case SCHEDULER_PRIORITY:
            return find_priority();

        case SCHEDULER_FCFS:
            return find_fcfs();

        case SCHEDULER_ROUND_ROBIN:
        default:
            return find_round_robin();
    }
}

void scheduler_init(void)
{
    scheduler_current = 0;
}

void scheduler_set_policy(
    scheduler_policy_t policy
)
{
    if (policy > SCHEDULER_FCFS)
    {
        return;
    }

    scheduler_policy = policy;
}

scheduler_policy_t scheduler_get_policy(void)
{
    return scheduler_policy;
}

uint32_t scheduler_tick(uint32_t current_esp)
{
    /*
     * Save interrupted context.
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

    int32_t next =
        find_next_thread();

    /*
     * No runnable thread.
     */
    if (next < 0)
    {
        return current_esp;
    }

    if (next != scheduler_current)
    {
        scheduler_switches++;
    }

    scheduler_current = next;

    thread_t* selected =
        thread_get(
            (uint32_t)next
        );

    if (selected == 0)
    {
        return current_esp;
    }

    selected->state =
        THREAD_RUNNING;

    selected->runs++;

    return selected->saved_esp;
}

thread_t* scheduler_get_current(void)
{
    if (scheduler_current < 0)
    {
        return 0;
    }

    return thread_get(
        (uint32_t)scheduler_current
    );
}

uint32_t scheduler_get_switches(void)
{
    return scheduler_switches;
}
