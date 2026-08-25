
#include <stdint.h>

#include "thread.h"

#define KERNEL_CODE_SEGMENT 0x08
#define INITIAL_EFLAGS      0x202

static thread_t threads[THREAD_MAX];

static uint32_t thread_count = 0;
static int32_t current_thread = -1;


static void thread_return_trap(void)
{
    __asm__ volatile ("cli");

    while (1)
    {
        __asm__ volatile ("hlt");
    }
}


void thread_init(void)
{
    for (uint32_t i = 0;
         i < THREAD_MAX;
         i++)
    {
        threads[i].id = 0;
        threads[i].state = THREAD_UNUSED;

        threads[i].context.edi = 0;
        threads[i].context.esi = 0;
        threads[i].context.ebp = 0;
        threads[i].context.esp = 0;

        threads[i].context.ebx = 0;
        threads[i].context.edx = 0;
        threads[i].context.ecx = 0;
        threads[i].context.eax = 0;

        threads[i].context.eip = 0;
        threads[i].context.eflags = 0;

        threads[i].run_ticks = 0;
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

    while (
        slot < THREAD_MAX &&
        threads[slot].state != THREAD_UNUSED
    )
    {
        slot++;
    }

    if (slot >= THREAD_MAX)
    {
        return -1;
    }

    thread_t* thread =
        &threads[slot];

    thread->id =
        slot + 1;

    thread->state =
        THREAD_READY;

    thread->run_ticks = 0;

    uint32_t stack_top =
        (uint32_t)&thread->stack[
            THREAD_STACK_SIZE
        ];

    stack_top &= ~0x0F;

    uint32_t esp =
        stack_top - 52;

    uint32_t* frame =
        (uint32_t*)esp;

    /*
     * PUSHA frame.
     */
    frame[0] = 0;
    frame[1] = 0;
    frame[2] = 0;
    frame[3] = esp;
    frame[4] = 0;
    frame[5] = 0;
    frame[6] = 0;
    frame[7] = 0;

    /*
     * Interrupt metadata.
     */
    frame[8] = 0;
    frame[9] = 0;

    /*
     * CPU return frame.
     */
    frame[10] =
        (uint32_t)entry;

    frame[11] =
        KERNEL_CODE_SEGMENT;

    frame[12] =
        INITIAL_EFLAGS;

    /*
     * Keep this symbol referenced so the compiler
     * does not remove the trap function.
     */
    (void)thread_return_trap;

    thread->context.esp =
        esp;

    thread->context.ebp =
        esp;

    thread->context.eip =
        (uint32_t)entry;

    thread->context.eflags =
        INITIAL_EFLAGS;

    thread_count++;

    return (int)thread->id;
}


thread_t* thread_get_current(void)
{
    if (current_thread < 0)
    {
        return 0;
    }

    if ((uint32_t)current_thread >=
        THREAD_MAX)
    {
        return 0;
    }

    return &threads[current_thread];
}


thread_t* thread_get(uint32_t index)
{
    if (index >= THREAD_MAX)
    {
        return 0;
    }

    return &threads[index];
}


uint32_t thread_get_count(void)
{
    return thread_count;
}


void thread_set_current(uint32_t index)
{
    if (index >= THREAD_MAX)
    {
        return;
    }

    current_thread =
        (int32_t)index;
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

    threads[index].state =
        state;
}


const char* thread_state_name(
    thread_state_t state
)
{
    switch (state)
    {
        case THREAD_UNUSED:
            return "UNUSED";

        case THREAD_READY:
            return "READY";

        case THREAD_RUNNING:
            return "RUNNING";

        case THREAD_BLOCKED:
            return "BLOCKED";

        case THREAD_TERMINATED:
            return "TERMINATED";

        default:
            return "UNKNOWN";
    }
}

