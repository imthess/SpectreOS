# Security Policy

## Security status

SpectreOS is an experimental operating-system project. The repository does **not** claim production-grade security, privacy, or isolation.

The current kernel has useful security-oriented building blocks—privilege descriptors, paging, a syscall entry point, process metadata, and synchronization primitives—but several critical security properties are still incomplete.

## Current security model

At the current development stage:

- Kernel code runs in Ring 0.
- User-code/user-data GDT descriptors and a TSS are present.
- `INT 0x80` is configured as a user-callable syscall entry point.
- Paging is enabled, but the current paging implementation is an early identity-mapping foundation rather than a complete per-process virtual-memory system.
- The process layer is prototype-level.
- The filesystem is persistent but does not provide a finished permission, journaling, encryption, or authenticated-integrity system.
- Authentication and the planned cryptographic user identity are not yet implemented as production security mechanisms.
- Networking is not yet a completed kernel service.

## Important limitations

### Memory isolation

A complete userspace security boundary requires independent address spaces, validated user pointers, controlled mappings, safe user stacks, page-fault handling, and correct privilege-transition behavior. These are still development targets.

### Syscalls

The syscall ABI is intentionally small, but pointer validation and argument ownership are not yet comprehensive. Do not treat current syscalls as a security boundary for hostile programs.

### Process termination

Process/thread teardown and resource reclamation are still evolving. Future work must ensure that terminated execution contexts cannot retain access to stale memory, handles, locks, or scheduler state.

### Filesystem integrity

The current filesystem uses a simple fixed metadata layout. Crash consistency, journaling, atomic metadata updates, permissions, authenticated integrity, and encrypted storage are not complete.

### Hardware trust

The OS may run under QEMU or on development hardware. Virtualized hardware observations are not equivalent to complete physical-hardware coverage. Device-driver expansion and hardware validation are separate roadmap items.

### Privacy claims

The earlier SpectreOS concept included strong privacy/no-trace goals. Those are not current product guarantees. Any future privacy mode should be specified from a threat model and tested against concrete host, storage, memory, network, and shutdown assumptions before being described as secure.

## Security development priorities

The security sequence should be:

```text
stable userspace
    ↓
address-space isolation
    ↓
validated syscalls
    ↓
process/resource isolation
    ↓
authentication + permissions
    ↓
key management
    ↓
network security
    ↓
optional privacy-oriented features
```

Higher-level identity, social, messaging, and decentralized-account features should not bypass these foundations.

## Reporting a vulnerability

For a private report, use the project's preferred maintainer contact listed on the GitHub repository. Do not publish exploit details until maintainers have had a reasonable opportunity to reproduce and analyze the issue.

Please include:

- affected commit/version
- hardware or QEMU configuration
- exact reproduction steps
- expected behavior
- observed behavior
- relevant logs, crash output, or register state
- whether the issue is reproducible on a clean build

## Release security expectations

Before calling a release security-sensitive, SpectreOS should have at least:

1. documented threat model;
2. deterministic privilege-transition tests;
3. user-memory validation tests;
4. process lifecycle/resource-reclamation tests;
5. filesystem corruption and recovery tests;
6. syscall fuzz/stress coverage where practical;
7. build and artifact verification.
