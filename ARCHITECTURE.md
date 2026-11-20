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
- **booted** — the kernel has been booted in QEMU and driven through the PS/2
  keyboard by `make test-interactive`. Tags of the form "compiles/links only"
  that appeared in earlier revisions of this document are obsolete: the whole
  kernel, including the RTL8139 driver (real ARP + ICMP round trip to the
  QEMU gateway), now runs under test.

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
   and both memory functions dereference the Multiboot info pointer _before_
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
flagged, but nothing in the codebase currently _uses_ them — no code ever
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

### `src/kernel/interrupts.c` / `src/include/interrupts.h` (original, modified)

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
  _not_ through `terminal_putchar()`'s mirroring, so the panic screen keeps
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
which physical frames are free or used. On init, it starts by marking _every_
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
_before_ paging is enabled.

### `src/kernel/memory.c` / `src/include/memory.h` (original)

Distinct from `pmm.c` — this is memory _detection and reporting_, not
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

### `src/kernel/pci.c` / `src/include/pci.h` (added)

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

### `src/kernel/thread.c` / `src/include/thread.h` (original, modified)

Kernel-thread bookkeeping: a fixed-size thread table, states
(`READY`/`RUNNING`/`TERMINATED`/etc.), stack allocation, and
`build_initial_stack()` — hand-constructs a fake interrupt-stack-frame for a
brand-new thread so that the _first_ time the scheduler "resumes" it, the
`popa; add esp,8; iretd` epilogue in `interrupts.asm` lands correctly on the
thread's real entry point. **Modification**: the header's include guard
(`#endif`) was misplaced, leaving `thread_wake()`/`thread_wake_one()`
declared outside it — harmless in practice (repeated prototypes are legal in
C) but fixed for correctness.

### `src/kernel/scheduler.c` / `src/include/scheduler.h` (original, modified — real bug fix)

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

### `src/kernel/process.c` / `src/include/process.h` (added - live-tested)

A deliberately _thin_ process abstraction over `thread.c`: a PID, a PPID, a
small file-descriptor bookkeeping array, and the underlying thread's table
index. It is not the full roadmap `process_t` - no page directory, no user
stack, no ring-3 entry; every process still runs in ring 0 in the single
shared address space. Process state is **never stored independently** - it is
always derived live from `thread_get_state()` - specifically so it cannot
drift out of sync with the thread table the way `scheduler.c` once did.
All processes start at one shared `process_trampoline()` that looks itself up
by `thread_get_current_index()`, runs the caller's real entry function, and -
if that function _returns_ normally rather than calling `thread_exit()`
itself - closes every fd it registered (`process_register_fd()`) before
terminating. fd tracking is bookkeeping, not isolation: `fs.c`'s handle table
is still global to the kernel. **Live result**: `threadtest` / `processtest`
showed genuinely interleaved output from two concurrent threads/processes,
`threads` reporting `switches` of 26-28 per thread, and `proctest_a.dat`
persisting after process A's automatic cleanup ran. Also confirmed in
testing: shell commands run _inside_ the keyboard interrupt handler with
interrupts disabled, so a command that waits on `pit_get_ticks()` hangs
forever (this froze `ping` once) - any waiting a shell command does must be a
bounded iteration count, never timer-based.

---

## 7. Syscalls

### `src/kernel/syscall.c` / `src/include/syscall.h` (original, modified)

The `INT 0x80` handler: a switch dispatching on a syscall number to
`hardware_get_cpu_info()`, `memory_get_info()`, VGA text output, or the
filesystem functions. **Argument validation** is backed by the live page
tables (`paging_range_ok()` in `paging.c`): every pointer must be above the
NULL-guard page, must not wrap the 32-bit space, and every page it touches
must be present (and writable if the kernel writes through it). Strings are
scanned page by page and must be NUL-terminated within a bound; filenames are
copied into a kernel buffer and rejected, not truncated, if they do not fit
`FS_MAX_FILENAME`; I/O sizes are capped at 65536. `syscall_handler()` reads the
caller's CS from the interrupt frame (`regs[9]`): for RPL 3 every page must
additionally carry the user bit and the range must not overlap the kernel
image. Return convention: `SYS_HWINFO`, `SYS_MEMINFO`, `SYS_WRITE` return 1 on
success and 0 on failure. **Limits**: single shared address space, no page has
the user bit yet, and data buffers are read in place after the check (no
copy-in), which is sound only while there is no preemptible userspace. Tested
by `selftest` (including fabricated ring-0 and ring-3 frames).

---

## 8. I/O Drivers

### `src/kernel/terminal.c` / `src/include/terminal.h` (original, modified)

Direct VGA text-mode framebuffer writes (`0xB8000`), cursor tracking, and
scrolling. **Modification**: `terminal_putchar()` — the one function every
other output path funnels through — now also mirrors each character to
`serial_putchar()`. This single change point means every existing
`terminal_write()` call site across the _entire_ codebase (boot messages,
shell output, everything) automatically reaches the serial console too,
without needing to touch dozens of call sites individually.

### `src/kernel/keyboard.c` / `src/include/keyboard.h` (original, modified)

PS/2 keyboard scancode-to-ASCII translation and a small dispatch layer.
**Modification**: originally a two-way dispatch (nano editor active, or
shell). Now three-way: `auth_login_active()` is checked _first_, routing
keystrokes to `auth.c`'s login/signup handlers while a session isn't
authenticated yet, falling through to nano or the shell only once it's not.
Arrow keys/Home/End/Delete are simply ignored during login (the login line
editor only supports character entry, backspace, and enter).

### `src/kernel/serial.c` / `src/include/serial.h` (added)

A standard 16550 UART driver for COM1 (`0x3F8`). Runs a loopback self-test at
init (switch the UART to loopback mode, write a known byte, confirm it reads
back) before trusting the port at all — some real machines have no COM1, and
this degrades to a silent no-op rather than hanging on port I/O nothing is
listening on. Exists specifically so an automated test harness (QEMU with
`-serial stdio`, no display) can observe full boot progress and panics
without screen-scraping VGA memory.

### `src/kernel/ata.c` / `src/include/ata.h` (original, modified)

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

### `src/kernel/blockdev.c` / `src/include/blockdev.h` (added)

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

### `src/kernel/ramdisk.c` / `src/include/ramdisk.h` (added)

An in-memory 256 KiB (512-sector) block device, registering into
`blockdev.h` as a second, independent backend — proof that the abstraction
above genuinely supports more than one kind of storage, not just ATA. Also
serves as the safe, non-destructive target for the disk-encryption
self-test that runs on every boot (nothing here risks corrupting real data).

---

## 10. Filesystem

### `src/kernel/fs.c` / `src/include/fs.h` (original, modified)

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

**At-rest encryption (added later)**: a freshly _formatted_ disk is now
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
filesystem mounts _after_ login - `auth.c` currently needs `fs.c` to check a
password at all.

---

## 11. Shell & Interactive Layer

### `src/shell/` and `src/include/shell.h`, `shell_internal.h` (original, heavily modified)

The command-line interface, split by responsibility:

- `shell.c` — line editing (cursor movement, history, backspace/delete),
  prompt/redraw, the keyboard entry points, and the `shell_execute()` command
  dispatch chain.
- `shell_util.c` — string comparison, number printing, argument tokenising.
- `shell_cmd_fs.c` — `ls touch rm cat write cp mv nano`.
- `shell_cmd_net.c` — `netinfo`, `ping`.
- `shell_cmd_sys.c` — `hwinfo`, `meminfo` helper, `pci`, `disks`, `crypttest`,
  `whoami`, `logout`, `threadtest`, `processtest`.

`shell_internal.h` is the private interface between these files. Also:
`selftest` (runs `selftest.c`: paging, syscall validation, RNG, sync) and
`ps`, `threads`, `syscall`, `help`, `clear`, `echo`.

---

## 12. Cryptographic Primitives

Every file in this section was checked against an independent reference
before being trusted anywhere else in the codebase.

### `src/kernel/sha256.c` / `src/include/sha256.h` (added)

FIPS 180-4 SHA-256, self-contained (no libc). **Verified** against Python's
`hashlib` across 4 vectors: the empty string, `"abc"`, a 56-byte multi-block
message, and 1,000,000 `'a'` characters (exercising the multi-block/streaming
path) — byte-for-byte exact matches.

### `src/kernel/hmac_sha256.c` / `src/include/hmac_sha256.h` (added)

RFC 2104 HMAC instantiated with SHA-256. **Verified** against both RFC 4231
official test cases. Wipes its internal key-block buffer through a
`volatile` pointer before returning.

### `src/kernel/pbkdf2.c` / `src/include/pbkdf2.h` (added)

RFC 8018 PBKDF2-HMAC-SHA256 — used to turn a login password into a stored
verifier (`auth.c`) and, separately, into an AES key (`crypto_disk.c`).
**Verified** by `tests/crypto` against Python's `hashlib.pbkdf2_hmac` across 6
vectors (1, 2, 3, 10 and 4096 iterations; output lengths 20, 32, 45, 64
including non-block-multiples; empty and long inputs). Compiled with `-O2`
(see `CRYPTO_CFLAGS` in the Makefile) because `auth.c` runs 600,000 iterations
per login.

### `src/kernel/aes256.c` / `src/include/aes256.h` (added)

AES-256, **forward cipher only** — deliberately no decrypt transform, because
the only use site (`crypto_disk.c`) runs AES in CTR mode, where both
encryption and decryption of the actual data are done by encrypting a
counter and XORing; the decrypt half of the AES algorithm itself is never
needed. Halves the amount of security-critical code that has to be correct.
The S-box is **computed, not looked up**: multiplicative inverse in GF(2^8)
via `x^254`, then the affine transform, with branch-free `xtime`/`gmul`. No
secret-dependent branch or table index remains (constant-time at the C
level; audit the object file if a hostile local observer matters).
**Verified** by `tests/crypto`: FIPS-197 C.3, SP 800-38A F.1.5, 64 random
key/plaintext pairs against OpenSSL, and all 256 computed S-box values against
the FIPS-197 table (kept only as a test oracle in `tests/crypto/sbox_ref.h`).

### `src/kernel/rng.c` / `src/include/rng.h` (added)

A SHA-256 pool plus hash-counter generator. Keyboard interrupts call
`rng_add_entropy()` with the scancode and TSC; the pool is updated as
`SHA-256("mix" || pool || sample || tsc || gap)`. One bit is credited per
event, and only if the inter-event gap differs from the previous gap; the pool
is _seeded_ at 256 credited bits, or immediately if `RDRAND` exists
(`CPUID.1:ECX.30`, retried up to 16 times). Output block _i_ is
`SHA-256("out" || pool || counter)`; RDRAND words, when present, are XORed in;
the pool is ratcheted forward after every request so a later pool compromise
does not reveal earlier output. `rng_is_seeded()` gates key creation:
`auth.c` collects keystrokes at signup until it returns 1. The credit figure
is a deliberately stingy heuristic, not a measurement; jitter under an
emulator depends on the host.

### `src/kernel/checksum.c` / `src/include/checksum.h` (added)

RFC 1071 Internet checksum, split into `checksum_add()`/`checksum_finish()`
so callers (UDP) can fold a pseudo-header, a protocol header, and a payload
into one running sum before finishing it. **Verified** against the
well-known Wikipedia worked example for an IPv4 header (checksum `0xb861`).

### `src/kernel/secure_wipe.c` / `src/include/secure_wipe.h` **(added)**

One function: zero a buffer through a `volatile` pointer, specifically so the
compiler can't optimize away a "dead store" (a plain `memset` right before a
buffer goes out of scope is legal for a compiler to delete entirely). Used
everywhere a password, key, or other secret needs to leave memory —
`auth.c`, `identity.c`, `hmac_sha256.c`, `crypto_disk.c`.

### `src/kernel/fast_mem.c` / `src/include/fast_mem.h` (added)

Word-aligned `memcpy`/`memset` replacements. Copies 4 bytes at a time when
source and destination share the same alignment relative to a 4-byte
boundary, falling back to a byte loop for any leftover 1–3 bytes or for
misaligned pointer pairs (always correct, just not always sped up).
**Verified** against libc's own `memcpy`/`memset` across 40,000 randomized
trials spanning sizes 0–599 and all alignment combinations — zero mismatches.
Wired into `fs.c` (12 call sites) and `ramdisk.c`.

---

## 13. Privacy Layer

### `src/kernel/crypto_disk.c` / `src/include/crypto_disk.h` (added)

Transparent AES-256-CTR disk encryption, wrapping an existing block device
(from `blockdev.h`) with a second one that encrypts/decrypts every sector
on the fly. The counter for each 16-byte AES block is
`(lba × 32 + block_index)` plus an 8-byte random per-disk nonce (generated
at creation time) — meaning no two 16-byte blocks on a given encrypted disk
ever reuse the same keystream, which is the one property CTR mode absolutely
depends on. `crypto_disk_self_test()` writes a known pattern through the
encrypted view, reads it back (must match), and separately reads the _raw_
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

### `src/kernel/identity.c` / `src/include/identity.h` (added)

A **hash-based** identity, not an asymmetric keypair — worth being precise
about, since it's the one place in the project where the honest scope is
easy to misread. At generation time, a random 32-byte seed is drawn from
`rng.c`, and the public fingerprint is
`SHA256(seed || "SPECTREOS-ID" || username)`. `identity_prove()` gives
HMAC-SHA256-based proof of possessing the seed — a _symmetric_ proof, useful
locally, but not the "anyone can verify without sharing a secret" property
real peer-to-peer identity verification eventually needs (that would require
an actual elliptic-curve keypair — e.g. X25519 — which does not exist here
yet). Persisted to disk as `identity.dat` via `fs.c`; only one identity is
currently supported per machine, tied to the first account created.

### `src/kernel/auth.c` / `src/include/auth.h` (added)

A login/signup gate in front of the shell. On a blank disk (`users.dat`
doesn't exist), runs a first-run wizard (username → password → confirm)
that also generates the machine's cryptographic identity in the same flow.
On subsequent boots, a normal login prompt. Passwords are never stored —
only a random 16-byte salt, an iteration count, and
`PBKDF2-HMAC-SHA256(password, salt, iterations, 32)` per account (up to 4
accounts; 600,000 iterations for new/re-hashed records), persisted to
`users.dat` (magic `SAU2`). Databases in the old layout (`SAUT`, fixed 4096
iterations) are read, treated as 4096-iteration records, and rewritten in the
new layout with a fresh salt on the next successful login. An unknown username
runs a dummy PBKDF2 so timing does not reveal which users exist. Passwords
must be 8+ characters. If `rng_is_seeded()` is false at signup, an entropy
stage ("type random keys") runs before any salt or identity is generated. Password buffers
are wiped (`secure_wipe.c`) the instant they've been hashed or compared.
Implements its own tiny append-only line editor (no cursor movement needed
for a single-line prompt) driven through the three-way keyboard dispatch
described under `keyboard.c` above.

---

## 15. Networking

### `src/include/net_byteorder.h` (added)

Header-only big-endian field helpers (`net_read16`/`net_write16`/
`net_read32`/`net_write32`) used throughout the network stack so every
protocol header is built/parsed byte-by-byte rather than relying on struct
packing and the CPU's native endianness happening to line up with network
byte order.

### `src/kernel/nic.c` / `src/include/nic.h` (added)

The same registration-abstraction pattern as `blockdev.h`, this time for
network interfaces: a driver provides `send`/`poll_receive` function
pointers and a MAC address. **Deliberately polling-only** — an
interrupt-driven NIC would mean routing a new PCI IRQ through the PIC/IDT,
a second hardware-interaction surface on top of the NIC programming itself;
`net_poll()` (see `net.c`) must be called periodically instead, which
`kernel.c`'s main idle loop does on every wake from `hlt`.

### `src/kernel/net_config.c` / `src/include/net_config.h` (added)

Shared state — this machine's local IP, subnet mask, gateway IP, and a
`net_get_local_mac()` accessor — factored out specifically to avoid a
circular dependency (the protocol layers need this; `net.c`'s higher-level
init/poll logic depends on all of the protocol layers).

### `src/kernel/ethernet.c` / `src/include/ethernet.h` (added)

Builds and parses 14-byte Ethernet headers, pads short frames to the minimum
60 bytes, and dispatches received frames to `arp.c` or `ip.c` by EtherType.

### `src/kernel/arp.c` / `src/include/arp.h` (added)

Address Resolution Protocol: sends broadcast requests, replies to requests
for our own IP, and maintains a small fixed-size (8-entry) IP→MAC cache,
learning from _any_ ARP traffic seen (request or reply) rather than only
from replies to our own requests — standard behavior that saves round trips.

### `src/kernel/ip.c` / `src/include/ip.h` (added)

IPv4: builds/parses 20-byte headers (no options support), verifies the
header checksum on receipt (a correct header always folds to exactly zero
when checksummed over itself, checksum field included), and **explicitly
rejects fragmented packets** (checks the MF flag and fragment-offset field)
rather than attempting to reassemble them — a documented limitation, not a
silent bug. Routing is a simple same-subnet-or-gateway decision
(`resolve_next_hop()`), resolved via `arp.c`; if the MAC isn't cached yet, an
ARP request is sent automatically and the send returns "not yet" for the
caller to retry after `net_poll()` has run.

### `src/kernel/icmp.c` / `src/include/icmp.h` (added)

ICMP echo request/reply. Replies automatically to incoming echo requests;
`icmp_send_echo_request()` + `icmp_reply_received()` together back the
`ping` shell command, tracking only the single most recent outstanding
request (sufficient for an interactive ping command, not a general-purpose
multi-request ICMP API).

### `src/kernel/udp.c` / `src/include/udp.h` (added)

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

### `src/kernel/rtl8139.c` / `src/include/rtl8139.h` (added — verified under QEMU)

A real driver for the RTL8139 Ethernet card (QEMU's traditional default NIC
model), following the widely-documented programming sequence: PCI
bus-mastering + I/O-space enable, software reset with a bounded wait, RX
ring buffer setup (oversized with `WRAP` enabled so a packet is never split
across the ring boundary), a 4-slot round-robin TX descriptor scheme, and the
well-known "off-by-16" quirk in the `CAPR` register that has to be accounted
for when telling the card how much of the RX buffer has been consumed. Every
wait loop here is bounded (a fixed maximum iteration count) rather than
infinite, specifically so a wrong assumption about the hardware fails
closed — returning an error — instead of hanging the whole kernel. `make test-interactive`
boots the kernel under QEMU's user-mode network and checks that `ping 10.0.2.2`
completes an ARP resolution and ICMP echo round trip through this driver.

### `src/kernel/net.c` / `src/include/net.h` (added)

Top-level glue: `net_init()` brings up every layer above in dependency
order and then the RTL8139 driver; if a NIC was found, hardcodes network
configuration to QEMU's default user-mode networking (IP `10.0.2.15`,
gateway `10.0.2.2`, mask `255.255.255.0`) — a real, common hobby-OS default
for exactly that reason, but **not DHCP**, and almost certainly wrong on
real hardware or a differently configured VM. `net_poll()` drains up to 8
received frames per call, handing each to `eth_handle_frame()`.

---

## 16. Build, Packaging & CI

### `Makefile` (original, modified)

Sources are discovered by wildcard (`src/kernel/*.c`, `src/shell/*.c`) and
built by one pattern rule with `-MMD -MP` header dependencies; `boot.o` is
linked first (it holds the Multiboot header). The crypto objects (`sha256`,
`hmac_sha256`, `pbkdf2`, `aes256`) get `-O2`. `iso` assembles the image in
`build/isoroot` rather than packing `iso/` itself (the output file lives
there, and packing it swept the previous ISO into the new one). Targets:
`run`, `run-log`, `run-headless`, `test`, `fstest`, `unittest`,
`test-interactive`, `check` (everything non-graphical), `clean`.

### `iso/boot/grub/grub.cfg` (original)

A minimal GRUB configuration: one menu entry, zero timeout, loads
`spectreos.bin` as a multiboot kernel.

### `scripts/ci_boot_test.sh` (added — verified)

Boots headless under QEMU with a timeout, captures the serial log, and greps
it for every subsystem's boot-time marker (`"ATA: OK"`, `"Filesystem: OK"`,
...), failing if any is missing or a `"KERNEL PANIC"` string appears. The RNG
line accepts either the RDRAND or the entropy-pool message. It passes against
the real kernel.

### `Dockerfile` (added)

A reproducible Ubuntu 24.04-based build/test environment: `nasm`,
`grub-pc-bin`, `xorriso`, `qemu-system-x86`, `build-essential`, and
`gcc-multilib`. Source is mounted at `docker run` time rather than copied
into the image, so editing code on the host doesn't require rebuilding the
image — only a change to this file's package list does.

### `.github/workflows/build-and-test.yml` (added)

Builds the Docker image and runs `make test` inside it on every push/PR,
uploading the captured boot log as a build artifact regardless of pass/fail.
The `"on":` trigger key is explicitly quoted — YAML 1.1 parsers (like
Python's PyYAML) otherwise interpret the bare word `on` as the boolean
`true`, which is harmless for GitHub Actions' own parser but was removed
as a needless ambiguity.

### `tests/crypto/` and `make unittest` (added - no QEMU)

`test_crypto.c` compiles the real `sha256.c`, `hmac_sha256.c`, `pbkdf2.c`,
`aes256.c`, `checksum.c` natively and checks them against `vectors.h`, which
`gen_vectors.py` generates from independent implementations (Python
`hashlib`/`hmac`, the `cryptography` package/OpenSSL; fixed seed, reproducible)
plus published vectors. 22 checks.

### `tests/auth_harness/` (added - no QEMU)

Compiles the real `auth.c` and crypto against in-memory stubs for the
filesystem, terminal, shell, identity and RNG, and drives it through
`auth_keyboard_character/enter`. A linker `--wrap` on `pbkdf2_hmac_sha256`
counts hashing work. One scenario per process (auth caches its database in
statics): signup rules, weak-RNG gating, legacy-database upgrade and
equal-cost unknown-user login, corrupt database. 31 checks.

### `src/kernel/selftest.c` and the `selftest` shell command (added)

32 in-kernel checks run in the real 32-bit environment: paging range logic,
syscall pointer validation, ring-0 vs ring-3 (fabricated `INT 0x80` frames),
RNG properties, and the sync primitives (`sync_self_test()` existed but was
never called before).

### `tests/interactive/run.py` and `make test-interactive` (added)

Boots the ISO headless on a fresh disk and types through QEMU's monitor
(`sendkey`) while reading the serial log: signup validation, entropy
collection, account/identity creation, shell file commands, `netinfo` and a
real `ping 10.0.2.2`, `crypttest`, `selftest`, then a second boot on the same
disk for wrong-user, wrong-password and correct login and data persistence.

### `tests/fs_harness/` and `make fstest` **(added - runnable without QEMU)**

Builds the real `src/kernel/fs.c`, plus a _frozen copy of the pre-encryption
one_ (`legacy/fs.c`, a fixture - do not "fix" it), as ordinary userspace
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

## 17. Repository hygiene

The earlier `backup/phase-final/` snapshot no longer exists in the repository.
Generated files (`build/`, `iso/spectreos.iso`, `disk.img`, `*.o`, `*.bin`) are
excluded by `.gitignore` and must never be committed. Host-side test harnesses
live under `tests/`; the kernel is built only from `src/`.

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
- **Booted and tested end to end**: `make check` runs the host unit tests,
  the filesystem harness, the headless boot test, and a keyboard-driven QEMU
  session including real network traffic.
- **Known, documented gaps** (see `SECURITY.md`): disk key stored beside the
  data, CTR keystream reuse, no TSS / ring-3 entry, no per-process address
  spaces, no per-user file permissions, symmetric-only identity.

---

## System model (current)

```text
BIOS/firmware -> GRUB (Multiboot) -> boot.asm -> kernel_main()
  |- CPU/interrupt foundation: GDT (5 entries, no TSS), IDT, PIC, PIT
  |- Memory: Multiboot memory map, PMM, identity-mapped paging (all supervisor-only)
  |- Storage: ATA PIO -> blockdev layer -> crypto_disk (AES-256-CTR) -> filesystem
  |- Entropy/identity: RNG pool, PBKDF2 auth, symmetric machine identity
  |- Networking: RTL8139 -> Ethernet -> ARP / IPv4 -> ICMP echo / UDP (polled)
  |- Concurrency: threads, scheduler, spinlocks/mutexes/semaphores, thin processes
  |- Syscalls: INT 0x80 (DPL 3 gate) with page-table-based pointer validation
  `- Login gate -> shell (ring 0)
```

There is **no TSS** (`ltr` is never executed and no `esp0` is set), so no code can
enter ring 3 and return through the syscall gate. No page carries the user bit.
All processes are kernel threads in ring 0, as is the shell. See `SECURITY.md`.
