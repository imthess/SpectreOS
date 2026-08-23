#include <stdint.h>

#include "syscall.h"
#include "terminal.h"
#include "hardware.h"
#include "memory.h"



static uint32_t syscall_write(const char* text)
{
    if (text == 0)
    {
        return (uint32_t)-1;
    }

    terminal_write(text);

    return 0;
}

static uint32_t syscall_getpid(void)
{
    return 1;
}

static uint32_t syscall_hwinfo(
    cpu_info_t* info
)
{
    if (info == 0)
    {
        return (uint32_t)-1;
    }

    hardware_cpu_detect(info);

    return 0;
}

static uint32_t syscall_meminfo(
    memory_info_t* info
)
{
    return memory_get_info(info);
}

uint32_t syscall_dispatch(
    uint32_t syscall_number,
    uint32_t arg1,
    uint32_t arg2,
    uint32_t arg3
)
{
    (void)arg2;
    (void)arg3;

    switch (syscall_number)
    {
        case SYS_EXIT:
            return 0;

        case SYS_WRITE:
            return syscall_write(
                (const char*)arg1
            );

        case SYS_READ:
            return 0;

        case SYS_GETPID:
            return syscall_getpid();

        case SYS_YIELD:
            return 0;

        case SYS_HWINFO:
            return syscall_hwinfo(
                (cpu_info_t*)arg1
            );

        case SYS_MEMINFO:
            return syscall_meminfo(
                (memory_info_t*)arg1
            );

        default:
            return (uint32_t)-1;
    }
}

void syscall_handler(uint32_t* registers)
{

    
    /*
     * After:
     *
     * pusha
     *
     * the layout is:
     *
     * [0] EDI
     * [1] ESI
     * [2] EBP
     * [3] ESP
     * [4] EBX
     * [5] EDX
     * [6] ECX
     * [7] EAX
     */

    uint32_t syscall_number = registers[7];
    uint32_t arg1 = registers[4];
    uint32_t arg2 = registers[6];
    uint32_t arg3 = registers[5];

    uint32_t result =
        syscall_dispatch(
            syscall_number,
            arg1,
            arg2,
            arg3
        );

    /*
     * Return value → EAX
     */
    registers[7] = result;
}

uint32_t spectre_write(const char* text)
{
    uint32_t result;

    __asm__ volatile (
        "int $0x80"
        : "=a"(result)
        : "a"(SYS_WRITE),
          "b"(text)
        : "memory"
    );

    return result;
}
uint32_t spectre_hwinfo(
    cpu_info_t* info
)
{
    uint32_t result;

    __asm__ volatile (
        "mov $5, %%eax\n"
        "mov %1, %%ebx\n"
        "int $0x80\n"
        "mov %%eax, %0\n"
        : "=r"(result)
        : "r"(info)
        : "eax", "ebx"
    );

    return result;
}

uint32_t spectre_meminfo(
    memory_info_t* info
)
{
    uint32_t result;

    __asm__ volatile (
        "mov $6, %%eax\n"
        "mov %1, %%ebx\n"
        "int $0x80\n"
        "mov %%eax, %0\n"
        : "=r"(result)
        : "r"(info)
        : "eax", "ebx"
    );

    return result;
}