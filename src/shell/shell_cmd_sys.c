/* Shell commands for hardware, devices, identity and scheduler self-tests. */

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


void shell_print_memory_info(void)
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


void shell_cmd_hwinfo(void)
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


void shell_cmd_pci(void)
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


void shell_cmd_disks(void)
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


void shell_cmd_crypttest(void)
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


void shell_cmd_whoami(void)
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


void shell_cmd_logout(void)
{
    terminal_write(
        "\nLogging out...\n"
    );

    auth_logout();
}

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


void shell_cmd_threadtest(void)
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


void shell_cmd_processtest(void)
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
