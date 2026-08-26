#ifndef SPECTREOS_SYNC_H
#define SPECTREOS_SYNC_H

#include <stdint.h>

/*
 * Atomic spinlock.
 */
typedef struct
{
    volatile uint32_t locked;
} spinlock_t;

void spinlock_init(spinlock_t* lock);
void spinlock_acquire(spinlock_t* lock);
void spinlock_release(spinlock_t* lock);
int spinlock_try_acquire(spinlock_t* lock);

/*
 * Mutex.
 */
typedef struct
{
    spinlock_t lock;
} mutex_t;

void mutex_init(mutex_t* mutex);
void mutex_lock(mutex_t* mutex);
void mutex_unlock(mutex_t* mutex);

/*
 * Counting semaphore.
 */
typedef struct
{
    volatile int32_t count;
    spinlock_t lock;
} semaphore_t;

void semaphore_init(
    semaphore_t* semaphore,
    int32_t initial_count
);

void semaphore_wait(
    semaphore_t* semaphore
);

void semaphore_signal(
    semaphore_t* semaphore
);

#endif
