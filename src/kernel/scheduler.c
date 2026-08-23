#include <stdint.h>

#include "thread.h"

static int32_t scheduler_current = -1;

static uint32_t scheduler_search_start = 0;

void scheduler_init(void)
{
    scheduler_current = -1;
    scheduler_search_start = 0;
}

static int find_next_thread(void)
{
    /*
     * Round-Robin search.
     *
     * Start immediately after the currently
     * selected thread and wrap around.
     */
    for (uint32_t offset = 1;
         offset <= THREAD_MAX;
         offset++)
    {
        uint32_t index =
            (scheduler_search_start + offset)
            % THREAD_MAX;

        /*
         * We currently don't have direct access
         * to the thread table.
         *
         * Actual selection will be moved into
         * the thread manager during the context
         * switching phase.
         */
        (void)index;
    }

    return -1;
}

void scheduler_tick(void)
{
    /*
     * Scheduling policy will be connected to
     * the thread table during context switching.
     */
}

thread_t* scheduler_get_current(void)
{
    (void)scheduler_current;

    return 0;
}