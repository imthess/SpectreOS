# Contributing to SpectreOS

Thank you for contributing to SpectreOS. The project is a from-scratch operating-system experiment, so small, well-isolated changes are easier to verify than large cross-subsystem patches.

## Development setup

A typical development environment needs:

- GNU GCC with 32-bit compilation support
- GNU `ld`
- NASM
- GRUB image-generation tools
- QEMU for x86 testing
- `make`
- standard Unix command-line tools such as `dd`, `cp`, and `rm`

Build the kernel and ISO with:

```bash
make clean
make
```

Run the image with:

```bash
make run
```

## Before submitting a change

At minimum:

1. Build the project from a clean tree.
2. Boot the resulting image in QEMU when the change affects runtime behavior.
3. Exercise the affected shell command, driver, or kernel subsystem.
4. Check the build output for new warnings.
5. Keep generated build artifacts out of commits unless they are intentionally part of a release.

## Coding guidelines

- Keep kernel code freestanding and explicit.
- Prefer small functions with clear ownership of resources.
- Validate pointers and bounds before using them.
- Avoid hidden dependencies between subsystems.
- Keep hardware-specific code in the appropriate driver/module.
- Keep public interfaces in `src/include/` synchronized with their implementation.
- Use fixed-width integer types where hardware-visible sizes matter.
- Comment unusual assembly, interrupt-frame layouts, paging assumptions, and on-disk structures.
- Do not silently change an on-disk filesystem layout without documenting compatibility implications.

## Kernel safety

Changes affecting interrupts, context switching, paging, the syscall path, process state, or filesystem metadata should be reviewed conservatively. These subsystems can fail catastrophically even when a local function appears correct.

For scheduler and context-switch changes, document:

- what context is saved/restored;
- which stack the CPU is using before and after the transition;
- whether the code can run in interrupt context;
- how blocked/terminated threads are selected;
- what happens when no runnable thread exists.

For paging and userspace changes, document:

- virtual/physical mappings;
- privilege bits;
- page ownership;
- kernel stack behavior on Ring 3 transitions;
- user-pointer validation.

## Commits and pull requests

Use commit messages that describe the subsystem and the change, for example:

```text
paging: add per-process page directory scaffold
fs: reduce bitmap write amplification
keyboard: fix extended arrow-key handling
```

A useful pull request should include:

- a short summary;
- why the change is needed;
- affected files/subsystems;
- how it was tested;
- known limitations or follow-up work.

## Documentation

When behavior changes, update the relevant documentation in the same change where practical:

- `README.md` for user-facing project status and usage;
- `ARCHITECTURE.md` for subsystem design;
- `ROADMAP.md` for milestone status;
- `SECURITY.md` for security assumptions and limitations.

## License

By contributing, you agree that your contribution is provided under the repository's **GNU GPLv3-only** license, subject to any separate terms you explicitly provide that the project accepts.
