# SpectreOS

SpectreOS is a 32-bit x86 operating-system project built from scratch, primarily in C with x86 assembly. It follows a Unix-inspired systems design and currently provides a lightweight command-line environment rather than a graphical desktop.

The repository is intentionally focused on operating-system fundamentals first: boot, CPU/interrupt setup, physical and virtual memory, storage, a persistent filesystem, keyboard/terminal input, threading, scheduling, synchronization, system calls, and early process/user-mode work.

<<<<<<< HEAD
> **Project status:** experimental / educational. SpectreOS boots and runs in QEMU, and every feature listed below is exercised by an automated test (see [Testing](#testing)). It is not a production operating system and should not be treated as a security-hardened environment — read [Security statement](#security-statement) for exactly what is and is not protected.
=======
> **Project status:** experimental / educational. SpectreOS is not a production-ready operating system and should not be treated as a security-hardened environment.
>>>>>>> origin/main

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
<<<<<<< HEAD
- No page currently carries the user bit, so ring-3 code cannot run yet; per-process address spaces and isolation are future work. The syscall validator already enforces user-bit and kernel-image rules for ring-3 callers so that work needs no changes there.
=======
- Early Ring 3 page permissions are present as groundwork; complete per-process address spaces and isolation are still future work.
>>>>>>> origin/main

### Storage and filesystem

- ATA PIO access for the primary IDE device using 28-bit LBA.
- 512-byte sector I/O.
- Persistent filesystem stored on the ATA disk image.
- Fixed filesystem metadata with superblock, bitmap, inode table, and root-directory storage.
- Basic file operations exposed through the shell and syscall layer.

<<<<<<< HEAD
### Accounts, identity, and randomness

- First boot walks you through creating an account; later boots show a login prompt. Passwords must be at least 8 characters.
- Passwords are never stored: each account keeps a random salt and a PBKDF2-HMAC-SHA256 verifier (600,000 iterations, the OWASP 2023 minimum), with the iteration count stored per record. Databases written by older versions (fixed 4,096 iterations) still log in and are upgraded in place on the next successful login.
- Logging in as a nonexistent user costs the same PBKDF2 work as a real one, so response time does not reveal which usernames exist.
- The first account also creates a machine identity (a random 32-byte seed with a SHA-256 fingerprint; a *symmetric* proof-of-possession, not a public-key identity).
- Randomness uses RDRAND when the CPU has it. Otherwise a SHA-256 entropy pool is fed by keystroke timing, and **no account, salt, or identity is created until the pool is seeded**: signup asks you to type random keys while a progress counter fills.

### Storage encryption

A freshly formatted disk is encrypted with AES-256-CTR (constant-time implementation, no secret-indexed lookup tables). **Read the limits in [`SECURITY.md`](SECURITY.md#disk-encryption-limits) before relying on it**: the key is stored in the disk's own superblock, so this protects against casual inspection of a raw image, not against someone who knows the format.

### Networking

An RTL8139 driver with ARP, IPv4, ICMP echo, and UDP. Under QEMU's user-mode network (`10.0.2.15`, gateway `10.0.2.2`), `ping 10.0.2.2` completes a real round trip; the test suite checks this on every run.

=======
>>>>>>> origin/main
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

<<<<<<< HEAD
Every pointer argument is validated against the live page tables before the kernel touches it: not in the NULL guard page, no address-space wrap-around, every page present (and writable where the kernel writes), and — when the caller's code segment is ring 3 — user-accessible and outside the kernel image. Strings must be NUL-terminated within a bound and filenames that do not fit are rejected rather than truncated. Bad arguments return an error instead of faulting inside the kernel. Return convention: `SYS_HWINFO`/`SYS_MEMINFO`/`SYS_WRITE` return 1 on success and 0 on failure; file calls return -1 on error where noted in `syscall.h`.

## Shell

```text
help  clear  echo <text>  syscall  threads  hwinfo  meminfo  pci  disks
ls  touch <name>  cat <name>  write <name> <text>  rm <name>
cp <src> <dst>  mv <src> <dst>  nano <name>
whoami  logout  netinfo  ping <a.b.c.d>
crypttest  selftest  threadtest  processtest  ps
```

`selftest` runs the in-kernel regression suite (paging range checks, syscall pointer validation, ring-0/ring-3 caller handling, RNG, synchronisation primitives). `crypttest` re-runs the disk-encryption self-test.
=======
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
>>>>>>> origin/main

The keyboard path includes PS/2 scancode handling, Shift, Caps Lock, Ctrl/Alt state tracking, cursor/editing controls, shell history navigation, and Nano input routing.

## Build and run

<<<<<<< HEAD
Requirements: `gcc` with 32-bit support, `nasm`, GNU `ld`, `grub-mkrescue` (with `xorriso` and `mtools`), `qemu-system-i386`, `python3`. On Ubuntu:

```bash
sudo apt-get install build-essential gcc-multilib nasm grub-pc-bin grub-common xorriso mtools qemu-system-x86 python3
```

Or use the provided image: `docker build -t spectreos-dev . && docker run --rm -v "$(pwd)":/spectreos spectreos-dev make check`.

```bash
make            # build build/spectreos.bin and iso/spectreos.iso
make run        # boot in QEMU (PS/2 keyboard: type in the QEMU window)
make run-log    # same, serial output to build/serial.log
```

The default disk image is `disk.img` (created on demand). The build is incremental, including header dependencies.

## Testing

```bash
make unittest          # host-side, no QEMU: crypto + checksum known-answer tests,
                       #   auth logic (signup, migration, timing, weak-RNG path)
make fstest            # filesystem + encryption regression harness, no QEMU
make test              # headless QEMU boot, checks every subsystem reports in
make test-interactive  # drives the real kernel through the keyboard in QEMU
make check             # all of the above
```

What each layer covers:

| Layer | What it proves |
| --- | --- |
| `unittest` | SHA-256, HMAC, PBKDF2, AES-256 and the Internet checksum against vectors from independent implementations (OpenSSL, Python) and published standards, including all 256 S-box values; `auth.c` signup rules, legacy-database upgrade, constant-work login, and that nothing is created from an unseeded RNG |
| `fstest` | A formatted disk is encrypted at rest, survives a reboot, and legacy unencrypted disks still mount unchanged |
| `test` | Kernel boots to the login prompt with no panic and every subsystem reports OK |
| `test-interactive` | Account creation and entropy collection, login/rejection paths, files persisting across reboot, a real ARP + ICMP round trip through the NIC driver, the disk-encryption self-test, and the in-kernel `selftest` suite |
=======
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
>>>>>>> origin/main

## Repository layout

```text
src/
├── boot/          boot assembly and Multiboot entry
├── include/       kernel subsystem interfaces
├── kernel/        kernel subsystems and hardware drivers
<<<<<<< HEAD
└── shell/         command-line shell (shell.c editor+dispatch, shell_cmd_*.c commands)

tests/             crypto/, auth_harness/ (host), fs_harness/ (host), interactive/ (QEMU)
scripts/           CI boot-test script
iso/               GRUB config; the generated ISO is written here
build/             generated objects, kernel image, ISO staging (not committed)
linker.ld          kernel linker layout
Makefile           build, run and test rules
ARCHITECTURE.md    file-by-file reference
=======
└── shell/         command-line shell

backup/            historical source snapshots
iso/               generated GRUB/ISO output
build/             generated object files and kernel image
linker.ld          kernel/user-program linker layout
Makefile           build and QEMU run rules
disk.img           local filesystem disk image
>>>>>>> origin/main
```

## Design direction

<<<<<<< HEAD
SpectreOS is being developed in layers. The immediate goal is a coherent small operating system rather than a collection of isolated demos. Higher-level features from the original SpectreOS concept—such as public-key identity, username-based discovery, trusted relationships, communication, broader hardware support, and portable/privacy-oriented operation—are later architectural work and are not represented as completed features in the current kernel.

## Security statement

SpectreOS has real security *components* (password hashing with a modern work factor, constant-time comparison, an entropy-pool RNG, constant-time AES, validated syscall arguments, disk encryption), but it does **not** yet have the *boundaries* that make them meaningful against a hostile user: there is one shared address space, nothing runs in ring 3 yet, and the disk key lives next to the data it protects. Do not store anything sensitive on it. [`SECURITY.md`](SECURITY.md) lists each limitation precisely, with the work needed to remove it.
=======
SpectreOS is being developed in layers. The immediate goal is a coherent small operating system rather than a collection of isolated demos. Higher-level features from the original SpectreOS concept—such as cryptographic user identity, username-based discovery, trusted relationships, communication, broader hardware support, and portable/privacy-oriented operation—are treated as later architectural work and are not represented as completed features in the current kernel.

## Security statement

SpectreOS currently has significant security limitations. There is no claim of production-grade isolation, secure authentication, encrypted storage, network security, or privacy-preserving operation. See [`SECURITY.md`](SECURITY.md) for the current threat model and hardening roadmap.
>>>>>>> origin/main

## Contributing

See [`CONTRIBUTING.md`](CONTRIBUTING.md) for build, coding, testing, and change-review expectations.

## License

SpectreOS is released under the **GNU General Public License v3.0 only (GPLv3)**. See [`LICENSE`](LICENSE).
