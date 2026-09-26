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
         -I src/include

LDFLAGS = -m elf_i386 \
          -T linker.ld

BUILD = build

KERNEL_OBJECTS = \
    $(BUILD)/boot.o \
    $(BUILD)/kernel.o \
    $(BUILD)/hardware.o \
    $(BUILD)/pci.o \
    $(BUILD)/blockdev.o \
    $(BUILD)/ata.o \
    $(BUILD)/ramdisk.o \
    $(BUILD)/sha256.o \
    $(BUILD)/hmac_sha256.o \
    $(BUILD)/pbkdf2.o \
    $(BUILD)/aes256.o \
    $(BUILD)/rng.o \
    $(BUILD)/secure_wipe.o \
    $(BUILD)/crypto_disk.o \
    $(BUILD)/serial.o \
    $(BUILD)/identity.o \
    $(BUILD)/auth.o \
    $(BUILD)/fast_mem.o \
    $(BUILD)/checksum.o \
    $(BUILD)/nic.o \
    $(BUILD)/net_config.o \
    $(BUILD)/ethernet.o \
    $(BUILD)/arp.o \
    $(BUILD)/ip.o \
    $(BUILD)/icmp.o \
    $(BUILD)/udp.o \
    $(BUILD)/rtl8139.o \
    $(BUILD)/net.o \
    $(BUILD)/fs.o \
    $(BUILD)/memory.o \
    $(BUILD)/terminal.o \
    $(BUILD)/keyboard.o \
	$(BUILD)/gdt.o \
    $(BUILD)/idt.o \
    $(BUILD)/interrupts.o \
    $(BUILD)/pic.o \
    $(BUILD)/pmm.o \
    $(BUILD)/paging.o \
    $(BUILD)/syscall.o \
    $(BUILD)/shell.o \
    $(BUILD)/thread.o \
    $(BUILD)/sync.o \
    $(BUILD)/scheduler.o \
	$(BUILD)/nano.o \
    $(BUILD)/pit.o

all: iso

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/boot.o: src/boot/boot.asm | $(BUILD)
	$(AS) -f elf32 $< -o $@

$(BUILD)/kernel.o: src/kernel/kernel.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/hardware.o: src/kernel/hardware.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/pci.o: src/kernel/pci.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/blockdev.o: src/kernel/blockdev.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ramdisk.o: src/kernel/ramdisk.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/sha256.o: src/kernel/sha256.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/hmac_sha256.o: src/kernel/hmac_sha256.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/pbkdf2.o: src/kernel/pbkdf2.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/aes256.o: src/kernel/aes256.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/rng.o: src/kernel/rng.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/secure_wipe.o: src/kernel/secure_wipe.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/crypto_disk.o: src/kernel/crypto_disk.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/serial.o: src/kernel/serial.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/identity.o: src/kernel/identity.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/auth.o: src/kernel/auth.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/fast_mem.o: src/kernel/fast_mem.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/checksum.o: src/kernel/checksum.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/nic.o: src/kernel/nic.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/net_config.o: src/kernel/net_config.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ethernet.o: src/kernel/ethernet.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/arp.o: src/kernel/arp.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ip.o: src/kernel/ip.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/icmp.o: src/kernel/icmp.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/udp.o: src/kernel/udp.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/rtl8139.o: src/kernel/rtl8139.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/net.o: src/kernel/net.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ata.o: src/kernel/ata.c | $(BUILD)
	$(CC) $(CFLAGS) -c src/kernel/ata.c -o $(BUILD)/ata.o

$(BUILD)/fs.o: src/kernel/fs.c | $(BUILD)
	$(CC) $(CFLAGS) -c src/kernel/fs.c -o $(BUILD)/fs.o

$(BUILD)/memory.o: src/kernel/memory.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/terminal.o: src/kernel/terminal.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/keyboard.o: src/kernel/keyboard.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/gdt.o: src/kernel/gdt.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/idt.o: src/kernel/idt.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/interrupts.o: src/kernel/interrupts.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/pic.o: src/kernel/pic.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/pmm.o: src/kernel/pmm.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/paging.o: src/kernel/paging.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/syscall.o: src/kernel/syscall.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/shell.o: src/shell/shell.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/thread.o: src/kernel/thread.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/sync.o: src/kernel/sync.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/scheduler.o: src/kernel/scheduler.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/nano.o: src/kernel/nano.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/pit.o: src/kernel/pit.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/interrupts_asm.o: src/kernel/interrupts.asm | $(BUILD)
	$(AS) -f elf32 $< -o $@

$(BUILD)/spectreos.bin: $(KERNEL_OBJECTS) $(BUILD)/interrupts_asm.o
	$(LD) $(LDFLAGS) -o $@ \
		$(KERNEL_OBJECTS) \
		$(BUILD)/interrupts_asm.o

iso: $(BUILD)/spectreos.bin
	mkdir -p iso/boot
	cp $(BUILD)/spectreos.bin iso/boot/spectreos.bin
	grub-mkrescue -o iso/spectreos.iso iso

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

clean:
	rm -rf build/*
	rm -f iso/boot/spectreos.bin
	rm -f iso/spectreos.iso


disk.img:
	dd if=/dev/zero of=disk.img bs=1M count=$(DISK_SIZE_MB)
