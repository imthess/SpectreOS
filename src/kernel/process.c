#include <stdint.h>

#include "process.h"
#include "thread.h"
#include "fs.h"

static process_t processes[PROCESS_MAX];
static uint32_t next_pid = 1;

void process_init(void)
{
    for (uint32_t i = 0; i < PROCESS_MAX; i++)
    {
        processes[i].pid = 0;
        processes[i].ppid = 0;
        processes[i].thread_index = 0;
        processes[i].entry = 0;
        processes[i].fd_count = 0;
        processes[i].in_use = 0;
    }

    next_pid = 1;
}

static process_t* process_find_by_pid(uint32_t pid)
{
    if (pid == 0)
    {
        return 0;
    }

    for (uint32_t i = 0; i < PROCESS_MAX; i++)
    {
        if (processes[i].in_use &&
            processes[i].pid == pid)
        {
            return &processes[i];
        }
    }

    return 0;
}

static process_t* process_find_by_thread_index(
    uint32_t thread_index
)
{
    for (uint32_t i = 0; i < PROCESS_MAX; i++)
    {
        if (processes[i].in_use &&
            processes[i].thread_index == thread_index)
        {
            return &processes[i];
        }
    }

    return 0;
}

/*
 * The single shared entry point every process-wrapped thread
 * actually starts at. Looks itself up in the process table by
 * its own thread index (valid the moment it starts running -
 * thread_get_current_index() reflects whichever thread the
 * scheduler just switched to), runs the real entry function the
 * caller asked for, then cleans up any fds that function
 * registered before finally calling thread_exit().
 *
 * One shared trampoline instead of one-per-process avoids the
 * same problem crypto_disk.c's fixed 2-slot design works around:
 * thread_create() takes a plain no-argument function pointer,
 * with no context/closure mechanism, so there is no way to pass
 * "which process is this" into a shared function except by
 * having it look itself up - which thread_get_current_index()
 * makes possible here, unlike blockdev.h's driver interface.
 */
static void process_trampoline(void)
{
    int32_t thread_index = thread_get_current_index();

    process_t* proc =
        (thread_index >= 0) ?
        process_find_by_thread_index(
            (uint32_t)thread_index
        ) :
        0;

    if (proc != 0 && proc->entry != 0)
    {
        proc->entry();
    }

    if (proc != 0)
    {
        for (uint32_t i = 0; i < proc->fd_count; i++)
        {
            fs_close(proc->fds[i]);
        }

        proc->fd_count = 0;
    }

    /*
     * Marks the underlying thread TERMINATED and halts. Process
     * state is derived from that automatically (see
     * process_get_state()) - nothing further to update here.
     */
    thread_exit();
}

int process_create(
    void (*entry)(void),
    uint32_t ppid
)
{
    if (entry == 0)
    {
        return -1;
    }

    int slot = -1;

    for (uint32_t i = 0; i < PROCESS_MAX; i++)
    {
        if (!processes[i].in_use)
        {
            slot = (int)i;
            break;
        }
    }

    if (slot < 0)
    {
        return -1;
    }

    int thread_id = thread_create(process_trampoline);

    if (thread_id < 0)
    {
        return -1;
    }

    process_t* proc = &processes[slot];

    proc->pid = next_pid;
    next_pid++;

    proc->ppid = ppid;

    /*
     * thread_create() returns an id (index + 1) - see thread.c -
     * so the table index it actually lives at is one less.
     */
    proc->thread_index = (uint32_t)(thread_id - 1);

    proc->entry = entry;
    proc->fd_count = 0;
    proc->in_use = 1;

    return (int)proc->pid;
}

void process_register_fd(
    uint32_t pid,
    int fd
)
{
    process_t* proc = process_find_by_pid(pid);

    if (proc == 0 ||
        proc->fd_count >= PROCESS_MAX_FDS)
    {
        return;
    }

    proc->fds[proc->fd_count] = fd;
    proc->fd_count++;
}

uint32_t process_get_current_pid(void)
{
    int32_t thread_index = thread_get_current_index();

    if (thread_index < 0)
    {
        return 0;
    }

    process_t* proc =
        process_find_by_thread_index(
            (uint32_t)thread_index
        );

    return (proc != 0) ? proc->pid : 0;
}

process_t* process_get(uint32_t index)
{
    if (index >= PROCESS_MAX ||
        !processes[index].in_use)
    {
        return 0;
    }

    return &processes[index];
}

uint32_t process_get_count(void)
{
    uint32_t count = 0;

    for (uint32_t i = 0; i < PROCESS_MAX; i++)
    {
        if (processes[i].in_use)
        {
            count++;
        }
    }

    return count;
}

process_state_t process_get_state(uint32_t pid)
{
    process_t* proc = process_find_by_pid(pid);

    if (proc == 0)
    {
        return PROCESS_UNUSED;
    }

    thread_state_t thread_state =
        thread_get_state(proc->thread_index);

    switch (thread_state)
    {
        case THREAD_READY:
            return PROCESS_READY;

        case THREAD_RUNNING:
            return PROCESS_RUNNING;

        case THREAD_BLOCKED:
            return PROCESS_BLOCKED;

        case THREAD_TERMINATED:
            return PROCESS_TERMINATED;

        case THREAD_UNUSED:
        default:
            return PROCESS_UNUSED;
    }
}
