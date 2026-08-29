#include <stdint.h>

#include "syscall.h"
#include "hardware.h"
#include "memory.h"
#include "fs.h"

static uint16_t syscall_cursor = 0;

/*
 * ------------------------------------------------------------
 * do_syscall()
 *
 * Issues a real INT 0x80, matching the ABI syscall_handler()
 * expects on the other side:
 *
 *   EAX = syscall number
 *   EBX = arg1
 *   ECX = arg2
 *   EDX = arg3
 *
 * interrupts.asm's isr80 wraps the call to syscall_handler()
 * in pusha/popa, so EBX/ECX/EDX/ESI/EDI/EBP all survive the
 * interrupt unchanged; only EAX is overwritten, with the
 * dispatch result written into regs[7] before popa restores
 * it. That means this is a genuine ring0->ring0 syscall: EAX
 * really does come back as syscall_dispatch()'s return value,
 * not a value the compiler already had lying around.
 *
 * This is the single place that actually executes INT 0x80;
 * every spectre_* wrapper below goes through this instead of
 * calling the underlying logic directly, so int 0x80 ->
 * isr80 -> syscall_handler -> syscall_dispatch() is genuinely
 * exercised on every call.
 * ------------------------------------------------------------
 */
static uint32_t do_syscall(
    uint32_t number,
    uint32_t arg1,
    uint32_t arg2,
    uint32_t arg3
)
{
    uint32_t result;

    __asm__ volatile (
        "int $0x80"
        : "=a"(result)
        : "a"(number),
          "b"(arg1),
          "c"(arg2),
          "d"(arg3)
        : "memory", "cc"
    );

    return result;
}

/*
 * ------------------------------------------------------------
 * Kernel-side syscall implementations.
 *
 * These run only on the far side of INT 0x80, inside
 * syscall_handler() -> syscall_dispatch(). Nothing outside
 * this file should call them directly; spectre_* below is the
 * public API, and it always goes through do_syscall().
 * ------------------------------------------------------------
 */

static uint32_t syscall_hwinfo(cpu_info_t* info)
{
    if (info == 0)
        return 0;

    hardware_get_cpu_info(info);
    return 1;
}

static uint32_t syscall_meminfo(memory_info_t* info)
{
    if (info == 0)
    {
        return 0;
    }

    return memory_get_info(info);
}

static uint32_t syscall_write(const char* text)
{
    if (text == 0)
        return 0;

    volatile uint16_t* video =
        (volatile uint16_t*)0xB8000;

    while (*text)
    {
        if (syscall_cursor >= 80 * 25)
            syscall_cursor = 0;

        video[syscall_cursor++] =
            (uint16_t)(0x07 << 8) |
            (uint8_t)*text++;

    }

    return 1;
}

uint32_t syscall_dispatch(
    uint32_t number,
    uint32_t arg1,
    uint32_t arg2,
    uint32_t arg3
)
{
    switch (number)
    {
        case SYS_WRITE:
            return syscall_write(
                (const char*)arg1
            );

        case SYS_READ:
            return 0;

        case SYS_EXIT:
            return 0;

        case SYS_GETPID:
            return 1;

        case SYS_MEMINFO:
            return syscall_meminfo(
                (memory_info_t*)arg1
            );

        case SYS_HWINFO:
            return syscall_hwinfo(
                (cpu_info_t*)arg1
            );

        case SYS_YIELD:
            __asm__ volatile ("int $0x20");
            return 0;

        case SYS_OPEN:
            return (uint32_t)fs_open(
                (const char*)arg1
            );

        case SYS_READ_FILE:
            return (uint32_t)fs_read(
                (int)arg1,
                (void*)arg2,
                arg3
            );

        case SYS_WRITE_FILE:
            return (uint32_t)fs_write(
                (int)arg1,
                (const void*)arg2,
                arg3
            );

        case SYS_CLOSE:
            return (uint32_t)fs_close(
                (int)arg1
            );

        case SYS_CREATE:
            return (uint32_t)fs_create(
                (const char*)arg1
            );

        case SYS_DELETE:
            return (uint32_t)fs_delete(
                (const char*)arg1
            );

        case SYS_LIST:
            return (uint32_t)fs_list(
                (char*)arg1,
                arg2
            );

        default:
            return 0;
    }
}

/*
 * ------------------------------------------------------------
 * Public API (spectre_*)
 *
 * Every one of these now issues a real INT 0x80 via
 * do_syscall() instead of calling kernel logic directly.
 * This is the fix: previously these functions called
 * syscall_write()/fs_open()/etc. in-process, which meant
 * syscall_dispatch() and the whole isr80 path were defined
 * but never actually used. Now the interrupt genuinely fires
 * on every call.
 * ------------------------------------------------------------
 */

uint32_t spectre_write(const char* text)
{
    return do_syscall(
        SYS_WRITE,
        (uint32_t)text,
        0,
        0
    );
}

uint32_t spectre_read(
    char* buffer,
    uint32_t size
)
{
    return do_syscall(
        SYS_READ,
        (uint32_t)buffer,
        size,
        0
    );
}

uint32_t spectre_meminfo(memory_info_t* info)
{
    return do_syscall(
        SYS_MEMINFO,
        (uint32_t)info,
        0,
        0
    );
}

uint32_t spectre_hwinfo(cpu_info_t* info)
{
    return do_syscall(
        SYS_HWINFO,
        (uint32_t)info,
        0,
        0
    );
}

uint32_t spectre_open(const char* name)
{
    return do_syscall(
        SYS_OPEN,
        (uint32_t)name,
        0,
        0
    );
}

uint32_t spectre_read_file(
    int fd,
    void* buffer,
    uint32_t size
)
{
    return do_syscall(
        SYS_READ_FILE,
        (uint32_t)fd,
        (uint32_t)buffer,
        size
    );
}

uint32_t spectre_write_file(
    int fd,
    const void* buffer,
    uint32_t size
)
{
    return do_syscall(
        SYS_WRITE_FILE,
        (uint32_t)fd,
        (uint32_t)buffer,
        size
    );
}

uint32_t spectre_close(int fd)
{
    return do_syscall(
        SYS_CLOSE,
        (uint32_t)fd,
        0,
        0
    );
}

uint32_t spectre_create(const char* name)
{
    return do_syscall(
        SYS_CREATE,
        (uint32_t)name,
        0,
        0
    );
}

uint32_t spectre_delete(const char* name)
{
    return do_syscall(
        SYS_DELETE,
        (uint32_t)name,
        0,
        0
    );
}

uint32_t spectre_list(
    char* buffer,
    uint32_t size
)
{
    return do_syscall(
        SYS_LIST,
        (uint32_t)buffer,
        size,
        0
    );
}

/*
 * Kept as a no-op for ABI/API compatibility with anything that
 * may already call syscall_init() during boot.
 */
void syscall_init(void)
{
}

/*
 * Entry point used by interrupts.asm for INT 0x80.
 *
 * The assembly stub pushes the syscall number and arguments
 * before calling this function. Keep this ABI synchronized
 * with interrupts.asm.
 */
void syscall_handler(uint32_t* regs)
{
    if (regs == 0)
    {
        return;
    }

    /*
     * interrupts.asm executes:
     *
     *     pusha
     *     push esp
     *     call syscall_handler
     *
     * PUSHA layout:
     *
     * regs[0] = EDI
     * regs[1] = ESI
     * regs[2] = EBP
     * regs[3] = original ESP
     * regs[4] = EBX
     * regs[5] = EDX
     * regs[6] = ECX
     * regs[7] = EAX
     */

    uint32_t number = regs[7];
    uint32_t arg1   = regs[4];
    uint32_t arg2   = regs[6];
    uint32_t arg3   = regs[5];

    uint32_t result = syscall_dispatch(
        number,
        arg1,
        arg2,
        arg3
    );

    /*
     * POPA restores EAX from this location.
     */
    regs[7] = result;
}
