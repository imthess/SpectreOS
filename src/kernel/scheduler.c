#include <stdint.h>

#include "scheduler.h"
#include "thread.h"

/*
 * ============================================================
 * NOTE ON "CURRENT THREAD" STATE
 *
 * This module used to keep its own private `scheduler_current`
 * variable in parallel with thread.c's `current_thread`. The
 * two were never synchronized: thread_init() sets thread.c's
 * copy to 0 for the bootstrap thread, while this file started
 * its own copy at -1. That meant:
 *
 *   1. The bootstrap/idle thread (index 0) was never selected
 *      by find_round_robin()/find_fcfs()/find_priority(),
 *      because they only pick THREAD_READY threads, and nothing
 *      ever transitioned thread 0 out of THREAD_RUNNING back to
 *      THREAD_READY - scheduler_tick() only did that for
 *      whichever thread scheduler_current pointed at, which was
 *      never thread 0.
 *
 *   2. Anything that called thread_get_current()/
 *      thread_get_current_index() (both in thread.c) could
 *      disagree with what the scheduler actually had running,
 *      since nothing here ever called thread_set_current().
 *
 * Fix: this file no longer keeps its own copy. thread.c is the
 * single source of truth for "which thread is running", and
 * this file reads/updates it via thread_get_current_index()/
 * thread_set_current(). This also makes thread 0 reachable by
 * the scheduler like any other thread once the bootstrap thread
 * is preempted for the first time.
 * ============================================================
 */

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
        thread_get_current_index();

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
    /*
     * Deliberately does not touch thread.c's current-thread
     * state: thread_init() (called before this) already set it
     * to the bootstrap thread, and that is the single source of
     * truth now. See the note above find_round_robin().
     */

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
    int32_t current_index =
        thread_get_current_index();

    /*
     * Save the interrupted thread context.
     */
    if (current_index >= 0)
    {
        thread_t* current =
            thread_get(
                (uint32_t)current_index
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
    if (next != current_index)
    {
        scheduler_switches++;

        thread_record_switch(
            (uint32_t)next
        );
    }

    thread_set_current(next);

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
