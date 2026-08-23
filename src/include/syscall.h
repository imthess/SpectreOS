#ifndef SPECTREOS_SYSCALL_H
#define SPECTREOS_SYSCALL_H

#include <stdint.h>
#include "hardware.h"
#include "memory.h"

#define SYS_EXIT   0
#define SYS_WRITE  1
#define SYS_READ   2
#define SYS_GETPID 3
#define SYS_YIELD  4
#define SYS_HWINFO 5
#define SYS_MEMINFO 6

#define SPECTRE_SYSCALL_MAX 7


uint32_t spectre_write(const char* text);

uint32_t spectre_hwinfo(cpu_info_t* info);

uint32_t spectre_meminfo(memory_info_t* info);

uint32_t syscall_dispatch(
    uint32_t syscall_number,
    uint32_t arg1,
    uint32_t arg2,
    uint32_t arg3
);

void syscall_handler(uint32_t* registers);

/*
 * User-facing/kernel syscall interface.
 */
uint32_t spectre_write(
    const char* text
);

#endif