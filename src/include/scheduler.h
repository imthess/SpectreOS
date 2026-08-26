#ifndef SPECTREOS_SCHEDULER_H
#define SPECTREOS_SCHEDULER_H

#include <stdint.h>

typedef enum
{
    SCHEDULER_ROUND_ROBIN = 0,
    SCHEDULER_PRIORITY,
    SCHEDULER_FCFS
} scheduler_policy_t;

void scheduler_init(void);

uint32_t scheduler_tick(
    uint32_t current_esp
);

uint32_t scheduler_get_switches(void);

void scheduler_set_policy(
    scheduler_policy_t policy
);

scheduler_policy_t scheduler_get_policy(void);

#endif
