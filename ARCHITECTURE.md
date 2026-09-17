# SpectreOS Architecture

## 1. System model

SpectreOS is a small 32-bit x86 operating system with a monolithic kernel structure. Kernel subsystems are split into focused C modules, while the earliest boot and interrupt entry paths use x86 assembly.

The intended architecture is:

```text
BIOS/firmware
    │
    ▼
GRUB / Multiboot
    │
    ▼
boot.asm
    │
    ▼
kernel_main()
    │
    ├── CPU + interrupt foundation
    │     ├── GDT
    │     ├── IDT
    │     ├── PIC
    │     ├── PIT
    │     └── TSS
    │
    ├── Memory
    │     ├── Multiboot memory discovery
    │     ├── PMM
    │     └── Paging
    │
    ├── Storage
    │     ├── ATA PIO
    │     └── Persistent filesystem
    │
    ├── Execution
    │     ├── Threads
    │     ├── Processes
    │     ├── Scheduler
    │     └── Synchronization
    │
    ├── Kernel entry interfaces
    │     └── INT 0x80 syscalls
    │
    ├── Input/output
    │     ├── VGA terminal
    │     ├── PS/2 keyboard
    │     └── Shell / Nano
    │
    └── Early Ring 3/user-process groundwork
```

## 2. Boot and initialization order

`kernel_main()` disables interrupts during early initialization and brings the system up in dependency order.

The current sequence is approximately:

1. Clear the terminal and initialize basic hardware information.
2. Validate the Multiboot magic value.
3. Read Multiboot memory information.
4. Install the GDT.
5. Install the IDT.
6. Remap the PIC and configure the PIT.
7. Initialize physical-memory tracking.
8. Enable the current paging setup.
9. Detect and initialize ATA storage.
10. Load or format the persistent filesystem.
11. Initialize the keyboard subsystem.
12. Initialize threads.
13. Initialize the process table.
14. Initialize scheduler state.
15. Prepare the early user-process/Ring 3 prototype.
16. Initialize the shell.
17. Unmask keyboard/timer IRQs and enable interrupts.

This ordering prevents drivers and higher-level code from assuming that the subsystems they depend on are already active.

## 3. CPU and privilege foundation

### GDT

The GDT contains kernel and user code/data descriptors plus a TSS descriptor. The current selectors used by the kernel/user split are:

```text
0x08  kernel code
0x10  kernel data
0x1B  user code
0x23  user data
0x28  TSS
```

The TSS provides the Ring 0 stack used during privilege transitions. The interface `gdt_set_kernel_stack()` also allows the kernel-stack pointer to be updated later as process/thread management matures.

### IDT

The IDT contains exception and hardware IRQ gates, plus a user-callable `INT 0x80` gate. The syscall gate is configured with a descriptor privilege level that allows Ring 3 code to invoke it.

### Interrupt entry

The common assembly path saves general-purpose registers with `PUSHA`, calls the corresponding C handler, restores registers, and returns with `IRETD`.

The current syscall entry path is:

```text
user/kernel caller
      │
      └── INT 0x80
             │
             ▼
          isr80
             │
          PUSHA
             │
             ▼
      syscall_handler()
             │
             ▼
      syscall_dispatch()
             │
          POPA / IRETD
```

## 4. Memory subsystem

### Physical memory manager

The PMM tracks physical frames at 4 KiB granularity and has a configured tracking ceiling of 128 MiB. Multiboot information is used during initialization rather than fabricating a memory size.

### Paging

The current paging foundation creates a page directory and first page table and identity-maps the initial low-memory range. User-accessible page permissions have been introduced in the upper portion of that initial mapping as Ring 3 groundwork.

This is not yet a complete per-process virtual-memory system. Planned work includes independent address spaces, page-fault handling suitable for user processes, guarded user stacks, copy-on-write or explicit mapping APIs where appropriate, and user-pointer validation.

## 5. Storage stack

### ATA

The current driver uses legacy primary-channel IDE/ATA PIO operations. It supports IDENTIFY plus 28-bit LBA sector reads and writes using 512-byte sectors.

This is deliberately small and easy to reason about, but it is not a complete modern storage stack. Additional controllers, DMA, caching, error recovery, and broader device discovery are future work.

### Filesystem

The filesystem uses a fixed on-disk layout with:

```text
sector 0       superblock
sectors 1-32   allocation bitmap
sectors 33-96  inode table
sector 97+     data region
```

The current filesystem supports up to 128 files. Inodes use fixed-size 256-byte records, and each inode contains 32 direct block references. Open-file handles track inode identity and current file position.

This layout is intentionally simple and suitable for the current learning/OS-development stage. Journaling, crash recovery, permissions, directories beyond the current root model, larger files, and metadata concurrency are future work.

## 6. Threads, processes, and scheduling

### Threads

The kernel maintains a fixed array of thread control structures. Each thread has a state, saved stack pointer, counters, priority metadata, and a private 4 KiB kernel stack.

### Processes

The current process layer maps a process record to a thread. It stores a PID, parent PID, state, entry address, user-stack metadata, and thread index.

A small user program is linked into the kernel image for early Ring 3 experimentation. The process layer can create a user thread with a user code selector and an `IRETD` return frame. This establishes the mechanism needed for later user-space execution, but it is not yet a complete process loader or isolated multi-process environment.

### Scheduler

Three scheduling policies are implemented in the selection logic:

- Round Robin
- First-Come, First-Served (FCFS)
- Priority selection

The scheduler records runs and switches and can save the interrupted stack pointer. The current project intentionally treats context switching as an area for continued hardening rather than claiming a finished Unix-style process scheduler.

## 7. Synchronization

The synchronization layer currently provides:

- spinlocks
- mutexes with owner tracking
- counting semaphores with bounded waiter storage

The primitives are designed for low-level kernel use. A complete scheduler-aware blocking/wakeup model, interrupt-context rules, deadlock analysis, priority inheritance where needed, and broader stress testing remain future work.

## 8. Syscall layer

`syscall.c` separates the public wrapper API from the kernel-side dispatcher. Public `spectre_*` functions issue a real `INT 0x80` instruction with a small register-based ABI.

The current syscall surface covers early terminal output, process ID, memory/CPU information, file operations, and basic control interfaces. Pointer validation and strict user/kernel buffer ownership are not complete, so the interface must currently be treated as an experimental kernel API rather than a hardened userspace ABI.

## 9. Terminal, keyboard, and shell

The terminal writes directly to VGA text memory at `0xB8000` and updates the VGA hardware cursor. The keyboard driver reads PS/2 scancodes from the keyboard controller and handles modifiers, editing keys, history navigation, and routing between the shell and Nano editor.

The shell is intentionally small. It provides diagnostics, filesystem commands, process/thread visibility, system-call testing, and a full-screen text editor without requiring a graphical subsystem.

## 10. Architectural priorities

The next architectural upgrades should preserve clear boundaries between:

```text
hardware → kernel subsystems → process/thread layer → syscall boundary → user programs → higher-level SpectreOS services
```

In particular, higher-level identity, networking, communication, and decentralized-account features should be built on top of a stable userspace and security model rather than being coupled directly to early boot code.
