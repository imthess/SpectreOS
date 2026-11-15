#include <stdint.h>

#include "sync.h"
#include "thread.h"

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

    spinlock_init(&mutex->lock);
    mutex->owner = -1;
}

void mutex_lock(mutex_t* mutex)
{
    if (mutex == 0)
    {
        return;
    }

    spinlock_acquire(&mutex->lock);

    mutex->owner =
        thread_get_current_index();
}

void mutex_unlock(mutex_t* mutex)
{
    if (mutex == 0)
    {
        return;
    }

    int32_t current =
        thread_get_current_index();

    if (mutex->owner != current)
    {
        return;
    }

    mutex->owner = -1;

    spinlock_release(&mutex->lock);
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

    semaphore->count = initial_count;

    spinlock_init(&semaphore->lock);

    semaphore->waiter_count = 0;

    for (uint32_t i = 0;
         i < SYNC_WAITERS_MAX;
         i++)
    {
        semaphore->waiters[i] = 0;
    }
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
        spinlock_acquire(&semaphore->lock);

        if (semaphore->count > 0)
        {
            semaphore->count--;

            spinlock_release(
                &semaphore->lock
            );

            return;
        }

        int32_t current =
            thread_get_current_index();

        if (current >= 0 &&
            semaphore->waiter_count <
            SYNC_WAITERS_MAX)
        {
            semaphore->waiters[
                semaphore->waiter_count++
            ] = (uint32_t)current;

            thread_block_current();

            spinlock_release(
                &semaphore->lock
            );

            /*
             * The PIT will switch to another
             * READY thread. When this thread
             * is awakened it retries the wait.
             */
            while (
                thread_get_state(
                    (uint32_t)current
                ) == THREAD_BLOCKED
            )
            {
                __asm__ volatile (
                    "hlt"
                );
            }

            continue;
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

    spinlock_acquire(&semaphore->lock);

    if (semaphore->waiter_count > 0)
    {
        uint32_t thread_id =
            semaphore->waiters[0];

        for (uint32_t i = 1;
             i < semaphore->waiter_count;
             i++)
        {
            semaphore->waiters[i - 1] =
                semaphore->waiters[i];
        }

        semaphore->waiter_count--;

        thread_unblock(thread_id);
    }
    else
    {
        semaphore->count++;
    }

    spinlock_release(&semaphore->lock);
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

    if (mutex.owner !=
        thread_get_current_index())
    {
        return 0;
    }

    mutex_unlock(&mutex);

    if (mutex.owner != -1)
    {
        return 0;
    }

    semaphore_init(
        &semaphore,
        1
    );

    semaphore_wait(&semaphore);

    if (semaphore.count != 0)
    {
        return 0;
    }

    semaphore_signal(&semaphore);

    if (semaphore.count != 1)
    {
        return 0;
    }

    return 1;
}
