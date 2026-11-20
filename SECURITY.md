# Security Policy

## Security status

SpectreOS is an experimental operating-system project. It does **not** claim production-grade security, privacy, or isolation.

It does contain real security *components*, each covered by automated tests (see "What is implemented and tested"). What it lacks are the *boundaries* that make those components protect anything against a hostile local user. This document separates the two so nobody has to guess.

## What is implemented and tested

| Component | Detail | Verified by |
| --- | --- | --- |
| Password storage | PBKDF2-HMAC-SHA256, 600,000 iterations (OWASP 2023 minimum), random per-account salt, iteration count stored per record, constant-time verifier comparison, password buffers wiped after use. Older 4,096-iteration databases still log in and are upgraded on the next successful login | `tests/auth_harness`, `make test-interactive` |
| Login timing | A nonexistent username costs the same PBKDF2 work as a real one (exception: until a legacy account is upgraded by its first login, that account costs less than the current factor) | `tests/auth_harness` |
| Password policy | Minimum 8 characters, confirmation required | `tests/auth_harness`, `make test-interactive` |
| Randomness | RDRAND when present (mixed into the pool output, never used alone). Otherwise a SHA-256 pool fed by keystroke timing, ratcheted forward after every request. Credited at most 1 bit per event, 256 bits required. **No account, salt, or identity is created from an unseeded generator**: signup blocks on an entropy prompt | `tests/auth_harness`, `selftest`, `make test-interactive` |
| AES-256 | Constant-time at the C level: no secret-dependent branch or table index (S-box computed via GF(2^8) inversion, `xtime` is branch-free) | `tests/crypto` (FIPS-197, SP 800-38A, 64 random vectors vs OpenSSL, all 256 S-box values) |
| SHA-256 / HMAC / PBKDF2 | Verified against Python `hashlib`/`hmac` and published vectors for every padding boundary and long keys | `tests/crypto` |
| Syscall arguments | Every pointer is checked against the live page tables: NULL guard page, address wrap-around, present/writable per page, strings bounded and NUL-terminated, over-long filenames rejected (not truncated), I/O sizes capped. A ring-3 caller (CS RPL 3, read from the interrupt frame) must also hit user-accessible pages outside the kernel image | `selftest` |
| Secret hygiene | `secure_wipe` on passwords, derived keys, key material, and the login line buffer | review, `tests/auth_harness` |

## Known limitations

### Disk encryption limits

A freshly formatted disk is encrypted with AES-256-CTR, but:

- **The key is stored in the same disk.** The superblock (sector 0) is plaintext and holds the seed the key is derived from. Anyone with the disk image and knowledge of the format can decrypt it. This defends against casual inspection of a raw image only.
- **CTR keystream is reused on rewrite.** Key and nonce are fixed for the disk's life, so two snapshots of the image taken at different times reveal the XOR of any changed sector's old and new plaintext. A tweakable mode such as XTS, or per-write counters, is required.
- **No authenticated integrity.** Ciphertext can be modified undetected.
- **Why this is not fixed yet:** a password-derived key requires the filesystem to mount *after* login, but the account database currently lives *on* that filesystem. Fixing it means moving credentials to a plaintext, fixed-location area (or a separate partition), wrapping the disk key under a key derived from the password, and changing the on-disk format with a migration path for existing disks.

Disks formatted before encryption existed are mounted unencrypted and untouched.

### Memory isolation and userspace

- There is **no TSS** (no `ltr`, no `esp0`), so even with user pages a ring-3 to ring-0 transition would fault. The ring-3 GDT descriptors and the DPL-3 `INT 0x80` gate are plumbing only.
- There is a single shared address space and every page is supervisor-only: **no page carries the user bit and nothing runs in ring 3.** The syscall validator already distinguishes ring-0 and ring-3 callers, but until per-process page tables, user stacks, and a program loader exist, that distinction is exercised only by synthetic frames in `selftest`.
- Validation reads user memory directly after checking it (no copy-in for data buffers; filenames are copied). On a single core with a single address space this is sound today; it must be revisited with preemptible multi-process userspace.
- Kernel code and the shell both run in ring 0, so any kernel bug is a full compromise.

### Authentication

- Four accounts maximum; no account lockout or rate limiting beyond the PBKDF2 cost (about 7 s per attempt under QEMU emulation).
- There is no permission model: once logged in, every user can read and write every file.
- The machine identity is a random seed with a SHA-256 fingerprint and an HMAC proof of possession — a *symmetric* scheme. It cannot prove identity to a third party without sharing a secret. A public-key scheme (for example X25519/Ed25519) does not exist yet.
- The identity seed and the account database are stored on the same encrypted-with-a-co-located-key filesystem (see above).

### Network

- **ARP is unauthenticated.** The cache learns from every ARP packet, solicited or not, so a host on the segment can poison it. Learn only from replies to outstanding requests and add entry aging.
- ICMP echo replies are not rate-limited, and the stack is polled, so a flood stalls the idle loop.
- Receive-path bounds checks (IPv4 header and total length, fragment drop, RX ring status and length) are present; the parsers have not been fuzzed.

### Build and kernel hardening

The kernel is built with `-fno-stack-protector`: no stack canary, no W^X, no SMEP/SMAP, no KASLR (non-PAE paging has no NX bit). Kernel text is writable in the identity map.

### Account database integrity

`users.dat` is a plain file on the co-located-key filesystem with no MAC, so anyone who can write the disk image can add or replace accounts.

### Entropy

Timing-jitter credit is a conservative heuristic, not a measurement. Under an emulator, jitter depends on the host. If the machine has RDRAND, the pool is still mixed in, but a CPU with a broken RDRAND and no user input would have no independent source — hence the seeding rule above.

### Process termination, filesystem integrity, hardware

Thread/process teardown and resource reclamation are still evolving. The filesystem has a fixed metadata layout with no journaling, atomic metadata updates, or permissions. The OS runs under QEMU and has been exercised there (including real network traffic through the emulated RTL8139); physical-hardware coverage is a separate roadmap item.

### Privacy claims

The earlier SpectreOS concept included strong privacy/no-trace goals. Those are not current guarantees. Any future privacy mode must be specified from a threat model and tested against concrete host, storage, memory, network, and shutdown assumptions before it is described as secure.

## Security development priorities

```text
TSS + ring-3 entry, per-process address spaces
    ↓
copy-in/copy-out user memory API, syscall fuzz tests
    ↓
credentials outside the encrypted filesystem; password-derived, wrapped disk key
    ↓
tweakable/authenticated disk encryption (XTS or AEAD) with migration
    ↓
file permissions per user
    ↓
public-key identity (X25519/Ed25519)
    ↓
network security (authenticated ARP learning, parser fuzzing, rate limits)
    ↓
build hardening (stack protector, W^X)
    ↓
optional privacy-oriented features
```

## Reporting a vulnerability

For a private report, use GitHub's "Report a vulnerability" (Security tab, private vulnerability reporting); enable it under Settings > Code security if it is not shown. Do not publish exploit details until maintainers have had a reasonable opportunity to reproduce and analyze the issue.

Please include: affected commit/version; hardware or QEMU configuration; exact reproduction steps; expected and observed behavior; relevant logs, crash output, or register state; and whether it reproduces on a clean build (`make clean && make check`).

## Release security expectations

Before calling a release security-sensitive, SpectreOS should have at least:

1. a documented threat model;
2. deterministic privilege-transition tests (needs ring-3 entry first);
3. user-memory validation tests (**present**: `selftest`; extend with fuzzing);
4. process lifecycle/resource-reclamation tests;
5. filesystem corruption and recovery tests;
6. syscall fuzz/stress coverage where practical;
7. build and artifact verification.
