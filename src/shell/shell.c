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
#include "selftest.h"



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
            "  selftest - run kernel regression tests\n"
            "  ls      - list files\n"
            "  touch   - create a file\n"
            "  cat     - read a file\n"
            "  write   - write a file\n"
            "  rm      - delete a file\n"
            "  cp      - copy a file\n"
            "  mv      - move/rename a file\n"
            "  nano    - full-screen text editor (^O save, ^X exit)\n"
            "  meminfo - show actual memory information\n"
            "  pci     - list detected PCI devices\n"
            "  disks   - list registered block devices\n"
            "  crypttest - re-run AES-256-CTR self-test\n"
            "  whoami  - show current user and identity\n"
            "  logout  - end session, return to login\n"
            "  netinfo - show MAC/IP/gateway\n"
            "  ping <ip> - send an ICMP echo request\n"
            "  threadtest - create 2 threads, prove real "
            "timer-driven preemption\n"
            "  processtest - same proof via process.c's "
            "thin wrapper\n"
            "  ps      - list processes (PID/PPID/state)\n"
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
     * PCI device list.
     */
    if (string_equals(
            command_line,
            "pci"))
    {
        shell_cmd_pci();

        return;
    }

    /*
     * Block device list.
     */
    if (string_equals(
            command_line,
            "disks"))
    {
        shell_cmd_disks();

        return;
    }

    /*
     * Kernel regression tests (paging, syscall validation, RNG, sync).
     */
    if (string_equals(
            command_line,
            "selftest"))
    {
        selftest_run();

        return;
    }

    /*
     * Re-run the disk encryption self-test.
     */
    if (string_equals(
            command_line,
            "crypttest"))
    {
        shell_cmd_crypttest();

        return;
    }

    /*
     * Show current user + identity fingerprint.
     */
    if (string_equals(
            command_line,
            "whoami"))
    {
        shell_cmd_whoami();

        return;
    }

    /*
     * End the session and return to the login prompt.
     */
    if (string_equals(
            command_line,
            "logout"))
    {
        shell_cmd_logout();

        return;
    }

    /*
     * Network status.
     */
    if (string_equals(
            command_line,
            "netinfo"))
    {
        shell_cmd_netinfo();

        return;
    }

    /*
     * ICMP echo.
     */
    if (string_starts_with(
            command_line,
            "ping "))
    {
        shell_cmd_ping(
            command_line + 5
        );

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

            terminal_write("  switches=");

            terminal_write_uint(
                thread_get_switches(i)
            );

            terminal_write("  runs=");

            terminal_write_uint(
                thread_get_runs(i)
            );

            terminal_write("\n");
        }

        return;
    }

    /*
     * Multi-thread scheduler proof - see the note above
     * shell_cmd_threadtest() for why its output is asynchronous.
     */
    if (string_equals(
            command_line,
            "threadtest"))
    {
        shell_cmd_threadtest();

        return;
    }

    /*
     * Same proof, via the process.c wrapper instead of raw
     * threads.
     */
    if (string_equals(
            command_line,
            "processtest"))
    {
        shell_cmd_processtest();

        return;
    }

    /*
     * List processes (process.c's thin wrapper over thread.c).
     */
    if (string_equals(
            command_line,
            "ps"))
    {
        terminal_write("\nProcesses:\n");

        for (uint32_t i = 0; i < PROCESS_MAX; i++)
        {
            process_t* proc = process_get(i);

            if (proc == 0)
            {
                continue;
            }

            terminal_write("  PID ");
            terminal_write_uint(proc->pid);
            terminal_write("  PPID ");
            terminal_write_uint(proc->ppid);
            terminal_write("  state=");

            switch (process_get_state(proc->pid))
            {
                case PROCESS_READY:
                    terminal_write("READY");
                    break;

                case PROCESS_RUNNING:
                    terminal_write("RUNNING");
                    break;

                case PROCESS_BLOCKED:
                    terminal_write("BLOCKED");
                    break;

                case PROCESS_TERMINATED:
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
     * If the command just run was `logout`, auth_logout() has
     * already handed keyboard input back to the login gate (see
     * keyboard.c's dispatch_enter(), which checks
     * auth_login_active() before ever reaching here) and printed
     * its own "Username: " prompt. Redrawing the shell prompt now
     * would print a stray "spectre> " in the middle of that login
     * prompt - same class of bug the nano_active() check below
     * already guards against, just for a different mode switch.
     */
    if (auth_login_active())
    {
        command_length = 0;
        cursor_position = 0;
        history_position = -1;
        command_line[0] = '\0';

        return;
    }

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
