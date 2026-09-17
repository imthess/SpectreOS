# SpectreOS

SpectreOS is a 32-bit x86 operating-system project built from scratch, primarily in C with x86 assembly. It follows a Unix-inspired systems design and currently provides a lightweight command-line environment rather than a graphical desktop.

The repository is intentionally focused on operating-system fundamentals first: boot, CPU/interrupt setup, physical and virtual memory, storage, a persistent filesystem, keyboard/terminal input, threading, scheduling, synchronization, system calls, and early process/user-mode work.

> **Project status:** experimental / educational. SpectreOS is not a production-ready operating system and should not be treated as a security-hardened environment.

## Current implementation

### Kernel and CPU foundation

- Multiboot-compatible boot path.
- 32-bit x86 GDT setup with kernel and user descriptors.
- IDT and interrupt/exception handling.
- PIC remapping and PIT timer setup.
- TSS with a kernel stack for privilege transitions.

### Memory

- Multiboot-derived physical-memory detection.
- Physical-frame tracking/allocator with a 4 KiB frame size.
- Paging enabled with the current identity-mapped low-memory foundation.
- Early Ring 3 page permissions are present as groundwork; complete per-process address spaces and isolation are still future work.

### Storage and filesystem

- ATA PIO access for the primary IDE device using 28-bit LBA.
- 512-byte sector I/O.
- Persistent filesystem stored on the ATA disk image.
- Fixed filesystem metadata with superblock, bitmap, inode table, and root-directory storage.
- Basic file operations exposed through the shell and syscall layer.

### Threads, processes, and scheduling

- Fixed-size kernel thread table.
- Thread lifecycle states: unused, ready, running, blocked, terminated.
- Per-thread kernel stacks and saved stack pointers.
- Process table with PID, parent PID, entry point, user-stack metadata, and linked thread index.
- Early user-process/Ring 3 scaffolding.
- Scheduler policies: Round Robin, FCFS, and priority selection.
- Scheduler run/switch accounting.
- Spinlocks, mutexes, and semaphores with waiter tracking.

The process and Ring 3 components in this repository are a development milestone rather than a completed Unix-style userspace. The next major work is robust context switching, separate address spaces, safe user-memory validation, program loading, and reliable user-to-kernel transitions.

### System calls

SpectreOS uses `INT 0x80` as its current system-call entry point.

The documented ABI is:

```text
EAX = syscall number
EBX = argument 1
ECX = argument 2
EDX = argument 3
```

Defined syscall IDs include:

| ID | Name | Purpose |
| ---: | --- | --- |
| 1 | `SYS_WRITE` | Kernel text output path |
| 2 | `SYS_READ` | Reserved/early input interface |
| 3 | `SYS_EXIT` | Reserved/early process termination interface |
| 4 | `SYS_GETPID` | Return the current process ID |
| 5 | `SYS_HWINFO` | Return CPU information |
| 6 | `SYS_YIELD` | Early scheduler-yield path |
| 7 | `SYS_OPEN` | Open a filesystem object |
| 8 | `SYS_READ_FILE` | Read from an open file |
| 9 | `SYS_WRITE_FILE` | Write to an open file |
| 10 | `SYS_CLOSE` | Close an open file |
| 11 | `SYS_CREATE` | Create a file |
| 12 | `SYS_DELETE` | Delete a file |
| 13 | `SYS_LIST` | List filesystem entries |
| 14 | `SYS_MEMINFO` | Return memory information |

The syscall wrappers in `syscall.c` issue a real `INT 0x80`, so the current path is `int 0x80 -> isr80 -> syscall_handler -> syscall_dispatch`.

## Shell

The CLI currently includes:

```text
help
clear
echo
syscall
threads
hwinfo
meminfo
mem
ticks
ls
touch <name>
cat <name>
write <name> <text>
rm <name>
cp <src> <dst>
mv <src> <dst>
nano <name>
```

The keyboard path includes PS/2 scancode handling, Shift, Caps Lock, Ctrl/Alt state tracking, cursor/editing controls, shell history navigation, and Nano input routing.

## Build and run

The Makefile expects a 32-bit freestanding toolchain plus NASM, GNU `ld`, GRUB image tooling, and QEMU.

Build the ISO:

```bash
make clean
make
```

Run in QEMU:

```bash
make run
```

The default disk image is `disk.img`, and the ISO is generated at `iso/spectreos.iso`.

## Repository layout

```text
src/
├── boot/          boot assembly and Multiboot entry
├── include/       kernel subsystem interfaces
├── kernel/        kernel subsystems and hardware drivers
└── shell/         command-line shell

backup/            historical source snapshots
iso/               generated GRUB/ISO output
build/             generated object files and kernel image
linker.ld          kernel/user-program linker layout
Makefile           build and QEMU run rules
disk.img           local filesystem disk image
```

## Design direction

SpectreOS is being developed in layers. The immediate goal is a coherent small operating system rather than a collection of isolated demos. Higher-level features from the original SpectreOS concept—such as cryptographic user identity, username-based discovery, trusted relationships, communication, broader hardware support, and portable/privacy-oriented operation—are treated as later architectural work and are not represented as completed features in the current kernel.

## Security statement

SpectreOS currently has significant security limitations. There is no claim of production-grade isolation, secure authentication, encrypted storage, network security, or privacy-preserving operation. See [`SECURITY.md`](SECURITY.md) for the current threat model and hardening roadmap.

## Contributing

See [`CONTRIBUTING.md`](CONTRIBUTING.md) for build, coding, testing, and change-review expectations.

## License

SpectreOS is released under the **GNU General Public License v3.0 only (GPLv3)**. See [`LICENSE`](LICENSE).
