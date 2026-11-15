/* Shell commands that operate on files (ls, touch, rm, cat, write, cp, mv, nano). */

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


void shell_cmd_ls(void)
{
    static char list_buffer[4096];

    int result =
        fs_list(
            list_buffer,
            sizeof(list_buffer)
        );

    terminal_putchar('\n');

    if (result < 0)
    {
        terminal_write(
            "ls: filesystem error\n"
        );

        return;
    }

    if (result == 0)
    {
        terminal_write(
            "(empty)\n"
        );

        return;
    }

    terminal_write(
        list_buffer
    );
}


void shell_cmd_touch(
    const char* args
)
{
    char name[
        FS_MAX_FILENAME
    ];

    if (shell_get_token(&args, name, sizeof(name)) == 0)
    {
        terminal_write(
            "\ntouch: missing filename\n"
        );

        return;
    }

    args =
        shell_skip_spaces(args);

    if (*args != '\0')
    {
        terminal_write(
            "\ntouch: too many arguments\n"
        );

        return;
    }

    if (fs_exists(name))
    {
        terminal_write(
            "\nFile already exists.\n"
        );

        return;
    }

    if (fs_create(name) == 0)
    {
        terminal_write(
            "\ntouch: failed to create file\n"
        );

        return;
    }

    terminal_write(
        "\nFile created.\n"
    );
}


void shell_cmd_rm(
    const char* args
)
{
    char name[
        FS_MAX_FILENAME
    ];

    if (shell_get_token(&args, name, sizeof(name)) == 0)
    {
        terminal_write(
            "\nrm: missing filename\n"
        );

        return;
    }

    args =
        shell_skip_spaces(args);

    if (*args != '\0')
    {
        terminal_write(
            "\nrm: too many arguments\n"
        );

        return;
    }

    if (fs_exists(name) == 0)
    {
        terminal_write(
            "\nrm: file not found\n"
        );

        return;
    }

    if (fs_delete(name) == 0)
    {
        terminal_write(
            "\nrm: delete failed\n"
        );

        return;
    }

    terminal_write(
        "\nFile deleted.\n"
    );
}


void shell_cmd_cat(
    const char* args
)
{
    char name[
        FS_MAX_FILENAME
    ];

    if (shell_get_token(&args, name, sizeof(name)) == 0)
    {
        terminal_write(
            "\ncat: missing filename\n"
        );

        return;
    }

    args =
        shell_skip_spaces(args);

    if (*args != '\0')
    {
        terminal_write(
            "\ncat: too many arguments\n"
        );

        return;
    }

    int fd =
        fs_open(name);

    if (fd < 0)
    {
        terminal_write(
            "\ncat: file not found\n"
        );

        return;
    }

    terminal_putchar('\n');

    char buffer[512];

    while (1)
    {
        int count =
            fs_read(
                fd,
                buffer,
                sizeof(buffer)
            );

        if (count < 0)
        {
            terminal_write(
                "\ncat: read error\n"
            );

            break;
        }

        if (count == 0)
        {
            break;
        }

        for (int i = 0;
             i < count;
             i++)
        {
            terminal_putchar(
                buffer[i]
            );
        }
    }

    fs_close(fd);
}


void shell_cmd_write(
    const char* args
)
{
    char name[
        FS_MAX_FILENAME
    ];

    if (shell_get_token(&args, name, sizeof(name)) == 0)
    {
        terminal_write(
            "\nwrite: missing filename\n"
        );

        return;
    }

    args =
        shell_skip_spaces(args);

    if (fs_exists(name) == 0)
    {
        if (fs_create(name) == 0)
        {
            terminal_write(
                "\nwrite: could not create file\n"
            );

            return;
        }
    }

    int fd =
        fs_open(name);

    if (fd < 0)
    {
        terminal_write(
            "\nwrite: could not open file\n"
        );

        return;
    }

    uint32_t length = 0;

    while (args[length] &&
           length < FS_MAX_FILE_SIZE)
    {
        length++;
    }

    int written =
        fs_write(
            fd,
            args,
            length
        );

    fs_close(fd);

    if (written < 0 ||
        (uint32_t)written != length)
    {
        terminal_write(
            "\nwrite: write failed\n"
        );

        return;
    }

    terminal_write(
        "\nWrite successful.\n"
    );
}


void shell_cmd_cp(
    const char* args
)
{
    char src[
        FS_MAX_FILENAME
    ];

    char dst[
        FS_MAX_FILENAME
    ];

    if (shell_get_token(&args, src, sizeof(src)) == 0)
    {
        terminal_write(
            "\ncp: usage cp <src> <dst>\n"
        );

        return;
    }

    if (shell_get_token(&args, dst, sizeof(dst)) == 0)
    {
        terminal_write(
            "\ncp: usage cp <src> <dst>\n"
        );

        return;
    }

    args =
        shell_skip_spaces(args);

    if (*args != '\0')
    {
        terminal_write(
            "\ncp: too many arguments\n"
        );

        return;
    }

    if (fs_exists(src) == 0)
    {
        terminal_write(
            "\ncp: source file not found\n"
        );

        return;
    }

    if (fs_exists(dst))
    {
        terminal_write(
            "\ncp: destination already exists\n"
        );

        return;
    }

    int src_fd =
        fs_open(src);

    if (src_fd < 0)
    {
        terminal_write(
            "\ncp: could not open source\n"
        );

        return;
    }

    if (fs_create(dst) == 0)
    {
        fs_close(src_fd);

        terminal_write(
            "\ncp: could not create destination\n"
        );

        return;
    }

    int dst_fd =
        fs_open(dst);

    if (dst_fd < 0)
    {
        fs_close(src_fd);

        terminal_write(
            "\ncp: could not open destination\n"
        );

        return;
    }

    /*
     * Copy in fixed-size chunks rather than allocating a
     * FS_MAX_FILE_SIZE scratch buffer on the stack.
     */
    char chunk[512];
    int ok = 1;

    while (1)
    {
        int n =
            fs_read(
                src_fd,
                chunk,
                sizeof(chunk)
            );

        if (n < 0)
        {
            ok = 0;
            break;
        }

        if (n == 0)
        {
            break;
        }

        int w =
            fs_write(
                dst_fd,
                chunk,
                (uint32_t)n
            );

        if (w < 0 || w != n)
        {
            ok = 0;
            break;
        }
    }

    fs_close(src_fd);
    fs_close(dst_fd);

    if (!ok)
    {
        /*
         * Best-effort cleanup of the partially written copy.
         */
        fs_delete(dst);

        terminal_write(
            "\ncp: copy failed\n"
        );

        return;
    }

    terminal_write(
        "\nFile copied.\n"
    );
}


void shell_cmd_mv(
    const char* args
)
{
    char src[
        FS_MAX_FILENAME
    ];

    char dst[
        FS_MAX_FILENAME
    ];

    if (shell_get_token(&args, src, sizeof(src)) == 0)
    {
        terminal_write(
            "\nmv: usage mv <src> <dst>\n"
        );

        return;
    }

    if (shell_get_token(&args, dst, sizeof(dst)) == 0)
    {
        terminal_write(
            "\nmv: usage mv <src> <dst>\n"
        );

        return;
    }

    args =
        shell_skip_spaces(args);

    if (*args != '\0')
    {
        terminal_write(
            "\nmv: too many arguments\n"
        );

        return;
    }

    if (fs_exists(src) == 0)
    {
        terminal_write(
            "\nmv: source file not found\n"
        );

        return;
    }

    if (fs_exists(dst))
    {
        terminal_write(
            "\nmv: destination already exists\n"
        );

        return;
    }

    /*
     * This filesystem has no in-place rename, so mv is
     * implemented as copy-then-delete: the full contents of
     * `src` are read into `dst` and `src` is only removed once
     * that copy has fully succeeded.
     */
    int src_fd =
        fs_open(src);

    if (src_fd < 0)
    {
        terminal_write(
            "\nmv: could not open source\n"
        );

        return;
    }

    if (fs_create(dst) == 0)
    {
        fs_close(src_fd);

        terminal_write(
            "\nmv: could not create destination\n"
        );

        return;
    }

    int dst_fd =
        fs_open(dst);

    if (dst_fd < 0)
    {
        fs_close(src_fd);

        terminal_write(
            "\nmv: could not open destination\n"
        );

        return;
    }

    char chunk[512];
    int ok = 1;

    while (1)
    {
        int n =
            fs_read(
                src_fd,
                chunk,
                sizeof(chunk)
            );

        if (n < 0)
        {
            ok = 0;
            break;
        }

        if (n == 0)
        {
            break;
        }

        int w =
            fs_write(
                dst_fd,
                chunk,
                (uint32_t)n
            );

        if (w < 0 || w != n)
        {
            ok = 0;
            break;
        }
    }

    fs_close(src_fd);
    fs_close(dst_fd);

    if (!ok)
    {
        fs_delete(dst);

        terminal_write(
            "\nmv: move failed\n"
        );

        return;
    }

    if (fs_delete(src) == 0)
    {
        terminal_write(
            "\nmv: warning: copied but could not remove source\n"
        );

        return;
    }

    terminal_write(
        "\nFile moved.\n"
    );
}


void shell_cmd_nano(
    const char* args
)
{
    char name[
        FS_MAX_FILENAME
    ];

    if (shell_get_token(&args, name, sizeof(name)) == 0)
    {
        terminal_write(
            "\nnano: usage nano <filename>\n"
        );

        return;
    }

    args =
        shell_skip_spaces(args);

    if (*args != '\0')
    {
        terminal_write(
            "\nnano: too many arguments\n"
        );

        return;
    }

    nano_open(name);
}
