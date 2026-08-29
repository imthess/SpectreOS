#ifndef SPECTREOS_SYSCALL_H
#define SPECTREOS_SYSCALL_H

#include <stdint.h>
#include "hardware.h"
#include "memory.h"

/* Existing syscalls */
#define SYS_WRITE       1
#define SYS_READ        2
#define SYS_EXIT        3
#define SYS_GETPID      4
#define SYS_HWINFO      5
#define SYS_MEMINFO  14
#define SYS_YIELD       6

/* Filesystem syscalls */
#define SYS_OPEN        7
#define SYS_READ_FILE   8
#define SYS_WRITE_FILE  9
#define SYS_CLOSE       10
#define SYS_CREATE      11
#define SYS_DELETE      12
#define SYS_LIST        13

void syscall_init(void);

uint32_t syscall_dispatch(
    uint32_t number,
    uint32_t arg1,
    uint32_t arg2,
    uint32_t arg3
);

uint32_t spectre_write(const char* text);
uint32_t spectre_read(char* buffer, uint32_t size);
uint32_t spectre_hwinfo(cpu_info_t* info);

uint32_t spectre_meminfo(memory_info_t* info);

uint32_t spectre_open(const char* name);
uint32_t spectre_read_file(
    int fd,
    void* buffer,
    uint32_t size
);
uint32_t spectre_write_file(
    int fd,
    const void* buffer,
    uint32_t size
);
uint32_t spectre_close(int fd);
uint32_t spectre_create(const char* name);
uint32_t spectre_delete(const char* name);
uint32_t spectre_list(
    char* buffer,
    uint32_t size
);

#endif

/* Assembly ISR entry point */
void syscall_handler(uint32_t* regs);
