# SpectreOS build.
#
#   make              build build/spectreos.bin and iso/spectreos.iso
#   make test         boot in QEMU and check the boot log markers
#   make unittest     host-side tests: crypto/checksum vectors, auth logic
#   make fstest       filesystem + encryption regression harness
#   make check        everything that does not need a display
#
# Sources are discovered with wildcards and built by two pattern rules,
# so adding src/kernel/foo.c needs no Makefile edit. Header changes
# rebuild what depends on them (-MMD/-MP).

DISK = disk.img
DISK_SIZE_MB = 64

CC = gcc
LD = ld
AS = nasm

CFLAGS = -m32 \
         -ffreestanding \
         -fno-pie \
         -fno-stack-protector \
         -Wall \
         -Wextra \
         -MMD -MP \
         -I src/include

# Crypto is compute-bound (PBKDF2 runs 600,000 iterations at every
# login). Optimise just those translation units; everything else
# stays at -O0 so debugging the rest of the kernel stays predictable.
CRYPTO_CFLAGS := $(CFLAGS) -O2

CRYPTO_SOURCES = sha256 hmac_sha256 pbkdf2 aes256

LDFLAGS = -m elf_i386 \
          -T linker.ld

BUILD = build

KERNEL_C_SOURCES = $(wildcard src/kernel/*.c) $(wildcard src/shell/*.c)
KERNEL_C_OBJECTS = $(patsubst %.c,$(BUILD)/%.o,$(notdir $(KERNEL_C_SOURCES)))

# boot.o must come first: it holds the multiboot header.
KERNEL_OBJECTS = \
    $(BUILD)/boot.o \
    $(KERNEL_C_OBJECTS) \
    $(BUILD)/interrupts_asm.o

vpath %.c src/kernel src/shell

.PHONY: all iso run run-log run-headless test fstest unittest \
        test-interactive check clean

all: iso

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/boot.o: src/boot/boot.asm | $(BUILD)
	$(AS) -f elf32 $< -o $@

$(BUILD)/interrupts_asm.o: src/kernel/interrupts.asm | $(BUILD)
	$(AS) -f elf32 $< -o $@

$(BUILD)/%.o: %.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(foreach m,$(CRYPTO_SOURCES),$(eval $(BUILD)/$(m).o: CFLAGS = $$(CRYPTO_CFLAGS)))

$(BUILD)/spectreos.bin: $(KERNEL_OBJECTS) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(KERNEL_OBJECTS)

# The image is assembled in a staging directory. Packing iso/ itself
# would sweep the previous iso/spectreos.iso into the new one (the
# output file lives inside the tree being packed), growing the image
# on every rebuild and making grub-mkrescue fail.
iso: $(BUILD)/spectreos.bin
	rm -rf $(BUILD)/isoroot
	mkdir -p $(BUILD)/isoroot/boot/grub
	cp iso/boot/grub/grub.cfg $(BUILD)/isoroot/boot/grub/grub.cfg
	cp $(BUILD)/spectreos.bin $(BUILD)/isoroot/boot/spectreos.bin
	grub-mkrescue -o iso/spectreos.iso $(BUILD)/isoroot

-include $(wildcard $(BUILD)/*.d)

# Boots with the normal graphical window (for keyboard input -
# SpectreOS's keyboard driver is PS/2, so typing only works when
# this window has focus) AND duplicates every character printed
# (every terminal_write() call, mirrored via serial.c) into
# *this terminal* over the serial console. That's what makes the
# output scrollable and mouse-copyable: it's real text streaming
# into your actual terminal emulator, not a bitmap of an 80x25
# VGA buffer with no history, which is all the graphical window
# itself can ever be.
#
# Type into the QEMU window; read and copy from this terminal.
# QEMU treats this terminal's stdin as the serial line too, so a
# Ctrl-C here goes to the emulated serial port, not to QEMU
# itself - close the graphical window, or open the QEMU monitor
# (Ctrl-Alt-2 in that window) and type `quit`, to stop it.
run: iso $(DISK)
	qemu-system-i386 \
		-cdrom iso/spectreos.iso \
		-drive file=$(DISK),format=raw,if=ide,index=0,media=disk \
		-net nic,model=rtl8139 -net user \
		-serial stdio

# Same as `run`, but the serial stream goes to a log file instead
# of this terminal, so it doesn't compete with this terminal's
# own stdin/Ctrl-C. Open a second terminal and run
# `tail -f build/serial.log` to watch and copy from there, or
# just open the file after you're done to scroll back through
# the whole session.
run-log: iso $(DISK)
	mkdir -p build
	qemu-system-i386 \
		-cdrom iso/spectreos.iso \
		-drive file=$(DISK),format=raw,if=ide,index=0,media=disk \
		-net nic,model=rtl8139 -net user \
		-serial file:build/serial.log

# Headless boot for CI: no display, boot log goes out over the
# emulated serial port (see src/kernel/serial.c) to stdout.
# `timeout` guards against a hang (e.g. a real triple fault
# QEMU can't recover from) turning into a stuck CI job forever.
run-headless: iso $(DISK)
	timeout 30 qemu-system-i386 \
		-cdrom iso/spectreos.iso \
		-drive file=$(DISK),format=raw,if=ide,index=0,media=disk \
		-net nic,model=rtl8139 -net user \
		-display none \
		-serial stdio

# Boots headless, captures the serial log, and greps it for the
# markers every subsystem prints on a healthy boot. Exits
# non-zero (failing the CI job) if the kernel never reaches the
# expected state, whether that's a hang, a panic, or a boot that
# completes without one of these subsystems reporting OK.
test: iso $(DISK)
	bash scripts/ci_boot_test.sh

# Filesystem encryption + backward-compatibility regression test.
# Pure userspace (no QEMU, nasm, or root): builds the real fs.c plus a
# frozen pre-encryption copy against a fake file-backed ATA disk and
# checks encrypted-at-rest, survives-a-reboot, and legacy-disk-still-
# works. See tests/fs_harness/run.sh for exactly what it pins down.
fstest:
	bash tests/fs_harness/run.sh

# Host-side known-answer tests: compiles the REAL kernel sources
# (sha256, hmac, pbkdf2, aes256, checksum) natively and checks them
# against vectors produced by independent implementations.
unittest:
	bash tests/crypto/run.sh
	bash tests/auth_harness/run.sh

# Drives the real kernel in QEMU through the PS/2 keyboard (monitor
# `sendkey`): account creation, login, password rejection, shell
# commands, file create/read, reboot persistence and legacy-account
# migration. Slower than `test`; needs QEMU, python3 and no display.
test-interactive: iso
	python3 tests/interactive/run.py

# Everything that runs without a display.
check: unittest fstest test test-interactive

clean:
	rm -rf build/*
	rm -f iso/spectreos.iso


disk.img:
	dd if=/dev/zero of=disk.img bs=1M count=$(DISK_SIZE_MB)
