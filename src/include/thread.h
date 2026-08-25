
#ifndef SPECTREOS_THREAD_H
#define SPECTREOS_THREAD_H

#include <stdint.h>

#define THREAD_MAX 32
#define THREAD_STACK_SIZE 4096

typedef enum
{
    THREAD_UNUSED = 0,
    THREAD_READY,
    THREAD_RUNNING,
    THREAD_BLOCKED,
    THREAD_TERMINATED
} thread_state_t;

typedef struct
{
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t esp;

    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;

    uint32_t eip;
    uint32_t eflags;
} thread_context_t;

typedef struct
{
    uint32_t id;

    thread_state_t state;

    thread_context_t context;

    uint64_t run_ticks;

    uint8_t stack[THREAD_STACK_SIZE];

} thread_t;

void thread_init(void);

int thread_create(void (*entry)(void));

thread_t* thread_get_current(void);

thread_t* thread_get(uint32_t index);

uint32_t thread_get_count(void);

void thread_set_current(uint32_t index);

void thread_set_state(
    uint32_t index,
    thread_state_t state
);

const char* thread_state_name(
    thread_state_t state
);

#endif

