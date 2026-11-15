/* Shell helpers: string comparison, number printing, argument tokenising. */

#include <stdint.h>
#include "shell.h"
#include "terminal.h"
#include "syscall.h"
#include "thread.h"
#include "process.h"
#include "pmm.h"
#include "hardware.h"
#include "fs.h"
#include "pit.h"
#include "memory.h"
#include "nano.h"
#include "pci.h"
#include "blockdev.h"
#include "rng.h"
#include "crypto_disk.h"
#include "secure_wipe.h"
#include "identity.h"
#include "auth.h"
#include "net.h"
#include "net_config.h"
#include "arp.h"
#include "icmp.h"
#include "shell_internal.h"


int string_equals(
    const char* a,
    const char* b
)
{
    uint32_t i = 0;

    while (a[i] && b[i])
    {
        if (a[i] != b[i])
        {
            return 0;
        }

        i++;
    }

    return (
        a[i] == '\0' &&
        b[i] == '\0'
    );
}


int string_starts_with(
    const char* text,
    const char* prefix
)
{
    uint32_t i = 0;

    while (prefix[i])
    {
        if (text[i] != prefix[i])
        {
            return 0;
        }

        i++;
    }

    return 1;
}


void shell_print_uint(uint32_t value)
{
    char buffer[11];
    uint32_t i = 0;

    if (value == 0)
    {
        terminal_putchar('0');
        return;
    }

    while (value > 0)
    {
        buffer[i++] =
            '0' + (value % 10);

        value /= 10;
    }

    while (i > 0)
    {
        i--;
        terminal_putchar(buffer[i]);
    }
}




/*
 * NOTE: the previous shell_fs_ls / shell_fs_touch / shell_fs_rm /
 * shell_fs_cat / shell_fs_write helpers were dead code — never called
 * from shell_execute() — and shell_fs_write had a bug (it passed a
 * hardcoded length of 4096 to fs_write() instead of the real length
 * of `text`, causing an out-of-bounds read past the end of the
 * caller's string). They have been removed; shell_cmd_ls / _touch /
 * _rm / _cat / _write below are the versions actually wired into the
 * dispatcher and are unaffected.
 */

void shell_write_uint(
    uint32_t value
)
{
    char buffer[11];
    uint32_t i = 0;

    if (value == 0)
    {
        terminal_putchar('0');
        return;
    }

    while (value > 0 && i < sizeof(buffer))
    {
        buffer[i++] =
            (char)('0' + (value % 10));

        value /= 10;
    }

    while (i > 0)
    {
        terminal_putchar(
            buffer[--i]
        );
    }
}


const char* shell_skip_spaces(
    const char* p
)
{
    while (*p == ' ' ||
           *p == '\t')
    {
        p++;
    }

    return p;
}


int shell_get_token(
    const char** input,
    char* output,
    uint32_t output_size
)
{
    const char* p =
        shell_skip_spaces(*input);

    uint32_t length = 0;

    if (*p == '\0')
    {
        return 0;
    }

    while (*p &&
           *p != ' ' &&
           *p != '\t')
    {
        if (length + 1 >= output_size)
        {
            return 0;
        }

        output[length++] =
            *p++;

    }

    output[length] = '\0';

    *input = p;

    return 1;
}


void shell_write_hex16(
    uint16_t value
)
{
    static const char digits[] = "0123456789ABCDEF";

    terminal_write("0x");

    for (int shift = 12; shift >= 0; shift -= 4)
    {
        terminal_putchar(
            digits[(value >> shift) & 0xF]
        );
    }
}
