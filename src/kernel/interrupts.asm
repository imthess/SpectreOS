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
; GDT / IDT LOADERS
;
; void gdt_flush(uint32_t gdt_ptr);
; void idt_load(uint32_t idt_ptr);
;
; cdecl: single argument is at [esp + 4].
; ============================================================

gdt_flush:
    mov eax, [esp + 4]
    lgdt [eax]

    mov ax, 0x10        ; kernel data selector (GDT index 2)
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    jmp 0x08:.flush     ; kernel code selector (GDT index 1)
.flush:
    ret

idt_load:
    mov eax, [esp + 4]
    lidt [eax]
    ret

; ============================================================
; CPU EXCEPTION STUBS (isr0-isr31)
;
; Build a uniform registers_t on the stack (matches struct
; registers_t in interrupts.h) and call exception_handler().
;
; Vectors 8, 10, 11, 12, 13, 14, 17, 21, 29, 30 push an error
; code automatically; all others get a dummy 0 pushed here so
; every stub produces the same stack layout.
; ============================================================

isr_common_stub:
    pusha

    push esp
    call exception_handler
    add esp, 4

    popa
    add esp, 8          ; discard err_code + int_no
    iretd

isr0:
    push dword 0
    push dword 0
    jmp isr_common_stub

isr1:
    push dword 0
    push dword 1
    jmp isr_common_stub

isr2:
    push dword 0
    push dword 2
    jmp isr_common_stub

isr3:
    push dword 0
    push dword 3
    jmp isr_common_stub

isr4:
    push dword 0
    push dword 4
    jmp isr_common_stub

isr5:
    push dword 0
    push dword 5
    jmp isr_common_stub

isr6:
    push dword 0
    push dword 6
    jmp isr_common_stub

isr7:
    push dword 0
    push dword 7
    jmp isr_common_stub

isr8:
    push dword 8
    jmp isr_common_stub

isr9:
    push dword 0
    push dword 9
    jmp isr_common_stub

isr10:
    push dword 10
    jmp isr_common_stub

isr11:
    push dword 11
    jmp isr_common_stub

isr12:
    push dword 12
    jmp isr_common_stub

isr13:
    push dword 13
    jmp isr_common_stub

isr14:
    push dword 14
    jmp isr_common_stub

isr15:
    push dword 0
    push dword 15
    jmp isr_common_stub

isr16:
    push dword 0
    push dword 16
    jmp isr_common_stub

isr17:
    push dword 17
    jmp isr_common_stub

isr18:
    push dword 0
    push dword 18
    jmp isr_common_stub

isr19:
    push dword 0
    push dword 19
    jmp isr_common_stub

isr20:
    push dword 0
    push dword 20
    jmp isr_common_stub

isr21:
    push dword 21
    jmp isr_common_stub

isr22:
    push dword 0
    push dword 22
    jmp isr_common_stub

isr23:
    push dword 0
    push dword 23
    jmp isr_common_stub

isr24:
    push dword 0
    push dword 24
    jmp isr_common_stub

isr25:
    push dword 0
    push dword 25
    jmp isr_common_stub

isr26:
    push dword 0
    push dword 26
    jmp isr_common_stub

isr27:
    push dword 0
    push dword 27
    jmp isr_common_stub

isr28:
    push dword 0
    push dword 28
    jmp isr_common_stub

isr29:
    push dword 29
    jmp isr_common_stub

isr30:
    push dword 30
    jmp isr_common_stub

isr31:
    push dword 0
    push dword 31
    jmp isr_common_stub

; ============================================================
; HARDWARE IRQ STUBS (irq0-irq15)
;
; Same stack layout as the exception stubs. IRQs never push a
; CPU error code, so a dummy 0 is always pushed. int_no is set
; to 32+n to match the remapped PIC vectors idt_init() uses.
;
; irq_handler() returns the (possibly switched) stack pointer
; for the scheduler; that value replaces esp before popa/iret
; so a context switch takes effect immediately on return.
; ============================================================

irq_common_stub:
    pusha

    push esp
    call irq_handler
    add esp, 4

    mov esp, eax        ; switch to the stack irq_handler returned

    popa
    add esp, 8          ; discard err_code + int_no
    iretd

irq0:
    push dword 0
    push dword 32
    jmp irq_common_stub

irq1:
    push dword 0
    push dword 33
    jmp irq_common_stub

irq2:
    push dword 0
    push dword 34
    jmp irq_common_stub

irq3:
    push dword 0
    push dword 35
    jmp irq_common_stub

irq4:
    push dword 0
    push dword 36
    jmp irq_common_stub

irq5:
    push dword 0
    push dword 37
    jmp irq_common_stub

irq6:
    push dword 0
    push dword 38
    jmp irq_common_stub

irq7:
    push dword 0
    push dword 39
    jmp irq_common_stub

irq8:
    push dword 0
    push dword 40
    jmp irq_common_stub

irq9:
    push dword 0
    push dword 41
    jmp irq_common_stub

irq10:
    push dword 0
    push dword 42
    jmp irq_common_stub

irq11:
    push dword 0
    push dword 43
    jmp irq_common_stub

irq12:
    push dword 0
    push dword 44
    jmp irq_common_stub

irq13:
    push dword 0
    push dword 45
    jmp irq_common_stub

irq14:
    push dword 0
    push dword 46
    jmp irq_common_stub

irq15:
    push dword 0
    push dword 47
    jmp irq_common_stub

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