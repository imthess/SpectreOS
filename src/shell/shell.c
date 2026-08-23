#include <stdint.h>

#include "shell.h"
#include "terminal.h"
#include "syscall.h"
#include "pmm.h"
#include "hardware.h"
#include "memory.h"

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


static void shell_execute(void)
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
            "  hwinfo  - show actual CPU hardware information\n"
            "  meminfo - show actual memory information\n"
        );

        return;
    }
        if (string_equals(
            command_line,
            "hwinfo"))
    {
        shell_print_cpu_info();

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
