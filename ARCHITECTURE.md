# SpectreOS Architecture — File-by-File Reference

This document explains what every source file in the project does and how
it works, organized in boot/dependency order rather than alphabetically —
each section generally depends on the ones before it. `.h`/`.c` pairs are
described together as one unit.

Status tags used below:

- **(original)** — present in the very first uploaded version of this project.
- **(added)** — introduced during this development effort.
- **(modified)** — an original file that was later changed.
- **verified** — checked against an independent reference (test vectors, a
  second library, or randomized comparison against libc) before being trusted.
- **compiles/links only, never booted** — the honest limit of what could be
  checked in the environment this was built in: no `nasm`, `qemu`, or network
  access were available, so nothing here has ever actually run.

---

## 1. Boot & Entry

### `src/boot/boot.asm` (original)

The first code that runs. Contains the Multiboot header (a magic number and
flags GRUB looks for so it recognizes this as a bootable kernel), sets up an
initial stack, pushes the Multiboot magic value and info-structure pointer
that GRUB leaves in `eax`/`ebx`, and calls `kernel_main()`. Everything before
this point is GRUB's job, not SpectreOS's.

### `src/kernel/kernel.c` (original, heavily modified)

The orchestrator. `kernel_main()` runs every subsystem's init function in a
carefully ordered sequence, printing a status line for each (`"X: OK"` or
`"X: FAILED"`) — these lines are also what `scripts/ci_boot_test.sh` greps
for. The current boot order is:

1. `serial_init()` — so every subsequent line is captured over COM1 too.
2. `terminal_clear()`, `hardware_init()` — screen ready, CPU identified.
3. Multiboot magic check — refuses to continue if GRUB didn't hand off correctly.
4. `gdt_init()`, `idt_init()`, `pic_remap()`, interrupts enabled groundwork.
5. `memory_init()`, `pmm_init()`, `paging_init()` — memory subsystems, in that
   order specifically because `paging_init()` unmaps page 0 (the NULL guard),
   and both memory functions dereference the Multiboot info pointer *before*
   paging changes take effect.
6. `pci_scan_bus()`, then `net_init()` (networking needs the PCI scan to find
   an RTL8139 card).
7. `blockdev_init()`, `ata_init()`, `ramdisk_init()` — storage devices register
   themselves into the block-device abstraction.
8. `rng_init()` — entropy source, needed before anything cryptographic.
9. A disk-encryption self-test: wraps the RAM disk in AES-256-CTR with a fresh
   random key and proves round-trip correctness on every single boot, not
   just once.
10. `fs_init()` — mounts (or formats, if blank) the filesystem on the ATA disk.
11. `identity_load()`, `auth_init()` — load any existing cryptographic identity
    and account database.
12. `keyboard_init()`, PIT started, interrupts unmasked.
13. `auth_start()` — hands control to the login/signup gate instead of the
    shell directly; the shell only starts once a session is authenticated.
14. The idle loop: `while (1) { net_poll(); hlt; }` — since the NIC driver is
    polling-only (see `nic.h`), every timer/keyboard interrupt that wakes the
    CPU from `hlt` is also an opportunity to check for received network frames.

`halt_forever()` is the fallback for any unrecoverable init failure — spins on
`hlt` forever rather than continuing in a half-initialized state.

---

## 2. Core CPU Setup

### `src/kernel/gdt.c` / `src/include/gdt.h` (original)

Sets up the Global Descriptor Table: 5 entries — a mandatory null descriptor,
then kernel code/data segments (ring 0) and user code/data segments (ring 3).
The ring-3 segments (`0xFA`/`0xF2` access bytes) exist and are correctly
flagged, but nothing in the codebase currently *uses* them — no code ever
actually switches to ring 3. This is real, correct plumbing for a future
user-mode implementation, not yet a working feature.

### `src/kernel/idt.c` / `src/include/idt.h` (original)

Sets up the Interrupt Descriptor Table: 256 entries, wiring the 32 CPU
exception vectors (`isr0`–`isr31`, defined in `interrupts.asm`) and the 16
hardware IRQ vectors (`irq0`–`irq15`) to their assembly entry points. Also
registers `isr80` — the `INT 0x80` software-interrupt gate used for syscalls
— with flags `0xEE`, meaning DPL=3 (callable from ring 3, once anything ever
runs there).

### `src/kernel/interrupts.asm` (original)

Hand-written assembly stubs for every ISR/IRQ vector. Each exception stub
pushes a dummy error code (for vectors that don't have one) and the vector
number, then jumps to a common handler that does `pusha` (saving all 8
general-purpose registers) before calling into C (`exception_handler()` in
`interrupts.c` for CPU faults, or the IRQ dispatch path for hardware
interrupts). This is the exact stack layout that `registers_t` (in
`interrupts.h`) is designed to overlay — the struct's field order matches
`pusha`'s push order precisely.

### `src/kernel/interrupts.c` / `src/include/interrupts.h` (original, **modified**)

Two things live here:

- **The IRQ dispatcher** — routes timer ticks to `pit_tick()` and keyboard
  interrupts to `keyboard_handler()`, sending the PIC an end-of-interrupt
  signal afterward.
- **The panic handler** (`exception_handler()`) — originally just printed
  "KERNEL EXCEPTION" and halted. Rewritten to decode which of the 32 CPU
  exceptions fired (divide error, GPF, page fault, etc. — see
  `exception_names[]`), print the error code, read `CR2` for page faults
  (the faulting address), and dump every register to a full-screen VGA panic
  display. It also mirrors everything to the serial console (`serial.c`)
  through its own `panic_put_string`/`panic_put_hex32` helpers — deliberately
  *not* through `terminal_putchar()`'s mirroring, so the panic screen keeps
  working even if something about the rest of the kernel's state is corrupted.

### `src/kernel/pic.c` / `src/include/pic.h` (original)

Remaps the two legacy 8259 PICs so hardware IRQs land at vectors 0x20–0x2F
instead of their default 0x08–0x0F (which would otherwise collide with CPU
exception vectors). Also provides `pic_send_eoi()` (acknowledge an interrupt)
and `pic_unmask_irq()` (enable a specific IRQ line).

---

## 3. Timing

### `src/kernel/pit.c` / `src/include/pit.h` (original)

Programs the 8253/8254 Programmable Interval Timer to fire at a configurable
frequency (kernel.c uses 100 Hz) by computing a divisor from the PIT's fixed
1.193182 MHz base clock. `pit_get_ticks()` is used throughout the project as
a monotonic clock — e.g., the `ping` shell command times out based on elapsed
ticks, not wall-clock time.

---

## 4. Memory

### `src/kernel/pmm.c` / `src/include/pmm.h` (original)

The Physical Memory Manager. Maintains a bitmap (one bit per 4 KiB frame) of
which physical frames are free or used. On init, it starts by marking *every*
frame as used (safer default), then walks the Multiboot memory map to release
whichever ranges the bootloader reports as actually available RAM, then
re-reserves the first 1 MiB (BIOS/bootloader territory) and the kernel's own
image (`_kernel_start`–`_kernel_end`, symbols provided by the linker script).
`pmm_alloc_frame()`/`pmm_free_frame()` are a simple linear bitmap scan — not
fast, but correct and easy to verify by reading.

### `src/kernel/paging.c` / `src/include/paging.h` (original, **modified**)

Sets up a single page directory with one page table identity-mapping the
first 4 MiB (virtual address == physical address — there is no higher-half
kernel, no per-process address spaces). **Modification**: page 0
(`0x00000000`–`0x00000FFF`) is deliberately left unmapped. It used to be
mapped present+writable like everything else, meaning a stray NULL-pointer
dereference anywhere in the kernel would silently read or write whatever
happened to be at physical address 0. Now it raises a real page fault,
caught by the hardened panic handler above (which prints the faulting
address from `CR2`). Confirmed safe to unmap by checking that nothing in the
codebase reads/writes low physical addresses, and that the only code that
dereferences the Multiboot info pointer (`memory_init()`, `pmm_init()`) runs
*before* paging is enabled.

### `src/kernel/memory.c` / `src/include/memory.h` (original)

Distinct from `pmm.c` — this is memory *detection and reporting*, not
allocation. Reads `mem_lower`/`mem_upper` straight from the Multiboot info
structure and computes total RAM for the `meminfo` shell command and the
`SYS_MEMINFO` syscall. Doesn't allocate anything itself.

---

## 5. Hardware Detection

### `src/kernel/hardware.c` / `src/include/hardware.h` (original)

CPUID-based CPU identification: vendor string, family/model/stepping, brand
string (if the CPU supports the extended CPUID leaves for it). Used by
`hwinfo` in the shell and by `rng.c`/`rtl8139.c`/`pci.c` (each of which has
its own small local `cpuid()` inline-asm helper, following this file's
pattern rather than sharing one — a deliberate low-coupling choice throughout
the project).

### `src/kernel/pci.c` / `src/include/pci.h` **(added)**

Real PCI bus enumeration via the legacy `0xCF8`/`0xCFC` configuration-space
I/O ports. `pci_scan_bus()` walks every bus/device/function (256 × 32 × 8,
bounded and safe even where nothing responds — a non-present slot always
reads back vendor ID `0xFFFF`), recording vendor/device IDs and class codes
for up to 32 devices. Also exposes raw `pci_config_read_dword()`/
`pci_config_write_dword()` publicly (originally private) so `rtl8139.c` can
read its BAR0 and flip the bus-mastering/I/O-space-enable bits in the PCI
command register — careful to only ever write zero to the register's upper
16 bits (the write-1-to-clear status half), so enabling the NIC can never
accidentally clear a pending status flag.

---

## 6. Concurrency

### `src/kernel/thread.c` / `src/include/thread.h` (original, **modified**)

Kernel-thread bookkeeping: a fixed-size thread table, states
(`READY`/`RUNNING`/`TERMINATED`/etc.), stack allocation, and
`build_initial_stack()` — hand-constructs a fake interrupt-stack-frame for a
brand-new thread so that the *first* time the scheduler "resumes" it, the
`popa; add esp,8; iretd` epilogue in `interrupts.asm` lands correctly on the
thread's real entry point. **Modification**: the header's include guard
(`#endif`) was misplaced, leaving `thread_wake()`/`thread_wake_one()`
declared outside it — harmless in practice (repeated prototypes are legal in
C) but fixed for correctness.

### `src/kernel/scheduler.c` / `src/include/scheduler.h` (original, **modified — real bug fix**)

Round-robin (with FCFS/priority variants available) thread scheduling, called
from the timer IRQ path. **The bug**: this file used to keep its own private
`scheduler_current` variable, completely separate from `thread.c`'s own
`current_thread` — and the two started at different values (`-1` vs `0`) and
were never synchronized. Concretely, this meant the bootstrap/idle thread
could never be selected by the round-robin selector (since it starts in
`RUNNING`, not `READY`, and nothing ever transitioned it back), and anything
that asked `thread.c` "who's running?" could get a different answer than what
the scheduler actually had active. This is almost certainly what caused the
`EIP=0x00000003`-style crash that led the original author to comment out
test-thread creation in `kernel.c`. **Fix**: removed the duplicate state
entirely; the scheduler now reads/writes `thread.c`'s `current_thread`
directly via `thread_get_current_index()`/`thread_set_current()`, making
`thread.c` the single source of truth.

### `src/kernel/sync.c` / `src/include/sync.h` (original)

Spinlocks (atomic `xchg`-based test-and-set, spinning with a `pause`
instruction) and mutexes built on top of them. Straightforward, standard
single-core synchronization primitives — not exercised much yet since there's
no real concurrent workload driving contention.

### `src/kernel/process.c` / `src/include/process.h` **(added - live-tested)**
A deliberately *thin* process abstraction over `thread.c`: a PID, a PPID, a
small file-descriptor bookkeeping array, and the underlying thread's table
index. It is not the full roadmap `process_t` - no page directory, no user
stack, no ring-3 entry; every process still runs in ring 0 in the single
shared address space. Process state is **never stored independently** - it is
always derived live from `thread_get_state()` - specifically so it cannot
drift out of sync with the thread table the way `scheduler.c` once did.
All processes start at one shared `process_trampoline()` that looks itself up
by `thread_get_current_index()`, runs the caller's real entry function, and -
if that function *returns* normally rather than calling `thread_exit()`
itself - closes every fd it registered (`process_register_fd()`) before
terminating. fd tracking is bookkeeping, not isolation: `fs.c`'s handle table
is still global to the kernel. **Live result**: `threadtest` / `processtest`
showed genuinely interleaved output from two concurrent threads/processes,
`threads` reporting `switches` of 26-28 per thread, and `proctest_a.dat`
persisting after process A's automatic cleanup ran. Also confirmed in
testing: shell commands run *inside* the keyboard interrupt handler with
interrupts disabled, so a command that waits on `pit_get_ticks()` hangs
forever (this froze `ping` once) - any waiting a shell command does must be a
bounded iteration count, never timer-based.

---

## 7. Syscalls

### `src/kernel/syscall.c` / `src/include/syscall.h` (original, **modified**)

The `INT 0x80` handler: a big switch statement dispatching on a syscall
number to `hardware_get_cpu_info()`, `memory_get_info()`, VGA text output, or
the filesystem functions (`fs_open`/`fs_read`/`fs_write`/etc.). **Modification
(hardening)**: previously, several handlers had inconsistent or missing
`NULL` checks, and `syscall_write()` walked a caller-supplied string with no
length limit at all — a bad or non-terminated pointer could read forever.
Now: every pointer-taking syscall rejects `NULL` *and* near-NULL addresses
(below `0x1000`, backed up by the real page-0 hardware guard in `paging.c`);
`syscall_write()` is capped at 4096 bytes; every file-I/O syscall caps its
size argument at 65536 bytes before it ever reaches `fs.c`. Explicitly scoped
to what's meaningful in a single-address-space kernel — this is *not*
per-process pointer validation (there are no per-process address spaces to
validate against yet).

---

## 8. I/O Drivers

### `src/kernel/terminal.c` / `src/include/terminal.h` (original, **modified**)

Direct VGA text-mode framebuffer writes (`0xB8000`), cursor tracking, and
scrolling. **Modification**: `terminal_putchar()` — the one function every
other output path funnels through — now also mirrors each character to
`serial_putchar()`. This single change point means every existing
`terminal_write()` call site across the *entire* codebase (boot messages,
shell output, everything) automatically reaches the serial console too,
without needing to touch dozens of call sites individually.

### `src/kernel/keyboard.c` / `src/include/keyboard.h` (original, **modified**)

PS/2 keyboard scancode-to-ASCII translation and a small dispatch layer.
**Modification**: originally a two-way dispatch (nano editor active, or
shell). Now three-way: `auth_login_active()` is checked *first*, routing
keystrokes to `auth.c`'s login/signup handlers while a session isn't
authenticated yet, falling through to nano or the shell only once it's not.
Arrow keys/Home/End/Delete are simply ignored during login (the login line
editor only supports character entry, backspace, and enter).

### `src/kernel/serial.c` / `src/include/serial.h` **(added)**

A standard 16550 UART driver for COM1 (`0x3F8`). Runs a loopback self-test at
init (switch the UART to loopback mode, write a known byte, confirm it reads
back) before trusting the port at all — some real machines have no COM1, and
this degrades to a silent no-op rather than hanging on port I/O nothing is
listening on. Exists specifically so an automated test harness (QEMU with
`-serial stdio`, no display) can observe full boot progress and panics
without screen-scraping VGA memory.

### `src/kernel/ata.c` / `src/include/ata.h` (original, **modified**)

PIO-mode ATA disk driver: identifies the drive, reads/writes 512-byte sectors
via `rep insw`/`rep outsw` (already word-at-a-time, efficient — never needed
the `fast_mem` treatment other files got). **Modification**: on successful
init, also calls `blockdev_register()` so anything written against the
generic block-device interface can reach this disk too — the existing direct
`ata_read_sector()`/`ata_write_sector()` callers in `fs.c` are completely
untouched.

### `src/kernel/nano.c` / `src/include/nano.h` (original)

A small full-screen text editor invoked from the shell (`nano <file>`).
Handles its own keyboard dispatch mode (see `keyboard.c`), cursor movement,
line editing, and saving back through the filesystem. The largest single
original file (966 lines) aside from the shell itself.

---

## 9. Block Device Layer

### `src/kernel/blockdev.c` / `src/include/blockdev.h` **(added)**

A small device-registry abstraction — any storage backend (ATA, RAM disk,
the AES-encrypted disk) implements four function pointers
(`present`/`sector_count`/`read_sector`/`write_sector`) and registers itself
here, up to `BLOCKDEV_MAX` (4) devices. Callers that just need "a disk" can
go through `blockdev_read_sector(id, ...)` etc. rather than depending on a
specific driver. This exact pattern is reused twice more later, for network
interfaces (`nic.h`) and encrypted disks (`crypto_disk.c`'s fixed slots).

`blockdev_find_by_name()` looks a device up by name rather than assuming a
fixed id, and `BLOCKDEV_MAX` is 8 (it was 4, which an encrypted filesystem
would have filled exactly: `ata0`, `ram0`, `cenc0`, `fsenc0`).

### `src/kernel/ramdisk.c` / `src/include/ramdisk.h` **(added)**

An in-memory 256 KiB (512-sector) block device, registering into
`blockdev.h` as a second, independent backend — proof that the abstraction
above genuinely supports more than one kind of storage, not just ATA. Also
serves as the safe, non-destructive target for the disk-encryption
self-test that runs on every boot (nothing here risks corrupting real data).

---

## 10. Filesystem

### `src/kernel/fs.c` / `src/include/fs.h` (original, **modified**)

A custom flat-namespace filesystem: a superblock, a free-space bitmap, a
fixed-size inode table, and up to 16 simultaneously open file handles. Files
are addressed by name directly (no directories), capped at 16 KiB each (32
direct blocks × 512 bytes). Provides `fs_create`/`fs_open`/`fs_read`/
`fs_write`/`fs_delete`/`fs_list`/`fs_exists` — the interface every higher
layer (the shell, `identity.c`, `auth.c`) persists through.
**Modification**: this file's internal `mem_copy()`/`mem_zero()` helpers —
used on **12 call sites**, all on the `fs_read`/`fs_write` hot path, copying
up to a full 512-byte sector per call — were plain byte-at-a-time loops.
Their bodies now delegate to `fast_mem.c`'s word-aligned implementation; the
function names, signatures, and every call site are completely unchanged, so
this was a zero-risk drop-in speedup.

**At-rest encryption (added later)**: a freshly *formatted* disk is now
encrypted with AES-256-CTR via `crypto_disk.c`; the superblock (sector 0) is
always plaintext and holds an `SENC` flag, a 16-byte random seed (key =
`SHA-256("SPECTREOS-FSKEY" || seed)`), and an 8-byte random nonce in its
`reserved[]` words. Every other sector goes through `fs_disk_read_sector()` /
`fs_disk_write_sector()`, which route to `ata0` directly for a legacy disk or
to the `fsenc0` crypto wrapper for an encrypted one. **Backward compatible by
construction**: `reserved[]` is all-zero on every disk this code ever formatted
(`fs_format()` zeroes the whole struct first), and all-zero means "legacy,
unencrypted" - an existing populated disk is mounted exactly as before and is
never re-keyed or rewritten. **Honest limits**: the key sits in cleartext in
the same disk's superblock, so this stops casual inspection of a raw image,
not an attacker who knows the format; and because key and nonce are fixed for
the disk's life, plain CTR reuses a sector's keystream on every rewrite, so
two snapshots of the image taken at different times reveal the XOR of any
changed sector's old and new plaintext. A password-derived key (and a mode
without that weakness) is future work, blocked on reordering boot so the
filesystem mounts *after* login - `auth.c` currently needs `fs.c` to check a
password at all.

---

## 11. Shell & Interactive Layer

### `src/shell/shell.c` / `src/include/shell.h` (original, heavily **modified**)

The command-line interface: line editing (cursor movement, history,
backspace/delete), a large dispatch chain matching typed commands to
handler functions, and the prompt/redraw logic. Original commands: `help`,
`clear`, `ls`, `cat`, `touch`, `rm`, `write`, `cp`, `mv`, `echo`, `meminfo`,
`hwinfo`, `nano`, plus debug commands (`mem`, `ticks`, `threads`, `syscall`).
**Added across this project's development**: `pci`, `disks`, `crypttest`,
`whoami`, `logout`, `netinfo`, `ping <ip>`. Also holds small shared string
helpers (`string_equals`, `string_starts_with`) that several of the newer
modules reuse rather than duplicate.

---

## 12. Cryptographic Primitives

Every file in this section was checked against an independent reference
before being trusted anywhere else in the codebase.

### `src/kernel/sha256.c` / `src/include/sha256.h` **(added)**

FIPS 180-4 SHA-256, self-contained (no libc). **Verified** against Python's
`hashlib` across 4 vectors: the empty string, `"abc"`, a 56-byte multi-block
message, and 1,000,000 `'a'` characters (exercising the multi-block/streaming
path) — byte-for-byte exact matches.

### `src/kernel/hmac_sha256.c` / `src/include/hmac_sha256.h` **(added)**

RFC 2104 HMAC instantiated with SHA-256. **Verified** against both RFC 4231
official test cases. Wipes its internal key-block buffer through a
`volatile` pointer before returning.

### `src/kernel/pbkdf2.c` / `src/include/pbkdf2.h` **(added)**

RFC 8018 PBKDF2-HMAC-SHA256 — used to turn a login password into a stored
verifier (`auth.c`) and, separately, into an AES key (`crypto_disk.c`).
**Verified** against Python's `hashlib.pbkdf2_hmac` across 3 vectors (1
iteration, 4096 iterations, and a long password/salt pair) — exact matches.

### `src/kernel/aes256.c` / `src/include/aes256.h` **(added)**

AES-256, **forward cipher only** — deliberately no decrypt transform, because
the only use site (`crypto_disk.c`) runs AES in CTR mode, where both
encryption and decryption of the actual data are done by encrypting a
counter and XORing; the decrypt half of the AES algorithm itself is never
needed. Halves the amount of security-critical code that has to be correct.
**Verified** against the official FIPS-197 Appendix C.3 test vector, then
independently cross-checked against Python's `cryptography` library.

### `src/kernel/rng.c` / `src/include/rng.h` **(added)**

Entropy source: prefers the hardware `RDRAND` instruction (detected via
`CPUID.1:ECX.30`), retrying up to 16 times per call since `RDRAND` can
transiently fail under contention. Falls back, if `RDRAND` isn't available,
to an `xorshift32` PRNG reseeded from the CPU timestamp counter (`RDTSC`) —
explicitly documented and reported as **weak, non-cryptographic entropy**
(the boot log prints a visible `WARNING` line in this case) rather than
silently pretending to be secure.

### `src/kernel/checksum.c` / `src/include/checksum.h` **(added)**

RFC 1071 Internet checksum, split into `checksum_add()`/`checksum_finish()`
so callers (UDP) can fold a pseudo-header, a protocol header, and a payload
into one running sum before finishing it. **Verified** against the
well-known Wikipedia worked example for an IPv4 header (checksum `0xb1e6`).

### `src/kernel/secure_wipe.c` / `src/include/secure_wipe.h` **(added)**

One function: zero a buffer through a `volatile` pointer, specifically so the
compiler can't optimize away a "dead store" (a plain `memset` right before a
buffer goes out of scope is legal for a compiler to delete entirely). Used
everywhere a password, key, or other secret needs to leave memory —
`auth.c`, `identity.c`, `hmac_sha256.c`, `crypto_disk.c`.

### `src/kernel/fast_mem.c` / `src/include/fast_mem.h` **(added)**

Word-aligned `memcpy`/`memset` replacements. Copies 4 bytes at a time when
source and destination share the same alignment relative to a 4-byte
boundary, falling back to a byte loop for any leftover 1–3 bytes or for
misaligned pointer pairs (always correct, just not always sped up).
**Verified** against libc's own `memcpy`/`memset` across 40,000 randomized
trials spanning sizes 0–599 and all alignment combinations — zero mismatches.
Wired into `fs.c` (12 call sites) and `ramdisk.c`.

---

## 13. Privacy Layer

### `src/kernel/crypto_disk.c` / `src/include/crypto_disk.h` **(added)**

Transparent AES-256-CTR disk encryption, wrapping an existing block device
(from `blockdev.h`) with a second one that encrypts/decrypts every sector
on the fly. The counter for each 16-byte AES block is
`(lba × 32 + block_index)` plus an 8-byte random per-disk nonce (generated
at creation time) — meaning no two 16-byte blocks on a given encrypted disk
ever reuse the same keystream, which is the one property CTR mode absolutely
depends on. `crypto_disk_self_test()` writes a known pattern through the
encrypted view, reads it back (must match), and separately reads the *raw*
backing-device bytes (must **not** match the plaintext) — proving real
encryption is happening, not a pass-through. Because `blockdev.h`'s function
pointers carry no per-device context parameter, this supports a small fixed
number of encrypted-disk "slots" (2) with duplicated trampoline functions
rather than changing that already-working interface.

`crypto_disk_create()` generates a fresh random nonce each call, which is right
for the throwaway boot self-test over the RAM disk but **fatal for persistent
storage** - the next boot would pick a different nonce and every sector would
decrypt to garbage (this bug was hit for real while integrating encryption into
the filesystem and caught by the userspace harness before it reached any real
disk). `crypto_disk_create_with_nonce()` takes a caller-persisted nonce; use it
for anything that must survive a reboot. Both of the 2 slots are now used at
boot (`cenc0` self-test, `fsenc0` filesystem).

---

## 14. Identity & Authentication

### `src/kernel/identity.c` / `src/include/identity.h` **(added)**

A **hash-based** identity, not an asymmetric keypair — worth being precise
about, since it's the one place in the project where the honest scope is
easy to misread. At generation time, a random 32-byte seed is drawn from
`rng.c`, and the public fingerprint is
`SHA256(seed || "SPECTREOS-ID" || username)`. `identity_prove()` gives
HMAC-SHA256-based proof of possessing the seed — a *symmetric* proof, useful
locally, but not the "anyone can verify without sharing a secret" property
real peer-to-peer identity verification eventually needs (that would require
an actual elliptic-curve keypair — e.g. X25519 — which does not exist here
yet). Persisted to disk as `identity.dat` via `fs.c`; only one identity is
currently supported per machine, tied to the first account created.

### `src/kernel/auth.c` / `src/include/auth.h` **(added)**

A login/signup gate in front of the shell. On a blank disk (`users.dat`
doesn't exist), runs a first-run wizard (username → password → confirm)
that also generates the machine's cryptographic identity in the same flow.
On subsequent boots, a normal login prompt. Passwords are never stored —
only a random 16-byte salt and `PBKDF2-HMAC-SHA256(password, salt, 4096, 32)`
per account (up to 4 accounts), persisted to `users.dat`. Password buffers
are wiped (`secure_wipe.c`) the instant they've been hashed or compared.
Implements its own tiny append-only line editor (no cursor movement needed
for a single-line prompt) driven through the three-way keyboard dispatch
described under `keyboard.c` above.

---

## 15. Networking

### `src/include/net_byteorder.h` **(added)**

Header-only big-endian field helpers (`net_read16`/`net_write16`/
`net_read32`/`net_write32`) used throughout the network stack so every
protocol header is built/parsed byte-by-byte rather than relying on struct
packing and the CPU's native endianness happening to line up with network
byte order.

### `src/kernel/nic.c` / `src/include/nic.h` **(added)**

The same registration-abstraction pattern as `blockdev.h`, this time for
network interfaces: a driver provides `send`/`poll_receive` function
pointers and a MAC address. **Deliberately polling-only** — an
interrupt-driven NIC would mean routing a new PCI IRQ through the PIC/IDT,
a second hardware-interaction surface on top of the NIC programming itself;
`net_poll()` (see `net.c`) must be called periodically instead, which
`kernel.c`'s main idle loop does on every wake from `hlt`.

### `src/kernel/net_config.c` / `src/include/net_config.h` **(added)**

Shared state — this machine's local IP, subnet mask, gateway IP, and a
`net_get_local_mac()` accessor — factored out specifically to avoid a
circular dependency (the protocol layers need this; `net.c`'s higher-level
init/poll logic depends on all of the protocol layers).

### `src/kernel/ethernet.c` / `src/include/ethernet.h` **(added)**

Builds and parses 14-byte Ethernet headers, pads short frames to the minimum
60 bytes, and dispatches received frames to `arp.c` or `ip.c` by EtherType.

### `src/kernel/arp.c` / `src/include/arp.h` **(added)**

Address Resolution Protocol: sends broadcast requests, replies to requests
for our own IP, and maintains a small fixed-size (8-entry) IP→MAC cache,
learning from *any* ARP traffic seen (request or reply) rather than only
from replies to our own requests — standard behavior that saves round trips.

### `src/kernel/ip.c` / `src/include/ip.h` **(added)**

IPv4: builds/parses 20-byte headers (no options support), verifies the
header checksum on receipt (a correct header always folds to exactly zero
when checksummed over itself, checksum field included), and **explicitly
rejects fragmented packets** (checks the MF flag and fragment-offset field)
rather than attempting to reassemble them — a documented limitation, not a
silent bug. Routing is a simple same-subnet-or-gateway decision
(`resolve_next_hop()`), resolved via `arp.c`; if the MAC isn't cached yet, an
ARP request is sent automatically and the send returns "not yet" for the
caller to retry after `net_poll()` has run.

### `src/kernel/icmp.c` / `src/include/icmp.h` **(added)**

ICMP echo request/reply. Replies automatically to incoming echo requests;
`icmp_send_echo_request()` + `icmp_reply_received()` together back the
`ping` shell command, tracking only the single most recent outstanding
request (sufficient for an interactive ping command, not a general-purpose
multi-request ICMP API).

### `src/kernel/udp.c` / `src/include/udp.h` **(added)**

UDP with correct RFC 768 pseudo-header checksums (source IP, dest IP, zero
byte, protocol number, and UDP length all folded in before the packet
itself), including the RFC-mandated rule that a computed checksum of exactly
0 is transmitted as `0xFFFF`. `udp_bind(port, callback)` is the "minimal
socket API" — register one callback per port, no connection state — which
is the honest scope for "minimal" without a real user/kernel boundary to
build a heavier socket abstraction on top of.

The receive path rejects any packet whose `udp_length` exceeds
`UDP_HEADER_LEN + UDP_MAX_PAYLOAD` - the same cap `udp_send()` enforces. Before
this, an oversized packet would be checksummed against a silently truncated
copy (wrongly failing valid large packets) and, with the legal checksum-of-0
"disabled" setting, would hand the full oversized length to a bound callback,
which could overflow a fixed buffer written against the documented cap. (The
other protocol layers were reviewed and already validate every length field
against the actual received buffer.)

### `src/kernel/rtl8139.c` / `src/include/rtl8139.h` **(added — compiles/links only, never booted)**

A real driver for the RTL8139 Ethernet card (QEMU's traditional default NIC
model), following the widely-documented programming sequence: PCI
bus-mastering + I/O-space enable, software reset with a bounded wait, RX
ring buffer setup (oversized with `WRAP` enabled so a packet is never split
across the ring boundary), a 4-slot round-robin TX descriptor scheme, and the
well-known "off-by-16" quirk in the `CAPR` register that has to be accounted
for when telling the card how much of the RX buffer has been consumed. Every
wait loop here is bounded (a fixed maximum iteration count) rather than
infinite, specifically so a wrong assumption about the hardware fails
closed — returning an error — instead of hanging the whole kernel. This file
carries the project's clearest unverified-risk warning: there is no way to
test real port I/O side effects without actual hardware or QEMU, neither of
which was available while writing it.

### `src/kernel/net.c` / `src/include/net.h` **(added)**

Top-level glue: `net_init()` brings up every layer above in dependency
order and then the RTL8139 driver; if a NIC was found, hardcodes network
configuration to QEMU's default user-mode networking (IP `10.0.2.15`,
gateway `10.0.2.2`, mask `255.255.255.0`) — a real, common hobby-OS default
for exactly that reason, but **not DHCP**, and almost certainly wrong on
real hardware or a differently configured VM. `net_poll()` drains up to 8
received frames per call, handing each to `eth_handle_frame()`.

---

## 16. Build, Packaging & CI

### `Makefile` (original, **modified**)

Defines `KERNEL_OBJECTS` (every `.o` file to link) and per-file build rules,
plus `iso` (via `grub-mkrescue`) and `run` (boots in QEMU with a display).
**Added**: `run-headless` (same boot, no display, serial piped to stdout,
wrapped in a 30-second `timeout` so a hang can't block CI forever) and
`test` (runs `scripts/ci_boot_test.sh`).

### `iso/boot/grub/grub.cfg` (original)

A minimal GRUB configuration: one menu entry, zero timeout, loads
`spectreos.bin` as a multiboot kernel.

### `scripts/ci_boot_test.sh` **(added — script logic verified, real boot never verified)**

Boots headless under QEMU with a timeout, captures the serial log, and greps
it against every subsystem's actual boot-time marker string (`"ATA: OK"`,
`"Filesystem: OK"`, etc. — pulled directly from the literal strings in
`kernel.c`, not guessed from memory), failing loudly if any marker is
missing or if a `"KERNEL PANIC"` string appears anywhere in the log. Treats
the RNG line specially (accepts either the hardware or the software-fallback
variant, since which one appears is legitimately hardware-dependent, not a
pass/fail signal). **Verified**: the script's own control-flow logic was
tested by substituting a fake `qemu-system-i386` that emits a canned log
instead of actually booting anything, run against four scenarios — a fully
healthy log (passes), a log missing one marker (fails, names which one), a
log containing a panic string (fails), and a completely empty log (fails
with a specific diagnostic) — all four produced the correct exit code. What
remains unverified is the thing being tested: an actual kernel boot.

### `Dockerfile` **(added)**

A reproducible Ubuntu 24.04-based build/test environment: `nasm`,
`grub-pc-bin`, `xorriso`, `qemu-system-x86`, `build-essential`, and
`gcc-multilib`. Source is mounted at `docker run` time rather than copied
into the image, so editing code on the host doesn't require rebuilding the
image — only a change to this file's package list does.

### `.github/workflows/build-and-test.yml` **(added)**

Builds the Docker image and runs `make test` inside it on every push/PR,
uploading the captured boot log as a build artifact regardless of pass/fail.
The `"on":` trigger key is explicitly quoted — YAML 1.1 parsers (like
Python's PyYAML) otherwise interpret the bare word `on` as the boolean
`true`, which is harmless for GitHub Actions' own parser but was removed
as a needless ambiguity.

### `tests/fs_harness/` and `make fstest` **(added - runnable without QEMU)**
Builds the real `src/kernel/fs.c`, plus a *frozen copy of the pre-encryption
one* (`legacy/fs.c`, a fixture - do not "fix" it), as ordinary userspace
programs against a fake ATA disk backed by a plain file; each separate process
run is a simulated reboot of a real disk image. 17 checks: a fresh disk is
encrypted at rest (no plaintext anywhere in the raw image) and still readable
after reboots; multi-sector files and deletion work; a legacy disk formatted by
the old code still mounts, reads, and accepts writes under the new code, its
`reserved[]` and superblock byte-for-byte untouched, and the old code can still
read what the new code wrote. Mutation-checked: reintroducing the per-boot
random-nonce bug, or silently bypassing encryption, makes it fail loudly.
Mirrors the kernel's real boot order (`cenc0` self-test claims a crypto slot
before the filesystem claims the next). Runs in CI before the boot test.

---

## 17. `backup/phase-final/`

A snapshot of an earlier version of several core files (`gdt.c`, `idt.c`,
`kernel.c`, `scheduler.c`, `shell.c`, etc.), left in the repository by
whoever built the original project, predating any of the work described
above. Not referenced by the build system (the `Makefile` only ever points
at `src/`) — effectively inert reference material, not live code.

---

## Summary by the numbers

- **Original files**: ~30, forming the standalone kernel/shell/filesystem core.
- **Added in this effort**: ~55 new files across hardening, encryption,
  authentication, identity, networking, and CI tooling.
- **One confirmed, fixed bug**: the scheduler/thread state desync (Section 6).
- **One hardware security gap closed**: the NULL-page guard (Section 4).
- **Fully independently verified**: every cryptographic primitive and the
  Internet checksum (Section 12), plus the memory-copy speedup (also
  Section 12) and the CI test script's own logic (Section 16).
- **Compiles and links cleanly, but never actually booted**: the entire
  project, as a whole — no `nasm`/`qemu`/network access existed in the
  environment this was built in. `scripts/ci_boot_test.sh` and the GitHub
  Actions workflow are what turn that from a permanent caveat into a
  one-time gap, the first time someone with real hardware runs them.

## SpectreOS Architecture

### 1. System model

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
