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
	$(BUILD)/terminal.o \
	$(BUILD)/keyboard.o \
    $(BUILD)/gdt.o \
    $(BUILD)/idt.o \
    $(BUILD)/interrupts.o \
    $(BUILD)/pic.o \
    $(BUILD)/syscall.o\
	$(BUILD)/shell.o

all: iso

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/boot.o: src/boot/boot.asm | $(BUILD)
	$(AS) -f elf32 src/boot/boot.asm -o $(BUILD)/boot.o

$(BUILD)/kernel.o: src/kernel/kernel.c | $(BUILD)
	$(CC) $(CFLAGS) -c src/kernel/kernel.c -o $(BUILD)/kernel.o

$(BUILD)/terminal.o: src/kernel/terminal.c | $(BUILD)
	$(CC) $(CFLAGS) -c src/kernel/terminal.c -o $(BUILD)/terminal.o

$(BUILD)/keyboard.o: src/kernel/keyboard.c | $(BUILD)
	$(CC) $(CFLAGS) -c src/kernel/keyboard.c -o $(BUILD)/keyboard.o

$(BUILD)/gdt.o: src/kernel/gdt.c | $(BUILD)
	$(CC) $(CFLAGS) -c src/kernel/gdt.c -o $(BUILD)/gdt.o

$(BUILD)/idt.o: src/kernel/idt.c | $(BUILD)
	$(CC) $(CFLAGS) -c src/kernel/idt.c -o $(BUILD)/idt.o

$(BUILD)/interrupts.o: src/kernel/interrupts.c | $(BUILD)
	$(CC) $(CFLAGS) -c src/kernel/interrupts.c -o $(BUILD)/interrupts.o

$(BUILD)/pic.o: src/kernel/pic.c | $(BUILD)
	$(CC) $(CFLAGS) -c src/kernel/pic.c -o $(BUILD)/pic.o

$(BUILD)/syscall.o: src/kernel/syscall.c | $(BUILD)
	$(CC) $(CFLAGS) -c src/kernel/syscall.c -o $(BUILD)/syscall.o

$(BUILD)/shell.o: src/shell/shell.c | $(BUILD)
	$(CC) $(CFLAGS) -c src/shell/shell.c -o $(BUILD)/shell.o

$(BUILD)/interrupts_asm.o: src/kernel/interrupts.asm | $(BUILD)
	$(AS) -f elf32 src/kernel/interrupts.asm -o $(BUILD)/interrupts_asm.o

$(BUILD)/spectreos.bin: $(KERNEL_OBJECTS) $(BUILD)/interrupts_asm.o
	$(LD) $(LDFLAGS) -o $(BUILD)/spectreos.bin \
		$(KERNEL_OBJECTS) \
		$(BUILD)/interrupts_asm.o

iso: $(BUILD)/spectreos.bin
	mkdir -p iso/boot
	cp $(BUILD)/spectreos.bin iso/boot/spectreos.bin
	grub-mkrescue -o iso/spectreos.iso iso

run: iso
	qemu-system-i386 -cdrom iso/spectreos.iso 

clean:
	rm -rf build/*
	rm -f iso/boot/spectreos.bin
	rm -f iso/spectreos.iso