#include <stdint.h>

#include "sync.h"

static inline uint32_t atomic_exchange(
    volatile uint32_t* address,
    uint32_t value
)
{
    __asm__ volatile (
        "xchgl %0, %1"
        : "+r"(value), "+m"(*address)
        :
        : "memory"
    );

    return value;
}

void spinlock_init(spinlock_t* lock)
{
    if (lock == 0)
    {
        return;
    }

    lock->locked = 0;
}

int spinlock_try_acquire(spinlock_t* lock)
{
    if (lock == 0)
    {
        return 0;
    }

    return atomic_exchange(
        &lock->locked,
        1
    ) == 0;
}

void spinlock_acquire(spinlock_t* lock)
{
    if (lock == 0)
    {
        return;
    }

    while (!spinlock_try_acquire(lock))
    {
        __asm__ volatile (
            "pause"
            :
            :
            : "memory"
        );
    }
}

void spinlock_release(spinlock_t* lock)
{
    if (lock == 0)
    {
        return;
    }

    __asm__ volatile (
        "movl $0, %0"
        : "=m"(lock->locked)
        :
        : "memory"
    );
}

void mutex_init(mutex_t* mutex)
{
    if (mutex == 0)
    {
        return;
    }

    spinlock_init(
        &mutex->lock
    );
}

void mutex_lock(mutex_t* mutex)
{
    if (mutex == 0)
    {
        return;
    }

    spinlock_acquire(
        &mutex->lock
    );
}

void mutex_unlock(mutex_t* mutex)
{
    if (mutex == 0)
    {
        return;
    }

    spinlock_release(
        &mutex->lock
    );
}

void semaphore_init(
    semaphore_t* semaphore,
    int32_t initial_count
)
{
    if (semaphore == 0)
    {
        return;
    }

    semaphore->count =
        initial_count;

    spinlock_init(
        &semaphore->lock
    );
}

void semaphore_wait(
    semaphore_t* semaphore
)
{
    if (semaphore == 0)
    {
        return;
    }

    for (;;)
    {
        spinlock_acquire(
            &semaphore->lock
        );

        if (semaphore->count > 0)
        {
            semaphore->count--;

            spinlock_release(
                &semaphore->lock
            );

            return;
        }

        spinlock_release(
            &semaphore->lock
        );

        __asm__ volatile (
            "pause"
            :
            :
            : "memory"
        );
    }
}

void semaphore_signal(
    semaphore_t* semaphore
)
{
    if (semaphore == 0)
    {
        return;
    }

    spinlock_acquire(
        &semaphore->lock
    );

    semaphore->count++;

    spinlock_release(
        &semaphore->lock
    );
}


int sync_self_test(void)
{
    spinlock_t spin;
    mutex_t mutex;
    semaphore_t semaphore;

    spinlock_init(&spin);

    if (!spinlock_try_acquire(&spin))
    {
        return 0;
    }

    if (spinlock_try_acquire(&spin))
    {
        spinlock_release(&spin);
        return 0;
    }

    spinlock_release(&spin);

    if (!spinlock_try_acquire(&spin))
    {
        return 0;
    }

    spinlock_release(&spin);

    mutex_init(&mutex);

    mutex_lock(&mutex);
    mutex_unlock(&mutex);

    semaphore_init(
        &semaphore,
        1
    );

    semaphore_wait(&semaphore);

    semaphore_signal(&semaphore);

    return 1;
}
