#ifndef SPECTREOS_PROCESS_H
#define SPECTREOS_PROCESS_H

#include <stdint.h>

/*
 * ============================================================
 * THIN PROCESS WRAPPER
 *
 * This is deliberately NOT the full process_t from the roadmap
 * (no page directory, no user stack, no ring-3 entry point) -
 * it is exactly what was agreed as the first safe step: prove
 * the existing thread/scheduler foundation genuinely supports
 * multiple independent units of execution with real, timer-
 * driven preemption, before building anything riskier on top of
 * it. Every process here still runs in ring 0, in the single
 * shared kernel address space - there is no isolation yet.
 *
 * Design choice worth calling out: process state is NEVER stored
 * independently here - it is always derived live from
 * thread_get_state() on the underlying thread. Two separate
 * copies of "what state is this in" tracked in two different
 * files is exactly the bug that was found and fixed in
 * scheduler.c earlier in this project (it kept its own
 * `scheduler_current` instead of reading thread.c's), so this
 * wrapper is built specifically to not repeat that mistake.
 *
 * File descriptor tracking here is bookkeeping only, not
 * enforcement: fs.c's handle table is still global to the whole
 * kernel, not scoped per process, so this does not yet isolate
 * one process's files from another's. What it DOES give you for
 * real: automatic cleanup - fs_close() is called on every fd a
 * process registered, the moment its entry function returns
 * normally (see process_trampoline() in process.c). A process
 * that calls thread_exit() directly instead of returning
 * bypasses this cleanup - test code should return normally to
 * exercise it, which is what processtest's worker A does.
 * ============================================================
 */

#define PROCESS_MAX      16
#define PROCESS_MAX_FDS  4

typedef enum
{
    PROCESS_UNUSED = 0,
    PROCESS_READY,
    PROCESS_RUNNING,
    PROCESS_BLOCKED,
    PROCESS_TERMINATED

} process_state_t;

typedef struct
{
    uint32_t pid;
    uint32_t ppid;

    uint32_t thread_index;
    void (*entry)(void);

    int fds[PROCESS_MAX_FDS];
    uint32_t fd_count;

    uint32_t in_use;

} process_t;

void process_init(void);

/*
 * Creates a new process running `entry` (a plain function, same
 * shape thread_create() takes - no arguments, since there is no
 * user stack/argv to place them on yet). `ppid` is whatever PID
 * the caller wants recorded as the parent - process_create()
 * does not enforce or verify any relationship, it's bookkeeping.
 * Returns the new PID (>= 1), or -1 on failure (process table
 * full, or the underlying thread_create() failed).
 */
int process_create(
    void (*entry)(void),
    uint32_t ppid
);

/*
 * Registers a filesystem handle as belonging to `pid`, so it
 * gets closed automatically on that process's normal exit. Safe
 * to call from within the process's own entry function using its
 * own PID (look it up via process_get_current_pid()).
 */
void process_register_fd(
    uint32_t pid,
    int fd
);

/*
 * The PID of the process currently running, found by mapping
 * thread_get_current_index() back through the process table.
 * Returns 0 if the current thread isn't wrapped in a process
 * (e.g. the bootstrap thread itself, which is not a process).
 */
uint32_t process_get_current_pid(void);

process_t* process_get(uint32_t index);

uint32_t process_get_count(void);

/*
 * Always derived live from the underlying thread's state - see
 * the design note above. Returns PROCESS_UNUSED if `pid` doesn't
 * exist (already exited, or never existed).
 */
process_state_t process_get_state(uint32_t pid);

#endif
