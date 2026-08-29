#include <stdint.h>

#include "shell.h"
#include "terminal.h"
#include "syscall.h"
#include "thread.h"
#include "pmm.h"
#include "hardware.h"
#include "fs.h"
#include "pit.h"
#include "memory.h"
#include "nano.h"

#define SHELL_MAX_LINE       128
#define SHELL_HISTORY_SIZE   16
#define SHELL_PROMPT         "spectre> "

static char command_line[SHELL_MAX_LINE];
static uint32_t command_length = 0;
static uint32_t cursor_position = 0;

static char history[
    SHELL_HISTORY_SIZE
][SHELL_MAX_LINE];

static uint32_t history_count = 0;
static int32_t history_position = -1;

static uint8_t prompt_x = 0;
static uint8_t prompt_y = 0;

static void shell_clear_command_area(void)
{
    uint8_t x = prompt_x;
    uint8_t y = prompt_y;

    for (uint32_t i = 0;
         i < SHELL_MAX_LINE;
         i++)
    {
        if (x >= 80)
        {
            x = 0;
            y++;

            if (y >= 25)
            {
                break;
            }
        }

        terminal_put_at(
            x,
            y,
            ' '
        );

        x++;
    }
}

static void shell_redraw(void)
{
    shell_clear_command_area();

    uint8_t x = prompt_x;
    uint8_t y = prompt_y;

    for (uint32_t i = 0;
         i < command_length;
         i++)
    {
        if (x >= 80)
        {
            x = 0;
            y++;

            if (y >= 25)
            {
                break;
            }
        }

        terminal_put_at(
            x,
            y,
            command_line[i]
        );

        x++;
    }

    /*
     * Calculate cursor location.
     */
    x = prompt_x;
    y = prompt_y;

    for (uint32_t i = 0;
         i < cursor_position;
         i++)
    {
        x++;

        if (x >= 80)
        {
            x = 0;
            y++;
        }
    }

    terminal_set_cursor(x, y);
}

static void shell_save_history(void)
{
    if (command_length == 0)
    {
        return;
    }

    /*
     * Don't save the same command twice consecutively.
     */
    if (history_count > 0)
    {
        uint32_t last =
            history_count - 1;

        int same = 1;

        for (uint32_t i = 0;
             i < SHELL_MAX_LINE;
             i++)
        {
            if (history[last][i] != command_line[i])
            {
                same = 0;
                break;
            }

            if (command_line[i] == '\0')
            {
                break;
            }
        }

        if (same)
        {
            return;
        }
    }

    if (history_count < SHELL_HISTORY_SIZE)
    {
        for (uint32_t i = 0;
             i < SHELL_MAX_LINE;
             i++)
        {
            history[history_count][i] =
                command_line[i];

            if (command_line[i] == '\0')
            {
                break;
            }
        }

        history_count++;
    }
    else
    {
        /*
         * Move old commands upward.
         */
        for (uint32_t h = 1;
             h < SHELL_HISTORY_SIZE;
             h++)
        {
            for (uint32_t i = 0;
                 i < SHELL_MAX_LINE;
                 i++)
            {
                history[h - 1][i] =
                    history[h][i];
            }
        }

        for (uint32_t i = 0;
             i < SHELL_MAX_LINE;
             i++)
        {
            history[
                SHELL_HISTORY_SIZE - 1
            ][i] = command_line[i];
        }
    }
}

static void shell_load_history(int32_t position)
{
    if (history_count == 0)
    {
        return;
    }

    if (position < 0)
    {
        position = 0;
    }

    if ((uint32_t)position >= history_count)
    {
        position =
            (int32_t)history_count - 1;
    }

    history_position = position;

    uint32_t i = 0;

    while (
        i < SHELL_MAX_LINE - 1 &&
        history[position][i] != '\0'
    )
    {
        command_line[i] =
            history[position][i];

        i++;
    }

    command_line[i] = '\0';

    command_length = i;
    cursor_position = i;

    shell_redraw();
}

static int string_equals(
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

static int string_starts_with(
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

static void shell_print_uint(uint32_t value)
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

static void shell_print_cpu_info(void)
{
    cpu_info_t info;

    uint32_t result =
        spectre_hwinfo(&info);

    if (result != 0)
    {
        terminal_write(
            "\nHardware information unavailable.\n"
        );

        return;
    }

    terminal_write(
        "\nCPU Vendor: "
    );

    terminal_write(
        info.vendor
    );

    terminal_write(
        "\nCPU Brand: "
    );

    terminal_write(
        info.brand
    );

    terminal_write(
        "\nCPU Family: "
    );

    shell_print_uint(
        info.family
    );

    terminal_write(
        "\nCPU Model: "
    );

    shell_print_uint(
        info.model
    );

    terminal_write(
        "\nCPU Stepping: "
    );

    shell_print_uint(
        info.stepping
    );

    terminal_write(
        "\n"
    );
}

static void shell_print_memory_info(void)
{
    memory_info_t info;

    uint32_t result =
        spectre_meminfo(&info);

    if (result != 0)
    {
        terminal_write(
            "\nMemory information unavailable.\n"
        );

        return;
    }

    if (!info.multiboot_memory_available)
    {
        terminal_write(
            "\nMultiboot memory information unavailable.\n"
        );

        return;
    }

    terminal_write(
        "\nLower memory: "
    );

    shell_print_uint(
        info.lower_memory_kb
    );

    terminal_write(
        " KB\n"
    );

    terminal_write(
        "Upper memory: "
    );

    shell_print_uint(
        info.upper_memory_kb
    );

    terminal_write(
        " KB\n"
    );

    terminal_write(
        "Total memory: "
    );

    shell_print_uint(
        info.total_memory_kb
    );

    terminal_write(
        " KB\n"
    );

    terminal_write(
        "Total memory: "
    );

    shell_print_uint(
        info.total_memory_mb
    );

    terminal_write(
        " MB\n"
    );
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

static void shell_write_uint(
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

static const char* shell_skip_spaces(
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

static int shell_get_token(
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

static void shell_command_prompt(void)
{
    terminal_write(
        SHELL_PROMPT
    );

    prompt_x =
        terminal_get_cursor_x();

    prompt_y =
        terminal_get_cursor_y();

    command_length = 0;
    cursor_position = 0;
    history_position = -1;
}

static void shell_cmd_hwinfo(void)
{
    cpu_info_t info;

    hardware_get_cpu_info(
        &info
    );

    terminal_write(
        "\nHardware information:\n"
    );

    if (hardware_available() == 0)
    {
        terminal_write(
            "  CPUID: unavailable\n"
        );

        return;
    }

    terminal_write(
        "  CPUID: available\n"
        "  Vendor: "
    );

    terminal_write(
        info.vendor
    );

    terminal_write(
        "\n  Brand: "
    );

    if (info.brand[0] != '\0')
    {
        terminal_write(
            info.brand
        );
    }
    else
    {
        terminal_write(
            "Unavailable"
        );
    }

    terminal_write(
        "\n  Family: "
    );

    shell_write_uint(
        info.family
    );

    terminal_write(
        "\n  Model: "
    );

    shell_write_uint(
        info.model
    );

    terminal_write(
        "\n  Stepping: "
    );

    shell_write_uint(
        info.stepping
    );

    terminal_write(
        "\n  Max CPUID leaf: "
    );

    shell_write_uint(
        hardware_get_cpuid_max()
    );

    terminal_write(
        "\n"
    );
}

static void shell_cmd_ls(void)
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

static void shell_cmd_touch(
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

static void shell_cmd_rm(
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

static void shell_cmd_cat(
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

static void shell_cmd_write(
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

static void shell_cmd_cp(
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

static void shell_cmd_mv(
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

static void shell_cmd_nano(
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

void shell_execute(void)
{
    command_line[command_length] = '\0';

    shell_save_history();

    /*
     * Built-in clear.
     */
    if (string_equals(
            command_line,
            "clear"))
    {
        terminal_clear();

        terminal_write(
            SHELL_PROMPT
        );

        prompt_x =
            terminal_get_cursor_x();

        prompt_y =
            terminal_get_cursor_y();

        command_length = 0;
        cursor_position = 0;
        history_position = -1;

        return;
    }

    /*
     * Built-in help.
     */
    if (string_equals(
            command_line,
            "help"))
    {
        terminal_write(
            "\n"
            "SpectreOS commands:\n"
            "  help    - show this help\n"
            "  clear   - clear the terminal\n"
            "  echo    - print text\n"
            "  syscall - test system calls\n"
            "  threads - show thread states\n"
            "  hwinfo  - show CPU hardware information\n"
            "  ls      - list files\n"
            "  touch   - create a file\n"
            "  cat     - read a file\n"
            "  write   - write a file\n"
            "  rm      - delete a file\n"
            "  cp      - copy a file\n"
            "  mv      - move/rename a file\n"
            "  nano    - full-screen text editor (^O save, ^X exit)\n"
            "  meminfo - show actual memory information\n"
        );

        return;
    }
        

        if (string_equals(
            command_line,
            "meminfo"))
    {
        shell_print_memory_info();

        return;
    }

    /*
     * Hardware info.
     */
    if (string_equals(
            command_line,
            "hwinfo"))
    {
        shell_cmd_hwinfo();

        return;
    }

    /*
     * List files.
     */
    if (string_equals(
            command_line,
            "ls"))
    {
        shell_cmd_ls();

        return;
    }

    /*
     * Create a file.
     */
    if (string_starts_with(
            command_line,
            "touch "))
    {
        shell_cmd_touch(
            command_line + 6
        );

        return;
    }

    /*
     * Delete a file.
     */
    if (string_starts_with(
            command_line,
            "rm "))
    {
        shell_cmd_rm(
            command_line + 3
        );

        return;
    }

    /*
     * Read a file.
     */
    if (string_starts_with(
            command_line,
            "cat "))
    {
        shell_cmd_cat(
            command_line + 4
        );

        return;
    }

    /*
     * Write a file.
     */
    if (string_starts_with(
            command_line,
            "write "))
    {
        shell_cmd_write(
            command_line + 6
        );

        return;
    }

    /*
     * Copy a file.
     */
    if (string_starts_with(
            command_line,
            "cp "))
    {
        shell_cmd_cp(
            command_line + 3
        );

        return;
    }

    /*
     * Move (rename) a file.
     */
    if (string_starts_with(
            command_line,
            "mv "))
    {
        shell_cmd_mv(
            command_line + 3
        );

        return;
    }

    /*
     * Full-screen text editor.
     */
    if (string_starts_with(
            command_line,
            "nano "))
    {
        shell_cmd_nano(
            command_line + 5
        );

        return;
    }

    /*
     * Echo.
     */
    if (string_starts_with(
            command_line,
            "echo "))
    {
        terminal_write("\n");
        terminal_write(
            command_line + 5
        );

        return;
    }

    /*
     * System call test.
     */
    if (string_equals(
            command_line,
            "syscall"))
    {
        terminal_write(
            "\nBefore syscall\n"
        );

        spectre_write(
            "Hello from a SpectreOS system call!"
        );

        terminal_write(
            "\nAfter syscall\n"
        );

        return;
    }

    /*
 * Physical memory information.
 */
if (string_equals(
        command_line,
        "mem"))
{
    terminal_write(
        "\nPhysical memory:\n"
    );

    terminal_write(
        "  Total frames: "
    );

    /*
     * Temporary numeric output will be added
     * properly in the next shell/libc stage.
     */
    terminal_write(
        "32768\n"
    );

    terminal_write(
        "  Frame size: 4096 bytes\n"
    );

    terminal_write(
        "  Total tracked memory: 128 MiB\n"
    );

    return;
}

    if (string_equals(
        command_line,
        "ticks"))
{
    uint32_t ticks = pit_get_ticks();

    terminal_write("\nPIT ticks: ");

    char buffer[16];
    int i = 0;

    if (ticks == 0)
    {
        buffer[i++] = '0';
    }
    else
    {
        char reversed[16];
        int j = 0;

        while (ticks > 0)
        {
            reversed[j++] =
                '0' + (ticks % 10);

            ticks /= 10;
        }

        while (j > 0)
        {
            buffer[i++] =
                reversed[--j];
        }
    }

    buffer[i] = '\0';

    terminal_write(buffer);

    return;
}

    if (string_equals(
            command_line,
            "threads"))
    {
        terminal_write(
            "\nThreads:\n"
        );

        for (uint32_t i = 0;
             i < THREAD_MAX;
             i++)
        {
            thread_t* thread =
                thread_get(i);

            if (thread == 0)
            {
                continue;
            }

            terminal_write("  ID ");

            terminal_write_uint(
                thread->id
            );

            terminal_write(": ");

            switch (thread->state)
            {
                case THREAD_READY:
                    terminal_write("READY");
                    break;

                case THREAD_RUNNING:
                    terminal_write("RUNNING");
                    break;

                case THREAD_BLOCKED:
                    terminal_write("BLOCKED");
                    break;

                case THREAD_TERMINATED:
                    terminal_write("TERMINATED");
                    break;

                default:
                    terminal_write("UNKNOWN");
                    break;
            }

            terminal_write("\n");
        }

        return;
    }

    if (command_length != 0)
    {
        terminal_write(
            "\nUnknown command: "
        );

        terminal_write(
            command_line
        );
    }
}

void shell_init(void)
{
    command_length = 0;
    cursor_position = 0;
    history_position = -1;
    history_count = 0;

    terminal_write(
        SHELL_PROMPT
    );

    prompt_x =
        terminal_get_cursor_x();

    prompt_y =
        terminal_get_cursor_y();

    terminal_update_cursor();
}

void shell_keyboard_character(char c)
{
    if (c < 32)
    {
        return;
    }

    if (command_length >=
        SHELL_MAX_LINE - 1)
    {
        return;
    }

    /*
     * Move everything after the cursor
     * one position to the right.
     */
    for (uint32_t i = command_length;
         i > cursor_position;
         i--)
    {
        command_line[i] =
            command_line[i - 1];
    }

    command_line[cursor_position] = c;

    command_length++;
    cursor_position++;

    history_position = -1;

    shell_redraw();
}

void shell_keyboard_backspace(void)
{
    /*
     * Never delete before the command cursor.
     */
    if (cursor_position == 0)
    {
        return;
    }

    for (uint32_t i = cursor_position - 1;
         i < command_length - 1;
         i++)
    {
        command_line[i] =
            command_line[i + 1];
    }

    command_length--;
    cursor_position--;

    command_line[command_length] =
        '\0';

    history_position = -1;

    shell_redraw();
}

void shell_keyboard_delete(void)
{
    if (cursor_position >= command_length)
    {
        return;
    }

    for (uint32_t i = cursor_position;
         i < command_length - 1;
         i++)
    {
        command_line[i] =
            command_line[i + 1];
    }

    command_length--;

    command_line[command_length] =
        '\0';

    history_position = -1;

    shell_redraw();
}

void shell_keyboard_left(void)
{
    if (cursor_position > 0)
    {
        cursor_position--;

        shell_redraw();
    }
}

void shell_keyboard_right(void)
{
    if (cursor_position < command_length)
    {
        cursor_position++;

        shell_redraw();
    }
}

void shell_keyboard_home(void)
{
    cursor_position = 0;

    shell_redraw();
}

void shell_keyboard_end(void)
{
    cursor_position =
        command_length;

    shell_redraw();
}

void shell_keyboard_up(void)
{
    if (history_count == 0)
    {
        return;
    }

    if (history_position == -1)
    {
        history_position =
            (int32_t)history_count - 1;
    }
    else if (history_position > 0)
    {
        history_position--;
    }

    shell_load_history(
        history_position
    );
}

void shell_keyboard_down(void)
{
    if (history_count == 0)
    {
        return;
    }

    if (history_position == -1)
    {
        return;
    }

    if (
        history_position <
        (int32_t)history_count - 1
    )
    {
        history_position++;

        shell_load_history(
            history_position
        );

        return;
    }

    /*
     * Going below the newest command
     * gives us a fresh command line.
     */
    history_position = -1;

    command_length = 0;
    cursor_position = 0;
    command_line[0] = '\0';

    shell_redraw();
}

void shell_keyboard_enter(void)
{
    command_line[command_length] =
        '\0';

    shell_execute();

    /*
     * If the command just run was `nano ...`, nano_open() has
     * already taken over the screen and the keyboard (see
     * keyboard.c's dispatch_enter(), which stops routing Enter
     * here at all once nano is active). Redrawing the shell
     * prompt now would immediately overwrite nano's UI, so skip
     * it; the prompt is redrawn once nano exits instead (see
     * shell_resume_prompt(), called from nano on Ctrl+X).
     */
    if (nano_active())
    {
        command_length = 0;
        cursor_position = 0;
        history_position = -1;
        command_line[0] = '\0';

        return;
    }

    terminal_putchar('\n');

    terminal_write(
        SHELL_PROMPT
    );

    prompt_x =
        terminal_get_cursor_x();

    prompt_y =
        terminal_get_cursor_y();

    command_length = 0;
    cursor_position = 0;
    history_position = -1;

    command_line[0] = '\0';

    terminal_update_cursor();
}

void shell_resume_prompt(void)
{
    terminal_write(
        SHELL_PROMPT
    );

    prompt_x =
        terminal_get_cursor_x();

    prompt_y =
        terminal_get_cursor_y();

    terminal_update_cursor();
}
