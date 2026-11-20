#include <stdint.h>

#include "syscall.h"
#include "hardware.h"
#include "memory.h"
#include "fs.h"
#include "paging.h"

static uint16_t syscall_cursor = 0;

/*
 * ------------------------------------------------------------
 * Pointer/length validation
 *
 * Every pointer a syscall receives is checked against the LIVE
 * page tables (paging_range_ok) before the kernel touches it,
 * so a bad pointer is rejected with an error return instead of
 * taking a page fault inside kernel code. Checks performed:
 *
 *   1. The pointer is not in the NULL-guard page.
 *   2. [ptr, ptr+len) does not wrap around the 32-bit space.
 *   3. Every page the range touches is present, and writable if
 *      the kernel will write through it.
 *   4. If the INT 0x80 came from ring 3 (CS RPL == 3, read from
 *      the interrupt frame in syscall_handler), every page must
 *      also carry the user bit, and the range must not overlap
 *      the kernel image. Ring-0 callers (the shell, which links
 *      against spectre_*) may pass kernel buffers.
 *   5. Strings are scanned page by page and must be NUL-
 *      terminated within a fixed bound; over-long input is
 *      rejected rather than silently truncated.
 *
 * Limitation (documented in SECURITY.md): there is still one
 * shared address space, so "valid for a user caller" currently
 * means "user-accessible page"; no page in the identity map
 * carries the user bit yet, so a ring-3 caller is rejected for
 * everything until per-process mappings exist. The checks are
 * written so that work needs no changes here.
 * ------------------------------------------------------------
 */

extern uint32_t _kernel_start;
extern uint32_t _kernel_end;

#define SYSCALL_MIN_VALID_ADDRESS 0x1000u
#define SYSCALL_MAX_STRING_LENGTH 4096u
#define SYSCALL_MAX_IO_SIZE       65536u

/* Set by syscall_handler() for the duration of one dispatch. */
static int syscall_caller_is_user = 0;

static int syscall_range_ok(
    const void* ptr,
    uint32_t length,
    int writable
)
{
    uint32_t address = (uint32_t)(uintptr_t)ptr;

    if (address < SYSCALL_MIN_VALID_ADDRESS)
    {
        return 0;
    }

    uint32_t required = PAGING_PRESENT;

    if (writable)
    {
        required |= PAGING_WRITE;
    }

    if (syscall_caller_is_user)
    {
        required |= PAGING_USER;
    }

    if (!paging_range_ok(address, length, required))
    {
        return 0;
    }

    if (syscall_caller_is_user && length > 0)
    {
        uint32_t start = (uint32_t)(uintptr_t)&_kernel_start;
        uint32_t end   = (uint32_t)(uintptr_t)&_kernel_end;
        uint32_t last  = address + (length - 1);

        if (address < end && last >= start)
        {
            return 0;
        }
    }

    return 1;
}

/*
 * Returns the string length (excluding NUL), or -1 if the
 * pointer is invalid, a page along the way is not accessible,
 * or no NUL appears within `max_length` bytes.
 */
static int32_t syscall_string_length(
    const char* text,
    uint32_t max_length
)
{
    uint32_t base = (uint32_t)(uintptr_t)text;

    for (uint32_t i = 0; i <= max_length; i++)
    {
        uint32_t address = base + i;

        if (i == 0 || (address & 0xFFFu) == 0)
        {
            if (!syscall_range_ok((const void*)(uintptr_t)address, 1, 0))
            {
                return -1;
            }
        }

        if (text[i] == '\0')
        {
            return (int32_t)i;
        }
    }

    return -1;
}

/*
 * Copies a validated, NUL-terminated string into a kernel
 * buffer so later code never re-reads user memory (no
 * check/use gap). Returns 1 on success, 0 on any failure
 * including a name that does not fit.
 */
static int syscall_copy_string(
    const char* source,
    char* destination,
    uint32_t destination_size
)
{
    int32_t length =
        syscall_string_length(source, destination_size - 1);

    if (length < 0)
    {
        return 0;
    }

    for (int32_t i = 0; i <= length; i++)
    {
        destination[i] = source[i];
    }

    return 1;
}

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
    if (!syscall_range_ok(info, sizeof(*info), 1))
    {
        return 0;
    }

    hardware_get_cpu_info(info);
    return 1;
}

static uint32_t syscall_meminfo(memory_info_t* info)
{
    if (!syscall_range_ok(info, sizeof(*info), 1))
    {
        return 0;
    }

    /*
     * memory_get_info() follows the kernel-internal convention
     * (0 = success, -1 = error). The syscall ABI reports 1 on
     * success and 0 on failure, like SYS_HWINFO, so a caller can
     * tell a bad pointer from a successful call.
     */
    return memory_get_info(info) == 0 ? 1u : 0u;
}

static uint32_t syscall_write(const char* text)
{
    int32_t length =
        syscall_string_length(
            text,
            SYSCALL_MAX_STRING_LENGTH
        );

    if (length < 0)
    {
        return 0;
    }

    volatile uint16_t* video =
        (volatile uint16_t*)0xB8000;

    for (int32_t i = 0; i < length; i++)
    {
        if (syscall_cursor >= 80 * 25)
            syscall_cursor = 0;

        video[syscall_cursor++] =
            (uint16_t)(0x07 << 8) |
            (uint8_t)text[i];
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
        {
            char name[FS_MAX_FILENAME + 1];

            if (!syscall_copy_string(
                    (const char*)arg1, name, sizeof(name)))
            {
                return (uint32_t)-1;
            }

            return (uint32_t)fs_open(name);
        }

        case SYS_READ_FILE:
            if (arg3 > SYSCALL_MAX_IO_SIZE ||
                !syscall_range_ok((void*)arg2, arg3, 1))
            {
                return (uint32_t)-1;
            }

            return (uint32_t)fs_read(
                (int)arg1,
                (void*)arg2,
                arg3
            );

        case SYS_WRITE_FILE:
            if (arg3 > SYSCALL_MAX_IO_SIZE ||
                !syscall_range_ok((const void*)arg2, arg3, 0))
            {
                return (uint32_t)-1;
            }

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
        {
            char name[FS_MAX_FILENAME + 1];

            if (!syscall_copy_string(
                    (const char*)arg1, name, sizeof(name)))
            {
                return 0;
            }

            return (uint32_t)fs_create(name);
        }

        case SYS_DELETE:
        {
            char name[FS_MAX_FILENAME + 1];

            if (!syscall_copy_string(
                    (const char*)arg1, name, sizeof(name)))
            {
                return 0;
            }

            return (uint32_t)fs_delete(name);
        }

        case SYS_LIST:
            if (arg2 > SYSCALL_MAX_IO_SIZE ||
                !syscall_range_ok((void*)arg1, arg2, 1))
            {
                return (uint32_t)-1;
            }

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
     *
     * The CPU's own interrupt frame sits directly above PUSHA:
     *
     * regs[8]  = EIP
     * regs[9]  = CS   (low 2 bits = caller's privilege level)
     * regs[10] = EFLAGS
     */

    syscall_caller_is_user = (regs[9] & 3u) == 3u;

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

    syscall_caller_is_user = 0;
}
