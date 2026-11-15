#ifndef SPECTREOS_SHELL_INTERNAL_H
#define SPECTREOS_SHELL_INTERNAL_H

/*
 * Private interface between the shell's translation units:
 *
 *   shell.c          line editor, history, command dispatch
 *   shell_util.c     string/number helpers and argument parsing
 *   shell_cmd_fs.c   ls touch rm cat write cp mv nano
 *   shell_cmd_net.c  netinfo ping
 *   shell_cmd_sys.c  hwinfo pci disks crypttest whoami logout
 *                    threadtest processtest
 *
 * Nothing outside src/shell/ should include this header.
 */

#include <stdint.h>

/* --- util --- */
int string_equals(
    const char* a,
    const char* b
);

int string_starts_with(
    const char* text,
    const char* prefix
);

void shell_print_uint(uint32_t value);

void shell_write_uint(
    uint32_t value
);

const char* shell_skip_spaces(
    const char* p
);

int shell_get_token(
    const char** input,
    char* output,
    uint32_t output_size
);

void shell_write_hex16(
    uint16_t value
);

/* --- sys --- */
void shell_print_memory_info(void);

void shell_cmd_hwinfo(void);

void shell_cmd_pci(void);

void shell_cmd_disks(void);

void shell_cmd_crypttest(void);

void shell_cmd_whoami(void);

void shell_cmd_logout(void);

void shell_cmd_threadtest(void);

void shell_cmd_processtest(void);

/* --- net --- */
void shell_cmd_netinfo(void);

void shell_cmd_ping(const char* argument);

/* --- fs --- */
void shell_cmd_ls(void);

void shell_cmd_touch(
    const char* args
);

void shell_cmd_rm(
    const char* args
);

void shell_cmd_cat(
    const char* args
);

void shell_cmd_write(
    const char* args
);

void shell_cmd_cp(
    const char* args
);

void shell_cmd_mv(
    const char* args
);

void shell_cmd_nano(
    const char* args
);

#endif
