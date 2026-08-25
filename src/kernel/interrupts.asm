BITS 32

section .text

global gdt_flush
global idt_load

global isr0
global isr1
global isr2
global isr3
global isr4
global isr5
global isr6
global isr7
global isr8
global isr9
global isr10
global isr11
global isr12
global isr13
global isr14
global isr15
global isr16
global isr17
global isr18
global isr19
global isr20
global isr21
global isr22
global isr23
global isr24
global isr25
global isr26
global isr27
global isr28
global isr29
global isr30
global isr31

global irq0
global irq1
global irq2
global irq3
global irq4
global irq5
global irq6
global irq7
global irq8
global irq9
global irq10
global irq11
global irq12
global irq13
global irq14
global irq15

global isr80

extern exception_handler
extern irq_handler
extern syscall_handler

; ============================================================
; GDT
; ============================================================

gdt_flush:
    mov eax, [esp + 4]
    lgdt [eax]

    mov ax, 0x10

    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    jmp 0x08:.flush

.flush:
    ret


; ============================================================
; IDT
; ============================================================

idt_load:
    mov eax, [esp + 4]
    lidt [eax]
    ret


; ============================================================
; CPU EXCEPTIONS
; ============================================================

isr0:
    cli
    push 0
    push 0
    jmp isr_common

isr1:
    cli
    push 0
    push 1
    jmp isr_common

isr2:
    cli
    push 0
    push 2
    jmp isr_common

isr3:
    cli
    push 0
    push 3
    jmp isr_common

isr4:
    cli
    push 0
    push 4
    jmp isr_common

isr5:
    cli
    push 0
    push 5
    jmp isr_common

isr6:
    cli
    push 0
    push 6
    jmp isr_common

isr7:
    cli
    push 0
    push 7
    jmp isr_common

isr8:
    cli
    push 8
    jmp isr_common

isr9:
    cli
    push 0
    push 9
    jmp isr_common

isr10:
    cli
    push 10
    jmp isr_common

isr11:
    cli
    push 11
    jmp isr_common

isr12:
    cli
    push 12
    jmp isr_common

isr13:
    cli
    push 13
    jmp isr_common

isr14:
    cli
    push 14
    jmp isr_common

isr15:
    cli
    push 0
    push 15
    jmp isr_common

isr16:
    cli
    push 0
    push 16
    jmp isr_common

isr17:
    cli
    push 17
    jmp isr_common

isr18:
    cli
    push 0
    push 18
    jmp isr_common

isr19:
    cli
    push 0
    push 19
    jmp isr_common

isr20:
    cli
    push 0
    push 20
    jmp isr_common

isr21:
    cli
    push 0
    push 21
    jmp isr_common

isr22:
    cli
    push 0
    push 22
    jmp isr_common

isr23:
    cli
    push 0
    push 23
    jmp isr_common

isr24:
    cli
    push 0
    push 24
    jmp isr_common

isr25:
    cli
    push 0
    push 25
    jmp isr_common

isr26:
    cli
    push 0
    push 26
    jmp isr_common

isr27:
    cli
    push 0
    push 27
    jmp isr_common

isr28:
    cli
    push 0
    push 28
    jmp isr_common

isr29:
    cli
    push 0
    push 29
    jmp isr_common

isr30:
    cli
    push 30
    jmp isr_common

isr31:
    cli
    push 0
    push 31
    jmp isr_common

isr_common:
    pusha

    push esp
    call exception_handler
    add esp, 4

    popa

    add esp, 8

    iretd

; ============================================================
; HARDWARE IRQs
; ============================================================

irq0:
    cli
    push 0
    push 32
    jmp irq_common

irq1:
    cli
    push 0
    push 33
    jmp irq_common

irq2:
    cli
    push 0
    push 34
    jmp irq_common

irq3:
    cli
    push 0
    push 35
    jmp irq_common

irq4:
    cli
    push 0
    push 36
    jmp irq_common

irq5:
    cli
    push 0
    push 37
    jmp irq_common

irq6:
    cli
    push 0
    push 38
    jmp irq_common

irq7:
    cli
    push 0
    push 39
    jmp irq_common

irq8:
    cli
    push 0
    push 40
    jmp irq_common

irq9:
    cli
    push 0
    push 41
    jmp irq_common

irq10:
    cli
    push 0
    push 42
    jmp irq_common

irq11:
    cli
    push 0
    push 43
    jmp irq_common

irq12:
    cli
    push 0
    push 44
    jmp irq_common

irq13:
    cli
    push 0
    push 45
    jmp irq_common

irq14:
    cli
    push 0
    push 46
    jmp irq_common

irq15:
    cli
    push 0
    push 47
    jmp irq_common


irq_common:
    pusha

    ; Pass the address of the complete IRQ frame.
    push esp
    call irq_handler
    add esp, 4

    ; EAX contains the ESP of the thread
    ; that should continue execution.
    mov esp, eax

    ; Restore that thread's registers.
    popa

    ; Remove int_no and err_code.
    add esp, 8

    iretd


; ============================================================
; SPECTREOS SYSTEM CALL
;
; EAX = syscall number
; EBX = argument 1
; ECX = argument 2
; EDX = argument 3
; ============================================================

isr80:
    pusha

    push esp
    call syscall_handler
    add esp, 4

    popa

    iretd