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
    uint32_t id;
    thread_state_t state;
    uint32_t saved_esp;
    uint32_t switches;
    uint32_t runs;
    uint32_t priority;
    uint32_t creation_order;
    uint8_t stack[THREAD_STACK_SIZE];
} thread_t;

void thread_init(void);

int thread_create(void (*entry)(void));

void thread_exit(void);

thread_t* thread_get(uint32_t index);

thread_t* thread_get_current(void);

int32_t thread_get_current_index(void);

void thread_set_current(int32_t index);

void thread_set_state(
    uint32_t index,
    thread_state_t state
);

uint32_t thread_get_count(void);

uint32_t thread_get_switches(
    uint32_t index
);

uint32_t thread_get_runs(
    uint32_t index
);

void thread_block_current(void);

void thread_unblock(uint32_t index);

void thread_mark_running(uint32_t index);

thread_state_t thread_get_state(uint32_t index);

#endif

/* Thread blocking / wakeup */
void thread_wake(uint32_t thread_id);
void thread_wake_one(void);

