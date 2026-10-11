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

static void shell_write_hex16(
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

static void shell_cmd_pci(void)
{
    uint32_t count = pci_device_count();

    terminal_write(
        "\nPCI devices: "
    );

    shell_write_uint(count);

    terminal_write("\n");

    for (uint32_t i = 0; i < count; i++)
    {
        const pci_device_t* device =
            pci_get_device(i);

        if (device == 0)
        {
            continue;
        }

        terminal_write("  ");
        shell_write_uint(device->bus);
        terminal_write(":");
        shell_write_uint(device->device);
        terminal_write(".");
        shell_write_uint(device->function);
        terminal_write("  vendor=");
        shell_write_hex16(device->vendor_id);
        terminal_write(" device=");
        shell_write_hex16(device->device_id);
        terminal_write("  ");
        terminal_write(
            pci_class_name(device->class_code)
        );
        terminal_write("\n");
    }

    if (count == 0)
    {
        terminal_write(
            "  (none found - re-run pci scan or check hardware)\n"
        );
    }
}

static void shell_cmd_disks(void)
{
    uint32_t count = blockdev_count();

    terminal_write(
        "\nBlock devices: "
    );

    shell_write_uint(count);

    terminal_write("\n");

    for (uint32_t i = 0; i < count; i++)
    {
        const block_device_t* device =
            blockdev_get(i);

        if (device == 0)
        {
            continue;
        }

        terminal_write("  ");
        shell_write_uint(i);
        terminal_write(": ");
        terminal_write(device->name);

        terminal_write(
            blockdev_present(i) ?
            "  present  sectors=" :
            "  absent   sectors="
        );

        shell_write_uint(
            blockdev_sector_count(i)
        );

        terminal_write("\n");
    }
}

static int shell_find_device_by_name(
    const char* name
)
{
    uint32_t count = blockdev_count();

    for (uint32_t i = 0; i < count; i++)
    {
        const block_device_t* device =
            blockdev_get(i);

        if (device == 0)
        {
            continue;
        }

        if (string_equals(device->name, name))
        {
            return (int)i;
        }
    }

    return -1;
}

static void shell_cmd_crypttest(void)
{
    int ram_id =
        shell_find_device_by_name("ram0");

    int crypto_id =
        shell_find_device_by_name("cenc0");

    if (ram_id < 0 || crypto_id < 0)
    {
        terminal_write(
            "\ncrypttest: encrypted disk not "
            "available (check boot log)\n"
        );

        return;
    }

    terminal_write(
        "\nRe-running AES-256-CTR round-trip "
        "self-test on cenc0...\n"
    );

    if (crypto_disk_self_test(
            (uint32_t)crypto_id,
            (uint32_t)ram_id,
            2))
    {
        terminal_write(
            "Result: PASS (ciphertext on ram0 "
            "differs from plaintext; decrypted "
            "read-back matches)\n"
        );
    }
    else
    {
        terminal_write(
            "Result: FAIL\n"
        );
    }
}

static void shell_cmd_whoami(void)
{
    terminal_write(
        "\nUser: "
    );

    terminal_write(
        auth_current_username()
    );

    terminal_write("\n");

    if (identity_is_loaded())
    {
        char fingerprint[
            IDENTITY_ID_SIZE * 2 + 1
        ];

        identity_get_fingerprint_hex(
            fingerprint,
            sizeof(fingerprint)
        );

        terminal_write(
            "Identity fingerprint: "
        );

        terminal_write(fingerprint);

        terminal_write("\n");
    }
    else
    {
        terminal_write(
            "No cryptographic identity "
            "loaded.\n"
        );
    }
}

static void shell_cmd_logout(void)
{
    terminal_write(
        "\nLogging out...\n"
    );

    auth_logout();
}

static void shell_write_ip(uint32_t ip)
{
    shell_write_uint((ip >> 24) & 0xFF);
    terminal_write(".");
    shell_write_uint((ip >> 16) & 0xFF);
    terminal_write(".");
    shell_write_uint((ip >> 8) & 0xFF);
    terminal_write(".");
    shell_write_uint(ip & 0xFF);
}

static int shell_parse_ip(
    const char* text,
    uint32_t* out_ip
)
{
    uint32_t octets[4];
    uint32_t octet_index = 0;
    uint32_t value = 0;
    int have_digit = 0;

    for (uint32_t i = 0; ; i++)
    {
        char c = text[i];

        if (c >= '0' && c <= '9')
        {
            value = value * 10 + (uint32_t)(c - '0');
            have_digit = 1;

            if (value > 255)
            {
                return 0;
            }
        }
        else if (c == '.' || c == '\0')
        {
            if (!have_digit || octet_index >= 4)
            {
                return 0;
            }

            octets[octet_index] = value;
            octet_index++;
            value = 0;
            have_digit = 0;

            if (c == '\0')
            {
                break;
            }
        }
        else
        {
            return 0;
        }
    }

    if (octet_index != 4)
    {
        return 0;
    }

    *out_ip =
        net_make_ip(
            (uint8_t)octets[0],
            (uint8_t)octets[1],
            (uint8_t)octets[2],
            (uint8_t)octets[3]
        );

    return 1;
}

static void shell_cmd_netinfo(void)
{
    terminal_write("\n");

    if (!net_is_up())
    {
        terminal_write(
            "No network interface found.\n"
        );

        return;
    }

    uint8_t mac[ETH_ADDR_LEN];
    net_get_local_mac(mac);

    terminal_write("MAC:     ");

    for (uint32_t i = 0; i < ETH_ADDR_LEN; i++)
    {
        static const char digits[] = "0123456789abcdef";

        terminal_putchar(digits[(mac[i] >> 4) & 0xF]);
        terminal_putchar(digits[mac[i] & 0xF]);

        if (i < ETH_ADDR_LEN - 1)
        {
            terminal_write(":");
        }
    }

    terminal_write("\nIP:      ");
    shell_write_ip(net_get_local_ip());

    terminal_write("\nGateway: ");
    shell_write_ip(net_get_gateway_ip());

    terminal_write("\nNetmask: ");
    shell_write_ip(net_get_subnet_mask());

    terminal_write("\n");
}

/*
 * NOTE ON THE TIMEOUT MECHANISM: this used to compare
 * pit_get_ticks() against a tick count, but that hung the
 * entire system - confirmed by reading interrupts.asm: the IDT
 * gates are 32-bit *interrupt* gates (idt.c, flags 0x8E), which
 * means the CPU hardware disables all interrupts on entry and
 * doesn't restore them until that interrupt handler returns via
 * iret. Since the entire shell command dispatch chain (keyboard
 * IRQ -> dispatch_enter() -> shell_keyboard_enter() ->
 * shell_execute() -> here) runs synchronously *inside* the
 * keyboard's own interrupt handler, and nothing in
 * interrupts.asm ever re-enables interrupts mid-handler, the
 * PIT's own timer interrupt could never fire while this function
 * was running - so pit_get_ticks() never advanced, and a
 * "wait until N ticks have passed" loop waited forever.
 *
 * Fixed by counting plain loop iterations instead, the same
 * pattern already used for every bounded wait in rtl8139.c. This
 * trades precise timing (an iteration takes a different amount
 * of wall-clock time on different host CPUs) for something that
 * provably cannot hang regardless of interrupt state - which
 * matters far more for a "give up gracefully" timeout than exact
 * timing does. Untuned/approximate: if this returns too fast or
 * too slow in practice, adjust PING_MAX_ITERATIONS.
 */
#define PING_MAX_ITERATIONS  3000000u
#define PING_ARP_RETRY_EVERY 50000u

/*
 * ------------------------------------------------------------
 * MULTI-THREAD SCHEDULER PROOF
 *
 * Two worker threads that each print a few progress lines with
 * a real (if untuned) delay between them, so that completing
 * even one iteration takes noticeably longer than one PIT timer
 * tick. That delay matters: without it, a thread could run its
 * *entire* loop to completion inside a single scheduling slice,
 * and the two threads would simply execute back-to-back rather
 * than genuinely interleaved - which would look identical to
 * "the scheduler doesn't really preempt", even if it does.
 *
 * IMPORTANT: this command creates the threads and returns
 * immediately - it does NOT wait for them to run, and it can't:
 * shell commands execute synchronously inside the keyboard's own
 * interrupt handler (see the note on PING_MAX_ITERATIONS above),
 * and the PIT timer interrupt that actually drives
 * scheduler_tick() cannot fire until that handler returns. So
 * the two threads' output will appear *after* this command's own
 * prompt reappears - asynchronously, spontaneously, possibly
 * interleaved with whatever you type next. That's not a bug;
 * it's the actual proof the milestone asks for: switching driven
 * by a real timer interrupt, not by anything synchronous.
 * ------------------------------------------------------------
 */

#define THREADTEST_ITERATIONS   5
#define THREADTEST_SPIN_PER_ITER 5000000u

static void threadtest_spin(void)
{
    for (volatile uint32_t i = 0;
         i < THREADTEST_SPIN_PER_ITER;
         i++)
    {
    }
}

/*
 * Prints one progress line as a single atomic unit with respect
 * to timer preemption. Without this, two threads' own code (which
 * genuinely does run with interrupts enabled - that's what makes
 * real preemption possible at all) can each be mid-way through
 * their own terminal_write()/shell_write_uint() calls when the
 * timer interrupts one and switches to the other, producing
 * output like "[thread [thread A] iteration 5\nB] iteration 5\n" -
 * exactly what was observed in testing. No data was ever at risk
 * (nothing here touches shared kernel state), it only ever
 * affected readability of this demo's own output.
 *
 * Safe specifically because this is only ever called from a
 * thread's own entry-point code (the timer-driven switch path),
 * never from inside another interrupt handler's synchronous
 * execution (e.g. a shell command) - cli/sti here never races
 * with or nests inside the keyboard IRQ the way it would if this
 * pattern were applied to terminal_write() globally. See the
 * long note above PING_MAX_ITERATIONS for why synchronous
 * shell-command code runs with interrupts already disabled.
 */
static void threadtest_print_progress(
    const char* label,
    uint32_t iteration
)
{
    __asm__ volatile ("cli");

    terminal_write(label);
    terminal_write(" iteration ");
    shell_write_uint(iteration);
    terminal_write("\n");

    __asm__ volatile ("sti");
}

static void threadtest_worker_a(void)
{
    for (uint32_t i = 0; i < THREADTEST_ITERATIONS; i++)
    {
        threadtest_print_progress("[thread A]", i + 1);

        threadtest_spin();
    }

    terminal_write("[thread A] done\n");

    thread_exit();
}

static void threadtest_worker_b(void)
{
    for (uint32_t i = 0; i < THREADTEST_ITERATIONS; i++)
    {
        threadtest_print_progress("[thread B]", i + 1);

        threadtest_spin();
    }

    terminal_write("[thread B] done\n");

    thread_exit();
}

static void shell_cmd_threadtest(void)
{
    terminal_write(
        "\nCreating two test threads (A and B). Their\n"
        "output will appear on its own over the next few\n"
        "seconds, interleaved with whatever else happens -\n"
        "that's the point: it can only happen once this\n"
        "command returns and the timer interrupt is free to\n"
        "fire again. Run `threads` again in a moment:\n"
        "switches/runs > 0 for both, then eventually\n"
        "TERMINATED for both, is success.\n\n"
    );

    int id_a = thread_create(threadtest_worker_a);
    int id_b = thread_create(threadtest_worker_b);

    if (id_a < 0 || id_b < 0)
    {
        terminal_write(
            "Failed to create one or both threads "
            "(thread table full?).\n"
        );

        return;
    }

    terminal_write("Created thread A (id ");
    shell_write_uint((uint32_t)id_a);
    terminal_write(") and thread B (id ");
    shell_write_uint((uint32_t)id_b);
    terminal_write(").\n");
}

/*
 * ------------------------------------------------------------
 * PROCESS ABSTRACTION PROOF
 *
 * Same milestone as threadtest above, but exercised through
 * process.c's thin wrapper instead of calling thread_create()
 * directly - proving the wrapper adds real value (PID/PPID
 * bookkeeping, automatic fd cleanup) without breaking anything
 * the raw thread test already proved. Worker A opens a real
 * file and registers it via process_register_fd(), then RETURNS
 * NORMALLY (no thread_exit() call) specifically to exercise the
 * trampoline's automatic-cleanup-on-return path in process.c -
 * a process that calls thread_exit() directly instead would
 * bypass that cleanup, which is exactly why threadtest's workers
 * above (which do call thread_exit() directly) are a different,
 * simpler pair of functions rather than being reused here.
 * ------------------------------------------------------------
 */

static void processtest_worker_a(void)
{
    uint32_t pid = process_get_current_pid();

    if (!fs_exists("proctest_a.dat"))
    {
        fs_create("proctest_a.dat");
    }

    int fd = fs_open("proctest_a.dat");

    if (fd >= 0)
    {
        process_register_fd(pid, fd);
    }

    for (uint32_t i = 0; i < THREADTEST_ITERATIONS; i++)
    {
        __asm__ volatile ("cli");

        terminal_write("[process A, pid ");
        shell_write_uint(pid);
        terminal_write("] iteration ");
        shell_write_uint(i + 1);
        terminal_write("\n");

        __asm__ volatile ("sti");

        threadtest_spin();
    }

    terminal_write(
        "[process A] returning normally - "
        "automatic fd cleanup runs now\n"
    );

    /*
     * No thread_exit() call here - process_trampoline() (see
     * process.c) runs the fd cleanup and calls thread_exit()
     * itself once this function returns.
     */
}

static void processtest_worker_b(void)
{
    uint32_t pid = process_get_current_pid();

    for (uint32_t i = 0; i < THREADTEST_ITERATIONS; i++)
    {
        __asm__ volatile ("cli");

        terminal_write("[process B, pid ");
        shell_write_uint(pid);
        terminal_write("] iteration ");
        shell_write_uint(i + 1);
        terminal_write("\n");

        __asm__ volatile ("sti");

        threadtest_spin();
    }

    terminal_write("[process B] returning normally\n");
}

static void shell_cmd_processtest(void)
{
    terminal_write(
        "\nCreating two test processes (A and B) via "
        "process_create() rather than thread_create()\n"
        "directly. Same asynchronous-output behavior as "
        "`threadtest` - see `ps` in a moment for PID/\n"
        "PPID/state. Process A also opens a real file "
        "(proctest_a.dat) to exercise automatic fd\n"
        "cleanup on normal return.\n\n"
    );

    int pid_a =
        process_create(processtest_worker_a, 0);

    int pid_b =
        process_create(processtest_worker_b, 0);

    if (pid_a < 0 || pid_b < 0)
    {
        terminal_write(
            "Failed to create one or both processes "
            "(process table or thread table full?).\n"
        );

        return;
    }

    terminal_write("Created process A (pid ");
    shell_write_uint((uint32_t)pid_a);
    terminal_write(") and process B (pid ");
    shell_write_uint((uint32_t)pid_b);
    terminal_write(").\n");
}

static void shell_cmd_ping(const char* argument)
{
    uint32_t dest_ip;

    if (!net_is_up())
    {
        terminal_write(
            "\nNo network interface available.\n"
        );

        return;
    }

    if (!shell_parse_ip(argument, &dest_ip))
    {
        terminal_write(
            "\nUsage: ping a.b.c.d\n"
        );

        return;
    }

    terminal_write("\nPinging ");
    shell_write_ip(dest_ip);
    terminal_write(" ... ");

    static uint16_t ping_sequence = 0;
    ping_sequence++;

    /*
     * NOTE ON RETRYING THE SEND: the first attempt almost always
     * fails to actually transmit anything - ip_send() needs the
     * destination's MAC address, which is essentially never
     * cached yet on a fresh boot; it fires an ARP request and
     * returns 0 rather than blocking. The previous version of
     * this function called icmp_send_echo_request() exactly once
     * and never noticed that failure, then sat waiting forever
     * for a reply to a packet that was never sent. Now it keeps
     * retrying the send (throttled, so as not to flood ARP
     * requests) until one attempt actually succeeds, and only
     * then starts waiting for a reply - and never calls
     * icmp_send_echo_request() again after that point, since it
     * resets the "reply received" state on every call and would
     * otherwise erase a reply that arrived between retries.
     */
    int sent = 0;
    uint32_t iteration = 0;

    while (iteration < PING_MAX_ITERATIONS)
    {
        net_poll();

        if (!sent)
        {
            if ((iteration % PING_ARP_RETRY_EVERY) == 0 &&
                icmp_send_echo_request(
                    dest_ip, 0xBEEF, ping_sequence))
            {
                sent = 1;
            }
        }
        else if (icmp_reply_received(0xBEEF, ping_sequence))
        {
            terminal_write("reply received\n");

            return;
        }

        iteration++;
    }

    terminal_write(
        sent ?
        "timed out (sent, but no reply came back)\n" :
        "timed out (destination MAC never resolved via ARP - "
        "no response to any ARP request)\n"
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
