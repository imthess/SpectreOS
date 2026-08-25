#ifndef SPECTREOS_INTERRUPTS_H
#define SPECTREOS_INTERRUPTS_H

#include <stdint.h>

typedef struct
{
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t esp;
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;

    uint32_t int_no;
    uint32_t err_code;

    uint32_t eip;
    uint32_t cs;
    uint32_t eflags;
    uint32_t useresp;
    uint32_t ss;

} registers_t;

void interrupts_init(void);

void exception_handler(registers_t* regs);

uint32_t irq_handler(registers_t* regs);

#endif
