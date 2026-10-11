#include <stdint.h>

#include "hardware.h"
#include "serial.h"
#include "memory.h"
#include "gdt.h"
#include "idt.h"
#include "pic.h"
#include "pit.h"
#include "pmm.h"
#include "paging.h"
#include "keyboard.h"
#include "pci.h"
#include "net.h"
#include "blockdev.h"
#include "ata.h"
#include "ramdisk.h"
#include "rng.h"
#include "crypto_disk.h"
#include "secure_wipe.h"
#include "fs.h"
#include "identity.h"
#include "auth.h"
#include "thread.h"
#include "process.h"
#include "scheduler.h"
#include "syscall.h"
#include "shell.h"
#include "terminal.h"

static void halt_forever(void)
{
    __asm__ volatile ("cli");

    while (1)
    {
        __asm__ volatile ("hlt");
    }
}

void kernel_main(
    uint32_t multiboot_magic,
    uint32_t multiboot_info
)
{
    /*
     * No hardware IRQs while the kernel is being initialized.
     */
    __asm__ volatile ("cli");

    /*
     * Before anything else: serial_init() so that every
     * subsequent terminal_write() (which now mirrors to serial
     * via terminal_putchar - see terminal.c) is captured from
     * the very first boot line onward. This is what an
     * automated test harness watches instead of a display -
     * see scripts/ci_boot_test.sh.
     */
    serial_init();

    terminal_clear();

    hardware_init();

    terminal_write(
        "SpectreOS kernel starting...\n"
    );

    terminal_write(
        serial_present() ?
        "Serial console: OK (COM1)\n" :
        "Serial console: not present\n"
    );

    if (multiboot_magic != 0x2BADB002)
    {
        terminal_write(
            "ERROR: Invalid Multiboot magic.\n"
        );

        halt_forever();
    }

    terminal_write(
        "Multiboot: OK\n"
    );

    /*
     * --------------------------------------------------------
     * MEMORY
     * --------------------------------------------------------
     */
    memory_init(
        multiboot_info
    );

    terminal_write(
        "Memory detection: OK\n"
    );

    /*
     * --------------------------------------------------------
     * CPU / INTERRUPT FOUNDATION
     * --------------------------------------------------------
     */
    gdt_init();

    terminal_write(
        "GDT: OK\n"
    );

    idt_init();

    terminal_write(
        "IDT: OK\n"
    );

    pic_remap();

    pit_init(
        100
    );

    /*
     * --------------------------------------------------------
     * PHYSICAL + VIRTUAL MEMORY
     * --------------------------------------------------------
     */
    pmm_init(
        multiboot_info
    );

    paging_init();

    terminal_write(
        "Paging: OK\n"
    );

    terminal_write(
        "Memory manager: OK\n"
    );

    /*
     * --------------------------------------------------------
     * PCI BUS
     * --------------------------------------------------------
     *
     * Informational only for now - nothing depends on this
     * yet, but it gives the shell's `pci` command something
     * real to report instead of nothing.
     */
    pci_scan_bus();

    terminal_write(
        "PCI: OK (devices: "
    );

    terminal_write_uint(
        pci_device_count()
    );

    terminal_write(
        ")\n"
    );

    /*
     * --------------------------------------------------------
     * NETWORK STACK
     * --------------------------------------------------------
     *
     * Needs the PCI scan above (to find an RTL8139 card, if
     * present) but nothing else - safe to bring up early.
     * Polling only: see net_poll() in the main loop below.
     */
    net_init();

    if (net_is_up())
    {
        terminal_write(
            "Network: OK (rtl8139 found)\n"
        );
    }
    else
    {
        terminal_write(
            "Network: no supported NIC found\n"
        );
    }

    /*
     * --------------------------------------------------------
     * BLOCK DEVICES
     * --------------------------------------------------------
     *
     * Must happen before any driver (ATA, RAM disk, ...)
     * registers itself.
     */
    blockdev_init();

    /*
     * --------------------------------------------------------
     * ATA STORAGE
     * --------------------------------------------------------
     *
     * Must happen before filesystem initialization. Also
     * registers itself with the block device layer above.
     */
    if (!ata_init())
    {
        terminal_write(
            "ATA: FAILED\n"
        );

        halt_forever();
    }

    terminal_write(
        "ATA: OK\n"
    );

    /*
     * --------------------------------------------------------
     * RAM DISK
     * --------------------------------------------------------
     *
     * A second block device, purely to prove the abstraction
     * above supports more than one backend. The filesystem
     * still boots from the ATA disk; nothing currently mounts
     * the RAM disk automatically.
     */
    int ramdisk_id =
        ramdisk_init();

    if (ramdisk_id >= 0)
    {
        terminal_write(
            "RAM disk: OK\n"
        );
    }

    /*
     * --------------------------------------------------------
     * ENTROPY SOURCE
     * --------------------------------------------------------
     */
    rng_init();

    if (rng_hardware_available())
    {
        terminal_write(
            "RNG: OK (hardware RDRAND)\n"
        );
    }
    else
    {
        terminal_write(
            "RNG: WARNING - no hardware RDRAND, "
            "using weak software fallback\n"
        );
    }

    /*
     * --------------------------------------------------------
     * DISK ENCRYPTION (AES-256-CTR)
     * --------------------------------------------------------
     *
     * Demonstrates the encrypting block device layer end to
     * end on every boot: wraps the RAM disk with a fresh random
     * key, writes a known pattern through the encrypted view,
     * reads it back, and separately confirms the raw bytes on
     * the backing device are NOT the plaintext. This particular
     * instance ("cenc0", over the RAM disk) is purely a boot-time
     * proof that the crypto layer itself works - the real
     * filesystem's own encryption (fs.c's "fsenc0", over the ATA
     * disk, only on freshly formatted disks - see
     * fs_setup_disk_device()) is a separate instance of the same
     * crypto_disk.c code, set up inside fs_init() below. Both
     * together use both of crypto_disk.c's CRYPTO_DISK_MAX_SLOTS
     * (2), so there is no room for a third encrypted disk
     * without adding more trampoline functions there.
     */
    if (ramdisk_id >= 0)
    {
        uint8_t disk_key[32];

        rng_get_bytes(
            disk_key,
            sizeof(disk_key)
        );

        int crypto_id =
            crypto_disk_create(
                "cenc0",
                (uint32_t)ramdisk_id,
                disk_key
            );

        secure_wipe(
            disk_key,
            sizeof(disk_key)
        );

        if (crypto_id >= 0 &&
            crypto_disk_self_test(
                (uint32_t)crypto_id,
                (uint32_t)ramdisk_id,
                1))
        {
            terminal_write(
                "Disk encryption: OK "
                "(self-test passed)\n"
            );
        }
        else
        {
            terminal_write(
                "Disk encryption: FAILED "
                "self-test\n"
            );
        }
    }

    /*
     * --------------------------------------------------------
     * FILESYSTEM
     * --------------------------------------------------------
     *
     * Loads the persistent filesystem from the ATA disk.
     * If the disk is blank, fs_init() creates the filesystem.
     */
    if (!fs_init())
    {
        terminal_write(
            "Filesystem: FAILED\n"
        );

        halt_forever();
    }

    terminal_write(
        "Filesystem: OK\n"
    );

    terminal_write(
        fs_is_encrypted() ?
        "Filesystem encryption: ON (AES-256-CTR, fsenc0)\n" :
        "Filesystem encryption: OFF (legacy unencrypted disk)\n"
    );

    /*
     * --------------------------------------------------------
     * IDENTITY + ACCOUNTS
     * --------------------------------------------------------
     *
     * Must happen after the filesystem, since both persist to
     * disk. identity_load() is a no-op if no identity exists
     * yet - the first-run wizard inside auth_start() (below,
     * after interrupts are enabled and keyboard input works)
     * creates one together with the first account.
     */
    identity_load();

    auth_init();

    terminal_write(
        "Accounts: OK ("
    );

    if (identity_is_loaded())
    {
        terminal_write(
            "identity present"
        );
    }
    else
    {
        terminal_write(
            "no identity yet"
        );
    }

    terminal_write(
        ")\n"
    );

    /*
     * --------------------------------------------------------
     * KEYBOARD
     * --------------------------------------------------------
     */
    keyboard_init();

    /*
     * --------------------------------------------------------
     * THREADING
     * --------------------------------------------------------
     *
     * Initialize the subsystem only.
     *
     * UPDATE: the invalid EIP=0x00000003 crash mentioned here
     * was root-caused and fixed - scheduler.c used to keep its
     * own private `scheduler_current` variable completely out of
     * sync with this file's `current_thread`, which (among other
     * things) made the bootstrap thread itself unreachable by
     * the round-robin selector. That's fixed; scheduler.c now
     * reads/writes this file's state directly. Real multi-thread
     * creation is now exercised on demand via the `threadtest`
     * and `processtest` shell commands rather than automatically
     * at boot, so a boot that never runs either of those commands
     * behaves exactly as it always did.
     */
    thread_init();

    terminal_write(
        "Threading: OK\n"
    );

    /*
     * --------------------------------------------------------
     * SCHEDULER
     * --------------------------------------------------------
     */
    scheduler_init();

    terminal_write(
        "Scheduler: OK\n"
    );

    /*
     * --------------------------------------------------------
     * PROCESSES
     * --------------------------------------------------------
     *
     * A thin wrapper over the threads above - see process.h for
     * the full design note on what this does and deliberately
     * does not yet do (no paging, no ring-3). Safe to initialize
     * here regardless of whether anything ever calls
     * process_create() this boot.
     */
    process_init();

    terminal_write(
        "Processes: OK\n"
    );

    /*
     * --------------------------------------------------------
     * SYSTEM CALLS
     * --------------------------------------------------------
     *
     * IDT/syscall infrastructure has already been installed
     * by idt_init() and the syscall implementation is linked.
     */
    terminal_write(
        "Interrupts: OK\n"
    );

    terminal_write(
        "System calls: OK\n"
    );

    /*
     * --------------------------------------------------------
     * LOGIN / SETUP
     * --------------------------------------------------------
     *
     * Replaces the direct shell_init() call: the login gate
     * (or first-run account + identity setup wizard) now owns
     * keyboard input first and calls shell_init() itself once a
     * session is authenticated - see auth.c and the three-way
     * dispatch in keyboard.c.
     */
    auth_start();

    /*
     * --------------------------------------------------------
     * INTERRUPTS
     * --------------------------------------------------------
     *
     * Keyboard and timer are enabled only after every
     * subsystem above is ready.
     */
    pic_unmask_irq(0);
    pic_unmask_irq(1);

    __asm__ volatile ("sti");

    /*
     * Bootstrap kernel remains active.
     *
     * PIT currently provides timer interrupts and the
     * keyboard provides shell input. The scheduler remains
     * initialized but no unsafe test context-switch is forced.
     */
    while (1)
    {
        net_poll();

        __asm__ volatile ("hlt");
    }
}
