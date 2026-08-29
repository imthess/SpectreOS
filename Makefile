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
    $(BUILD)/ata.o \
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

run: iso $(DISK)
	qemu-system-i386 \
		-cdrom iso/spectreos.iso \
		-drive file=$(DISK),format=raw,if=ide,index=0,media=disk

clean:
	rm -rf build/*
	rm -f iso/boot/spectreos.bin
	rm -f iso/spectreos.iso


disk.img:
	dd if=/dev/zero of=disk.img bs=1M count=$(DISK_SIZE_MB)
